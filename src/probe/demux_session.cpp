#include "probe/demux_session.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

extern "C" {
// 06-04-PLAN.md (AUDIO-03, D-12): avcodec.h itself (send/receive, codec
// open/close, AV_CODEC_FLAG_BITEXACT, AV_PROFILE_AAC_HE*) -- the bounded
// one-packet SBR probe decode's own decoder-open/send/receive block below
// mirrors src/probe/audio_decode.cpp's identical include and usage.
#include <libavcodec/avcodec.h>
#include <libavcodec/codec_id.h>
#include <libavcodec/codec_par.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/dict.h>
#include <libavutil/error.h>
#include <libavutil/log.h>
// 04-11-PLAN.md (VIDEO-09): AVMasteringDisplayMetadata/
// AVContentLightMetadata's own struct layouts -- AVPacketSideData itself
// and av_packet_side_data_get() already come in transitively via
// libavcodec/codec_par.h's own #include "packet.h" above.
#include <libavutil/mastering_display_metadata.h>
#include <libavutil/pixdesc.h>
// 06-03-PLAN.md (AUDIO-01): AVChannelLayout description and sample-format
// packed-equivalent resolution -- the SAME two libavutil headers
// src/probe/audio_decode.cpp already includes for the decoded-frame case.
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
// 04-12-PLAN.md (VIDEO-09's third HDR family): AVDOVIDecoderConfigurationRecord's
// own struct layout -- AV_PKT_DATA_DOVI_CONF itself is declared in
// libavcodec/packet.h, already transitively included above.
#include <libavutil/dovi_meta.h>
}

#include <cerrno>
#include <utility>
#include <vector>

#include "core/container_family.h"
#include "probe/audio_config.h"
#include "probe/pass.h"

namespace mediadiff {

namespace {

// Backing store for default_wall_clock_budget_ms()/
// set_default_wall_clock_budget_ms() -- see this pair's own declaration
// comment in demux_session.h for the write-once-then-read-only rationale
// behind relaxed ordering.
std::atomic<std::int64_t> g_default_wall_clock_budget_ms{kDefaultProbeBudgetMs};

// The thread_local accumulator probe_log_callback writes into --
// DemuxSession::open sets this before calling into libav and clears it
// when it returns; set_current_probe_diagnostics is the single place that
// ever assigns it, whether called internally by open() or directly by a
// test.
thread_local ProbeDiagnostics* g_current_diagnostics = nullptr;

// `>=` rather than `>`: a 0 ms budget must fail on the very first check
// rather than requiring elapsed time to exceed zero, which it trivially
// always does except at t=0 itself.
int check_interrupt(void* opaque) {
  const auto* state = static_cast<const detail::InterruptState*>(opaque);
  const auto elapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - state->start).count();
  return elapsed_ms >= state->budget_ms ? 1 : 0;
}

// The first comma-delimited token of `name` (doc 02 section 2's
// container.format extraction rule) -- "mov,mp4,m4a,3gp,3g2,mj2" becomes
// "mov"; "matroska,webm" becomes "matroska". A null `name` (no iformat
// resolved, which avformat_open_input's own success contract never
// actually produces, but nothing here assumes that at the type level)
// yields an empty string rather than dereferencing.
std::string first_token(const char* name) {
  if (name == nullptr) {
    return {};
  }
  const std::string_view view(name);
  const auto comma = view.find(',');
  return std::string(comma == std::string_view::npos ? view : view.substr(0, comma));
}

std::string averror_text(int rc) {
  char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
  av_strerror(rc, buf, sizeof(buf));
  return std::string(buf);
}

// Maps a libav failure code from either avformat_open_input or
// avformat_find_stream_info to mediadiff's own two-way error taxonomy
// (this plan's own read_first citations: core/snapshot.cpp's
// input_open-vs-input_unsupported distinction). AVERROR(ENOENT)/
// AVERROR(EACCES) mean "the bytes never opened at all" -- input_open.
// AVERROR_EXIT means the wall-clock budget fired -- input_unsupported,
// with a message naming the timeout explicitly (the word "budget" is
// what this plan's own acceptance criteria greps stderr for). Every
// other failure (AVERROR_INVALIDDATA and the rest) is "opened, or tried
// to, but is not something this parser understands" -- also
// input_unsupported.
Error map_probe_error(const std::string& path, int rc) {
  if (rc == AVERROR(ENOENT) || rc == AVERROR(EACCES)) {
    return Error{ErrorKind::input_open, "could not open input '" + path + "': " + averror_text(rc)};
  }
  if (rc == AVERROR_EXIT) {
    return Error{ErrorKind::input_unsupported,
                 "probing '" + path + "' exceeded its wall-clock budget (timeout)"};
  }
  return Error{ErrorKind::input_unsupported, "could not probe input '" + path + "': " + averror_text(rc)};
}

// 05-17-PLAN.md Task 1: the second-open half of DemuxSession::open's own
// context-open sequence, factored out so
// DemuxSession::reprobe_ts_declared_durations (Gap 2, TIME-02/TIME-03) can
// reuse it with `correct_ts_overflow` flipped, rather than duplicating
// alloc/interrupt-budget/diagnostics-accumulator/open/find_stream_info
// wiring a second time. `diagnostics` is attached exactly the way
// DemuxSession::open's own local `diagnostics` was attached before this
// refactor -- the caller decides whether that accumulator is the session's
// own (open()) or a throwaway one (reprobe_ts_declared_durations()), this
// helper does not care which.
struct OpenedContext {
  AVFormatContext* ctx = nullptr;
  std::unique_ptr<detail::InterruptState> interrupt_state;
};

// 06-18-PLAN.md (CR-05): the wall-clock interrupt budget bounds ONLY
// avformat_open_input + avformat_find_stream_info -- disarmed
// unconditionally afterward, for EVERY caller, no exceptions. Before this
// plan the bounded SBR probe decode below (06-04-PLAN.md) was the one
// caller that kept its own SECOND, throwaway AVFormatContext's budget
// ARMED through its own later av_read_frame/decode work -- on a loaded
// runner an interrupted read there turned a real answer into
// SbrSignaling::unknown, and the SAME file could report
// "(sbr: implicit)" on one run and "(sbr: unknown)" on the next: a P0
// determinism violation (06-REVIEW.md CR-05, checks.def's own "a check's
// value must never depend on which passes ran"). CR-05's fix: EVERY read
// after this open+find_stream_info window -- PacketScan's own sweep, and
// the bounded SBR probe's own packet loop below alike -- is bounded by a
// PACKET-COUNT limit instead of the wall clock (kMaxSbrProbeContainerPacketsScanned
// for the probe), so the same bytes always give the same answer regardless
// of host scheduling. A failure DURING this bounded open+find_stream_info
// window -- a genuine timeout included -- stays a hard Error
// (map_probe_error), propagated by every caller (DemuxSession::open and
// probe_implicit_sbr_via_second_open alike): CR-05's other half is that
// this Error is never turned into a value (e.g. `unknown`) further up the
// stack -- see probe_implicit_sbr_via_second_open's and
// resolve_sbr_signaling's own doc comments for the Error/nullopt
// classification that keeps that distinction honest.
mediadiff::expected<OpenedContext, Error> open_context(const std::string& utf8_path, const DemuxOptions& options,
                                                          bool correct_ts_overflow, ProbeDiagnostics& diagnostics) {
  AVFormatContext* ctx = avformat_alloc_context();
  if (ctx == nullptr) {
    return mediadiff::unexpected(
        Error{ErrorKind::internal, "could not allocate AVFormatContext for '" + utf8_path + "'"});
  }

  // Attaches this call's own diagnostics accumulator to the thread_local
  // slot probe_log_callback reads (Task 2) -- cleared unconditionally on
  // every exit path via this RAII guard, so the next open_context call on
  // the same thread always starts from a null pointer, never a stale one
  // left behind by a prior call's early return.
  set_current_probe_diagnostics(&diagnostics);
  struct DiagnosticsGuard {
    ~DiagnosticsGuard() { set_current_probe_diagnostics(nullptr); }
  } diagnostics_guard;

  // Installed BEFORE avformat_open_input (doc 02 section 1.1: the field
  // is documented "set by the user before avformat_open_input"; setting
  // it any later would leave the open call itself unbounded). Heap-owned
  // (not a stack-local temporary) -- see detail::InterruptState's own doc
  // comment in demux_session.h for why: ffmpeg's avio layer captures this
  // AVIOInterruptCB into its own URLContext at avio_open2() time,
  // independent of AVFormatContext::interrupt_callback from that point
  // forward, so the state it points at must remain valid for as long as
  // reads can happen on this session, not just for the duration of this
  // open() call.
  auto interrupt_state = std::make_unique<detail::InterruptState>(
      detail::InterruptState{std::chrono::steady_clock::now(), options.wall_clock_budget_ms});
  ctx->interrupt_callback.callback = &check_interrupt;
  ctx->interrupt_callback.opaque = interrupt_state.get();

  // avformat_open_input takes a narrow, UTF-8 path directly on every
  // platform mediadiff targets -- src/util/fs.h's own header comment
  // records that libavformat's file:// protocol already converts UTF-8 to
  // UTF-16 internally on Windows (file_open -> avpriv_open -> win32_open
  // -> get_extended_win32_path -> MultiByteToWideChar(CP_UTF8, ...) ->
  // _wsopen), so no wide-path shim is needed at this call site -- only
  // fopen_utf8's OWN file opens (util/fs.h) need one.
  //
  // Never touch ctx->flags -- AVFMT_FLAG_GENPTS stays unset, always (see
  // this file's own header comment for the full reasoning).
  //
  // 05-06-PLAN.md (TIME-02, Rule 1/2 gap closure): correct_ts_overflow is
  // a plain AVFormatContext int field (not part of ::flags) -- see
  // demux_session.h's own header comment for why DemuxSession::open
  // always clears it (correct_ts_overflow=false). 05-17-PLAN.md (Gap 2)
  // is the one and only caller that ever passes true: it deliberately
  // wants libavformat's OWN default wrap correction, on a second,
  // isolated context, precisely to recover the declared-duration values
  // the primary session's own correct_ts_overflow=0 leaves corrupted on a
  // genuinely-wrapping MPEG-TS file.
  ctx->correct_ts_overflow = correct_ts_overflow ? 1 : 0;
  int rc = avformat_open_input(&ctx, utf8_path.c_str(), nullptr, nullptr);
  if (rc < 0) {
    avformat_close_input(&ctx);
    return mediadiff::unexpected(map_probe_error(utf8_path, rc));
  }

  rc = avformat_find_stream_info(ctx, nullptr);
  if (rc < 0) {
    avformat_close_input(&ctx);
    return mediadiff::unexpected(map_probe_error(utf8_path, rc));
  }

  // Rule 1 fix (03-03-PLAN.md Task 1, discovered via AddressSanitizer
  // stack-use-after-return): this budget was only ever documented to
  // bound avformat_open_input + avformat_find_stream_info (this file's
  // own header comment) -- disarm it here so a LATER libav call on this
  // same AVFormatContext (av_read_frame's own internal
  // ff_check_interrupt calls, among others -- PacketScan, 03-03-PLAN.md
  // Task 1, is the first caller that ever makes one) never aborts a
  // mid-sweep read on a stale "budget already exceeded" reading. Earlier
  // attempt (nulling ctx->interrupt_callback itself) was INSUFFICIENT and
  // is exactly what ASan caught: ffmpeg's avio layer copies the
  // AVIOInterruptCB into its own URLContext back at avio_open2() time
  // (inside avformat_open_input), independent of
  // AVFormatContext::interrupt_callback from that point on -- so clearing
  // ctx's own field here never reaches the io layer's already-captured
  // copy. The correct disarm mutates the SAME InterruptState object that
  // copy still points at, so it keeps pointing at valid memory for the
  // rest of this session's lifetime; setting budget_ms to the largest
  // representable value makes every future `elapsed_ms >= budget_ms`
  // check false forever, i.e. "never interrupt again" without ever
  // reading freed/dangling memory.
  //
  // 06-18-PLAN.md (CR-05): unconditional now for EVERY caller, including
  // the bounded SBR probe's own second, throwaway open below -- see this
  // function's own top comment for why leaving the probe's copy armed was
  // a determinism bug, not a feature.
  interrupt_state->budget_ms = std::numeric_limits<std::int64_t>::max();

  OpenedContext result;
  result.ctx = ctx;
  result.interrupt_state = std::move(interrupt_state);
  return result;
}

// 06-18-PLAN.md (CR-05): the ONLY bound on the SBR probe below now that its
// wall-clock budget is disarmed the instant its own second open completes
// (see open_context's own top comment) -- the maximum number of container
// packets this probe will read looking for the TARGET stream's own first
// packet before giving up as `ok(nullopt)`. A hostile file with the
// ambiguous audio stream positioned deep behind many packets of other
// streams (or none at all matching) cannot make this probe scan
// indefinitely -- this bound, not the wall clock, is what makes the
// probe's reads deterministic: the same bytes always produce the same
// number of packets read and therefore the same answer, regardless of host
// scheduling (CR-05). This is the SAME stance PacketScan's own post-open
// reads already take (03-03-PLAN.md's own disarm) -- the probe stays
// consistent with the rest of this file rather than being a special case.
constexpr int kMaxSbrProbeContainerPacketsScanned = 64;

}  // namespace

namespace detail {

// 06-04-PLAN.md (AUDIO-03, D-12) / 06-18-PLAN.md (CR-05): the real
// SbrProbeFn implementation -- the ONLY place this project opens a second,
// throwaway AVFormatContext purely to resolve implicit SBR signaling.
// 06-13-PLAN.md: this is D-12's FALLBACK, not its primary mechanism (see
// compute_sbr_signaling below), and is reached only when
// avformat_find_stream_info resolved no profile for the stream at all.
// Moved out of the anonymous namespace and declared in demux_session.h as
// an exposed test seam (06-18-PLAN.md) so
// tests/unit/test_audio_config.cpp can drive it directly against a real
// fixture and assert its own Error-vs-nullopt classification (A1) without
// going through resolve_sbr_signaling's injected SbrProbeFn wrapper.
//
// A KNOWN limit, recorded rather than papered over (.planning/WINDOWS.md):
// its cost is a whole extra container open, unconstrained by either bound
// below; and kMaxSbrProbePackets == 1 cannot yield a frame from an
// encoder-primed AAC stream, whose first packet is consumed entirely as
// encoder-delay priming -- such a stream resolves to `unknown` (via
// ok(nullopt), honest: nothing was decoded), a fallback that cannot help
// the case it would most often be asked about. Mirrors
// DemuxSession::reprobe_ts_declared_durations' own isolation precedent (a
// second, independent open+close against the SAME path; this session's own
// ctx_/read position is never touched) and src/probe/audio_decode.cpp's
// own decoder-open/send/receive block (AV_CODEC_FLAG_BITEXACT,
// expected-mapped libav errors). Sends AT MOST kMaxSbrProbePackets (1)
// packet to the decoder and receives at most one frame from it -- the loop
// below reads container packets looking for the target stream's first one
// (bounded by kMaxSbrProbeContainerPacketsScanned above), but stops
// immediately, via `break`, the instant it has fed the decoder its one
// allowed packet, whether or not that packet produced a frame.
//
// 06-18-PLAN.md (CR-05, flagged assumption A1) -- the Error-vs-nullopt
// classification a caller (resolve_sbr_signaling) depends on:
//   - Error: the second container open or find_stream_info failure,
//     a timeout included (map_probe_error, via `opened`); the target
//     stream index absent on this second open; any allocation failure
//     (AVCodecContext, AVPacket, or AVFrame). These depend on the
//     environment (a genuine resource exhaustion) or on the file changing
//     shape between the two opens -- never a deterministic property of
//     these exact bytes alone.
//   - ok(nullopt): no decoder found for this codec_id; a
//     parameter-copy (avcodec_parameters_to_context) or avcodec_open2
//     failure; no target packet found within
//     kMaxSbrProbeContainerPacketsScanned packets; a send failure; or no
//     frame received. Every one of these is deterministic for the same
//     bytes and the same build -- the probe genuinely ran and genuinely
//     found nothing, which resolve_sbr_signaling maps to
//     SbrSignaling::unknown, never a guess.
mediadiff::expected<std::optional<SbrProbeDecodeResult>, Error> probe_implicit_sbr_via_second_open(
    const std::string& utf8_path, int target_stream_index) {
  ProbeDiagnostics throwaway_diagnostics;
  auto opened = open_context(utf8_path, DemuxOptions{}, /*correct_ts_overflow=*/false, throwaway_diagnostics);
  if (!opened) {
    return mediadiff::unexpected(opened.error());
  }
  AVFormatContext* probe_ctx = opened->ctx;
  struct CtxCloser {
    AVFormatContext* ctx;
    ~CtxCloser() { avformat_close_input(&ctx); }
  } ctx_closer{probe_ctx};

  if (target_stream_index < 0 || static_cast<unsigned>(target_stream_index) >= probe_ctx->nb_streams) {
    return mediadiff::unexpected(Error{ErrorKind::internal, "SBR probe: stream index out of range on second open"});
  }
  const AVStream* stream = probe_ctx->streams[target_stream_index];
  const AVCodecParameters* codecpar = stream->codecpar;
  const std::int64_t declared_rate = codecpar->sample_rate > 0 ? codecpar->sample_rate : 0;

  const AVCodec* decoder = avcodec_find_decoder(codecpar->codec_id);
  if (decoder == nullptr) {
    // A1: deterministic for this stream's own codec_id -- ok(nullopt), not
    // an Error.
    return std::optional<SbrProbeDecodeResult>{};
  }
  AVCodecContext* codec_ctx = avcodec_alloc_context3(decoder);
  if (codec_ctx == nullptr) {
    return mediadiff::unexpected(Error{ErrorKind::internal, "SBR probe: could not allocate AVCodecContext"});
  }
  struct CodecCtxFree {
    AVCodecContext* ctx;
    ~CodecCtxFree() { avcodec_free_context(&ctx); }
  } codec_closer{codec_ctx};

  if (avcodec_parameters_to_context(codec_ctx, codecpar) < 0) {
    // A1: deterministic for this stream's own codecpar -- ok(nullopt).
    return std::optional<SbrProbeDecodeResult>{};
  }
  // Same bitexact discipline as audio_decode.cpp's own decode sweep --
  // this probe's output must be reproducible across runs/machines.
  codec_ctx->flags |= AV_CODEC_FLAG_BITEXACT;
  if (avcodec_open2(codec_ctx, decoder, nullptr) < 0) {
    // A1: deterministic for this stream -- ok(nullopt).
    return std::optional<SbrProbeDecodeResult>{};
  }

  AVPacket* pkt = av_packet_alloc();
  if (pkt == nullptr) {
    return mediadiff::unexpected(Error{ErrorKind::internal, "SBR probe: av_packet_alloc failed"});
  }
  struct PktFree {
    AVPacket* p;
    ~PktFree() { av_packet_free(&p); }
  } pkt_free{pkt};

  std::optional<SbrProbeDecodeResult> result;
  for (int packets_scanned = 0; packets_scanned < kMaxSbrProbeContainerPacketsScanned; ++packets_scanned) {
    if (av_read_frame(probe_ctx, pkt) < 0) {
      // A1: no target packet found within the packet-count bound --
      // deterministic, ok(nullopt) via the fall-through below.
      break;
    }
    if (pkt->stream_index != target_stream_index) {
      av_packet_unref(pkt);
      continue;
    }

    const int send_rc = avcodec_send_packet(codec_ctx, pkt);
    av_packet_unref(pkt);
    if (send_rc < 0 && send_rc != AVERROR(EAGAIN)) {
      // A1: a send failure is deterministic for this packet's own bytes --
      // ok(nullopt) via the fall-through below.
      break;
    }

    AVFrame* frame = av_frame_alloc();
    if (frame == nullptr) {
      // A1: an allocation failure IS an Error -- unlike every other
      // outcome in this loop, this one is not a deterministic property of
      // the file's own bytes.
      return mediadiff::unexpected(Error{ErrorKind::internal, "SBR probe: av_frame_alloc failed"});
    }
    if (avcodec_receive_frame(codec_ctx, frame) >= 0) {
      SbrProbeDecodeResult r;
      r.declared_sample_rate_hz = declared_rate;
      r.decoded_sample_rate_hz = frame->sample_rate > 0 ? frame->sample_rate : 0;
      r.he_profile =
          codec_ctx->profile == AV_PROFILE_AAC_HE || codec_ctx->profile == AV_PROFILE_AAC_HE_V2;
      result = r;
    }
    av_frame_free(&frame);
    // kMaxSbrProbePackets (1): exactly one packet is ever sent to the
    // decoder, whether or not it produced a frame.
    break;
  }

  // A1: "no target packet found" and "a send failure or no frame decoded"
  // both fall through to here as ok(nullopt) -- deterministic for the same
  // bytes, never an Error.
  return result;
}

}  // namespace detail

std::int64_t default_wall_clock_budget_ms() { return g_default_wall_clock_budget_ms.load(std::memory_order_relaxed); }

void set_default_wall_clock_budget_ms(std::int64_t ms) {
  g_default_wall_clock_budget_ms.store(ms, std::memory_order_relaxed);
}

void set_current_probe_diagnostics(ProbeDiagnostics* diagnostics) { g_current_diagnostics = diagnostics; }

void probe_log_callback(void* /*avcl*/, int level, const char* /*fmt*/, va_list /*args*/) {
  // AV_LOG_WARNING and above, numerically -- libav's own convention is
  // that a LOWER number is MORE severe (AV_LOG_ERROR=16 < AV_LOG_WARNING=24
  // < AV_LOG_INFO=32), so "WARNING and above" means level <= AV_LOG_WARNING.
  if (level > AV_LOG_WARNING) {
    return;
  }
  if (g_current_diagnostics == nullptr) {
    // No DemuxSession::open call is active on this thread right now --
    // drop the line. This is also what keeps a unit test that calls
    // av_log() with no accumulator attached safe (probe_log_callback's own
    // documented contract).
    return;
  }
  g_current_diagnostics->warning_count++;
}

// AVMediaType -> StreamMediaType (03-04-PLAN.md Task 1). Every enumerator
// this project's own StreamMediaType declares maps directly; anything else
// (AVMEDIA_TYPE_UNKNOWN, AVMEDIA_TYPE_NB, or a future libav addition) folds
// into `other`, which container.track_count's own histogram construction
// (topology.cpp) further folds into the `data` bin -- doc 02's table has no
// sixth bin.
StreamMediaType stream_media_type(enum AVMediaType type) {
  switch (type) {
    case AVMEDIA_TYPE_VIDEO:
      return StreamMediaType::video;
    case AVMEDIA_TYPE_AUDIO:
      return StreamMediaType::audio;
    case AVMEDIA_TYPE_SUBTITLE:
      return StreamMediaType::subtitle;
    case AVMEDIA_TYPE_DATA:
      return StreamMediaType::data;
    case AVMEDIA_TYPE_ATTACHMENT:
      return StreamMediaType::attachment;
    default:
      return StreamMediaType::other;
  }
}

// Every key/value pair in `dict`, in AVDictionary iteration order --
// shared by container_tags()/stream_tags() below. av_dict_iterate (not the
// deprecated av_dict_get(..., AV_DICT_IGNORE_SUFFIX) loop) is this pinned
// FFmpeg's current iteration API.
std::vector<std::pair<std::string, std::string>> dict_to_pairs(const AVDictionary* dict) {
  std::vector<std::pair<std::string, std::string>> pairs;
  const AVDictionaryEntry* entry = nullptr;
  while ((entry = av_dict_iterate(dict, entry)) != nullptr) {
    pairs.emplace_back(std::string(entry->key != nullptr ? entry->key : ""),
                        std::string(entry->value != nullptr ? entry->value : ""));
  }
  return pairs;
}

// 03-06-PLAN.md Task 3 (CONT-02): delegates to core/container_family.h's
// container_family_token -- the SAME function compare/engine.cpp's
// cross-container demotion calls -- rather than re-declaring this mapping
// locally, so the probe layer's own scoping and the engine's demotion can
// never disagree about what family a file belongs to (this plan's own
// key_link). `format_name()` already returns the first comma-delimited
// token (this file's own extraction rule, above); container_family_token
// accepts that equally well as the raw, untruncated AVInputFormat::name.
ContainerFamily container_family_from_format_name(std::string_view format_name) {
  const std::string_view token = container_family_token(format_name);
  if (token == "mp4") {
    return ContainerFamily::mp4;
  }
  if (token == "mkv") {
    return ContainerFamily::mkv;
  }
  if (token == "ts") {
    return ContainerFamily::ts;
  }
  return ContainerFamily::other;
}

DemuxSession::DemuxSession(AVFormatContext* ctx, std::string format_name,
                             std::unique_ptr<detail::InterruptState> interrupt_state)
    : ctx_(ctx), format_name_(std::move(format_name)), interrupt_state_(std::move(interrupt_state)) {}

DemuxSession::DemuxSession(DemuxSession&& other) noexcept
    : ctx_(other.ctx_),
      format_name_(std::move(other.format_name_)),
      diagnostics_(other.diagnostics_),
      interrupt_state_(std::move(other.interrupt_state_)),
      declared_duration_source_(other.declared_duration_source_),
      reprobed_container_duration_ticks_(other.reprobed_container_duration_ticks_),
      reprobed_stream_duration_ticks_(std::move(other.reprobed_stream_duration_ticks_)),
      sbr_signaling_(std::move(other.sbr_signaling_)),
      implicit_probe_rate_hz_(std::move(other.implicit_probe_rate_hz_)) {
  other.ctx_ = nullptr;
}

DemuxSession& DemuxSession::operator=(DemuxSession&& other) noexcept {
  if (this != &other) {
    if (ctx_ != nullptr) {
      avformat_close_input(&ctx_);
    }
    ctx_ = other.ctx_;
    format_name_ = std::move(other.format_name_);
    diagnostics_ = other.diagnostics_;
    interrupt_state_ = std::move(other.interrupt_state_);
    declared_duration_source_ = other.declared_duration_source_;
    reprobed_container_duration_ticks_ = other.reprobed_container_duration_ticks_;
    reprobed_stream_duration_ticks_ = std::move(other.reprobed_stream_duration_ticks_);
    sbr_signaling_ = std::move(other.sbr_signaling_);
    implicit_probe_rate_hz_ = std::move(other.implicit_probe_rate_hz_);
    other.ctx_ = nullptr;
  }
  return *this;
}

DemuxSession::~DemuxSession() {
  if (ctx_ != nullptr) {
    avformat_close_input(&ctx_);
  }
}

mediadiff::expected<DemuxSession, Error> DemuxSession::open(const std::string& utf8_path,
                                                              const DemuxOptions& options) {
  // 05-17-PLAN.md Task 1: the alloc/interrupt-budget/diagnostics-
  // accumulator/open/find_stream_info sequence itself now lives in
  // open_context (this file's own anonymous namespace, above) --
  // `correct_ts_overflow=false` here is byte-for-byte the same behavior
  // this function had before the refactor (05-06-PLAN.md's own
  // correct_ts_overflow=0 fix, still the ONLY value the primary session
  // ever opens with).
  ProbeDiagnostics diagnostics;
  auto opened = open_context(utf8_path, options, /*correct_ts_overflow=*/false, diagnostics);
  if (!opened) {
    return mediadiff::unexpected(opened.error());
  }

  std::string fmt_name = first_token(opened->ctx->iformat != nullptr ? opened->ctx->iformat->name : nullptr);
  DemuxSession session(opened->ctx, std::move(fmt_name), std::move(opened->interrupt_state));
  session.diagnostics_ = diagnostics;
  // 06-04-PLAN.md (AUDIO-03, D-12): resolved ONCE here, in the header
  // pass, before this session is ever handed to PacketScan or any
  // analyzer -- see StreamInfo::sbr_signaling's own doc comment for why
  // this must not depend on which later passes run. 06-18-PLAN.md (CR-05):
  // an Error here -- the bounded fallback probe's own second open failing
  // or timing out -- now propagates out of open() as a hard failure of the
  // whole command, rather than being silently rendered as
  // SbrSignaling::unknown further downstream.
  auto sbr_result = session.compute_sbr_signaling(utf8_path);
  if (!sbr_result) {
    return mediadiff::unexpected(sbr_result.error());
  }
  return session;
}

// 05-17-PLAN.md Task 1 (Gap 2, TIME-02/TIME-03): see this method's own doc
// comment in demux_session.h.
void DemuxSession::reprobe_ts_declared_durations(const std::string& utf8_path) {
  std::vector<detail::StreamLayoutKey> primary_layout;
  if (ctx_ != nullptr) {
    primary_layout.reserve(ctx_->nb_streams);
    for (unsigned i = 0; i < ctx_->nb_streams; ++i) {
      const AVStream* stream = ctx_->streams[i];
      primary_layout.push_back(detail::StreamLayoutKey{static_cast<int>(stream->codecpar->codec_type), stream->id});
    }
  }

  // A throwaway accumulator -- this session's OWN warning_count() (already
  // captured at primary-open time) must never move because of this second,
  // isolated open.
  ProbeDiagnostics throwaway_diagnostics;
  auto opened = open_context(utf8_path, DemuxOptions{}, /*correct_ts_overflow=*/true, throwaway_diagnostics);
  if (!opened) {
    declared_duration_source_ = DeclaredDurationSource::withheld_wrap_uncorrectable;
    reprobed_container_duration_ticks_.reset();
    reprobed_stream_duration_ticks_.clear();
    return;
  }

  AVFormatContext* reprobe_ctx = opened->ctx;

  std::vector<detail::StreamLayoutKey> reprobe_layout;
  reprobe_layout.reserve(reprobe_ctx->nb_streams);
  for (unsigned i = 0; i < reprobe_ctx->nb_streams; ++i) {
    const AVStream* stream = reprobe_ctx->streams[i];
    reprobe_layout.push_back(detail::StreamLayoutKey{static_cast<int>(stream->codecpar->codec_type), stream->id});
  }

  if (!detail::stream_layouts_match(primary_layout, reprobe_layout)) {
    // T-05-75: the second open saw a different program map -- the
    // reprobed durations cannot be attributed to the primary session's
    // own streams by index. Withheld, never compared corrupt.
    avformat_close_input(&reprobe_ctx);
    declared_duration_source_ = DeclaredDurationSource::withheld_wrap_uncorrectable;
    reprobed_container_duration_ticks_.reset();
    reprobed_stream_duration_ticks_.clear();
    return;
  }

  // Duration fields only (this method's own prohibition) -- never
  // start_time, which sits on libavformat's own shifted epoch once its
  // default wrap correction has run.
  reprobed_container_duration_ticks_ =
      reprobe_ctx->duration != AV_NOPTS_VALUE ? std::optional<std::int64_t>(reprobe_ctx->duration) : std::nullopt;

  reprobed_stream_duration_ticks_.clear();
  reprobed_stream_duration_ticks_.reserve(reprobe_ctx->nb_streams);
  for (unsigned i = 0; i < reprobe_ctx->nb_streams; ++i) {
    const AVStream* stream = reprobe_ctx->streams[i];
    reprobed_stream_duration_ticks_.push_back(
        stream->duration != AV_NOPTS_VALUE ? std::optional<std::int64_t>(stream->duration) : std::nullopt);
  }

  avformat_close_input(&reprobe_ctx);
  declared_duration_source_ = DeclaredDurationSource::overflow_corrected_reprobe;
}

DeclaredDurationSource DemuxSession::declared_duration_source() const { return declared_duration_source_; }

std::string_view DemuxSession::format_name() const { return format_name_; }

int DemuxSession::stream_count() const { return ctx_ != nullptr ? static_cast<int>(ctx_->nb_streams) : 0; }

StreamInfo DemuxSession::stream_info(int index) const {
  if (ctx_ == nullptr || index < 0 || static_cast<unsigned>(index) >= ctx_->nb_streams) {
    return StreamInfo{};
  }
  const AVStream* stream = ctx_->streams[index];
  const AVCodecParameters* codecpar = stream->codecpar;

  StreamInfo info;
  info.media_type = stream_media_type(codecpar->codec_type);
  info.codec_name = avcodec_get_name(codecpar->codec_id);
  // 05-07-PLAN.md (TIME-02/TIME-04): AVStream::id verbatim -- see
  // StreamInfo::stream_id's own comment for why this crosses the boundary
  // (the ts_scan PID join).
  info.stream_id = stream->id;
  // MKTAG('t','m','c','d') -- the MOV/MP4 timecode-track four-character
  // code (confirmed against libavformat/mov.c's own mov_read_tmcd
  // dispatch table, keyed on this exact codec_tag).
  info.is_timecode = codecpar->codec_tag == MKTAG('t', 'm', 'c', 'd');
  info.is_caption = codecpar->codec_id == AV_CODEC_ID_EIA_608;

  // 05-11-PLAN.md (TIME-11): AVStream::metadata["timecode"], resolved here
  // -- the ONE place this project reads this key, same per-field boundary
  // as every other value above. Populated by the MOV/MP4 demuxer during
  // avformat_find_stream_info itself (05-RESEARCH.md Pattern 4), never by
  // a decode call.
  const AVDictionaryEntry* timecode_entry = av_dict_get(stream->metadata, "timecode", nullptr, 0);
  if (timecode_entry != nullptr && timecode_entry->value != nullptr) {
    info.timecode_metadata = timecode_entry->value;
  }

  // 04-06-PLAN.md (VIDEO-01/02): resolved here, never past this file's own
  // opaque-AVFormatContext boundary -- see StreamInfo's own comment.
  info.codec_id_raw = static_cast<std::int64_t>(codecpar->codec_id);
  info.codec_tag_raw = static_cast<std::int64_t>(codecpar->codec_tag);
  info.profile = codecpar->profile;
  const char* profile_name = avcodec_profile_name(codecpar->codec_id, codecpar->profile);
  if (profile_name != nullptr) {
    info.profile_name = profile_name;
  }
  info.level = codecpar->level;
  info.width = codecpar->width;
  info.height = codecpar->height;
  info.declared_frame_count = static_cast<std::int64_t>(stream->nb_frames);

  // 05-04-PLAN.md (TIME-01/TIME-03): the stream-declared member of
  // timeline.duration's triple -- AV_NOPTS_VALUE (never coerced to 0)
  // stays std::nullopt. 05-17-PLAN.md (Gap 2, TIME-02/TIME-03): overridden
  // by reprobe_ts_declared_durations()'s own stored value whenever this
  // session's declared_duration_source() is overflow_corrected_reprobe
  // (the primary session's own value is wrap-corrupted on a
  // genuinely-wrapping TS file, correct_ts_overflow=0 -- this file's own
  // header comment), or withheld entirely (stays nullopt) when
  // withheld_wrap_uncorrectable. `demuxer` (the default) reads
  // stream->duration verbatim, exactly as before this plan.
  if (declared_duration_source_ == DeclaredDurationSource::overflow_corrected_reprobe) {
    if (static_cast<std::size_t>(index) < reprobed_stream_duration_ticks_.size()) {
      info.declared_duration_ticks = reprobed_stream_duration_ticks_[static_cast<std::size_t>(index)];
    }
  } else if (declared_duration_source_ == DeclaredDurationSource::demuxer && stream->duration != AV_NOPTS_VALUE) {
    info.declared_duration_ticks = stream->duration;
  }

  // 04-07-PLAN.md (VIDEO-01): resolved here, same boundary as every other
  // codecpar/AVStream field above -- never past this file's own
  // opaque-AVFormatContext boundary.
  info.avg_frame_rate_num = stream->avg_frame_rate.num;
  info.avg_frame_rate_den = stream->avg_frame_rate.den;
  info.r_frame_rate_num = stream->r_frame_rate.num;
  info.r_frame_rate_den = stream->r_frame_rate.den;

  // 04-07-PLAN.md (VIDEO-04): the SAME per-field boundary as above --
  // container-level from AVStream, bitstream-level from codecpar, both
  // verbatim (including a 0 numerator).
  info.sar_container_num = stream->sample_aspect_ratio.num;
  info.sar_container_den = stream->sample_aspect_ratio.den;
  info.sar_bitstream_num = codecpar->sample_aspect_ratio.num;
  info.sar_bitstream_den = codecpar->sample_aspect_ratio.den;

  // 04-08-PLAN.md (VIDEO-03/VIDEO-07/VIDEO-08): resolved here, same
  // per-field boundary as every other codecpar value above -- never past
  // this file's own opaque-AVFormatContext boundary. `codecpar->format` is
  // an AVPixelFormat for a video stream (the only stream kind this check
  // family ever scopes to).
  info.pix_fmt_raw = static_cast<std::int64_t>(codecpar->format);
  const char* pix_fmt_name = av_get_pix_fmt_name(static_cast<AVPixelFormat>(codecpar->format));
  if (pix_fmt_name != nullptr) {
    info.pix_fmt_name = pix_fmt_name;
  }
  info.color_range_raw = static_cast<std::int64_t>(codecpar->color_range);
  const char* color_range_name = av_color_range_name(codecpar->color_range);
  if (color_range_name != nullptr) {
    info.color_range_name = color_range_name;
  }
  info.color_primaries_raw = static_cast<std::int64_t>(codecpar->color_primaries);
  const char* color_primaries_name = av_color_primaries_name(codecpar->color_primaries);
  if (color_primaries_name != nullptr) {
    info.color_primaries_name = color_primaries_name;
  }
  info.color_transfer_raw = static_cast<std::int64_t>(codecpar->color_trc);
  const char* color_transfer_name = av_color_transfer_name(codecpar->color_trc);
  if (color_transfer_name != nullptr) {
    info.color_transfer_name = color_transfer_name;
  }
  info.color_matrix_raw = static_cast<std::int64_t>(codecpar->color_space);
  const char* color_matrix_name = av_color_space_name(codecpar->color_space);
  if (color_matrix_name != nullptr) {
    info.color_matrix_name = color_matrix_name;
  }
  info.chroma_location_raw = static_cast<std::int64_t>(codecpar->chroma_location);
  const char* chroma_location_name = av_chroma_location_name(codecpar->chroma_location);
  if (chroma_location_name != nullptr) {
    info.chroma_location_name = chroma_location_name;
  }

  // 04-10-PLAN.md (VIDEO-06): the raw AVFieldOrder ordinal, same
  // per-field boundary as every other codecpar value above -- resolved to
  // a name only at the src/analyzers/video/interlace.cpp edge (no
  // av_field_order_name exists to call here).
  info.field_order_raw = static_cast<std::int64_t>(codecpar->field_order);

  // 04-11-PLAN.md (VIDEO-09): codecpar->coded_side_data, resolved here --
  // the ONLY place this project reads an AVPacketSideData/
  // AVMasteringDisplayMetadata/AVContentLightMetadata pointer;
  // src/analyzers/video/hdr.cpp only ever sees the plain StreamInfo
  // fields above. Populated at DEMUX time (D-08/D-09), never by a decode
  // pass.
  const AVPacketSideData* mdcv_side_data = av_packet_side_data_get(
      codecpar->coded_side_data, codecpar->nb_coded_side_data, AV_PKT_DATA_MASTERING_DISPLAY_METADATA);
  if (mdcv_side_data != nullptr) {
    // T-4-48: a payload whose reported size is smaller than the struct it
    // would be read as is never read past its end -- treated as though
    // the entry were absent, with the short-payload observation recorded
    // so it is visible rather than silent.
    if (mdcv_side_data->size < sizeof(AVMasteringDisplayMetadata)) {
      info.mdcv_short_payload = true;
    } else {
      const auto* mdcv = reinterpret_cast<const AVMasteringDisplayMetadata*>(mdcv_side_data->data);
      info.mdcv_present = true;
      info.mdcv_has_primaries = mdcv->has_primaries != 0;
      info.mdcv_has_luminance = mdcv->has_luminance != 0;
      info.mdcv_r_x_num = mdcv->display_primaries[0][0].num;
      info.mdcv_r_x_den = mdcv->display_primaries[0][0].den;
      info.mdcv_r_y_num = mdcv->display_primaries[0][1].num;
      info.mdcv_r_y_den = mdcv->display_primaries[0][1].den;
      info.mdcv_g_x_num = mdcv->display_primaries[1][0].num;
      info.mdcv_g_x_den = mdcv->display_primaries[1][0].den;
      info.mdcv_g_y_num = mdcv->display_primaries[1][1].num;
      info.mdcv_g_y_den = mdcv->display_primaries[1][1].den;
      info.mdcv_b_x_num = mdcv->display_primaries[2][0].num;
      info.mdcv_b_x_den = mdcv->display_primaries[2][0].den;
      info.mdcv_b_y_num = mdcv->display_primaries[2][1].num;
      info.mdcv_b_y_den = mdcv->display_primaries[2][1].den;
      info.mdcv_wp_x_num = mdcv->white_point[0].num;
      info.mdcv_wp_x_den = mdcv->white_point[0].den;
      info.mdcv_wp_y_num = mdcv->white_point[1].num;
      info.mdcv_wp_y_den = mdcv->white_point[1].den;
      info.mdcv_min_luminance_num = mdcv->min_luminance.num;
      info.mdcv_min_luminance_den = mdcv->min_luminance.den;
      info.mdcv_max_luminance_num = mdcv->max_luminance.num;
      info.mdcv_max_luminance_den = mdcv->max_luminance.den;
    }
  }

  const AVPacketSideData* cll_side_data = av_packet_side_data_get(
      codecpar->coded_side_data, codecpar->nb_coded_side_data, AV_PKT_DATA_CONTENT_LIGHT_LEVEL);
  if (cll_side_data != nullptr) {
    if (cll_side_data->size < sizeof(AVContentLightMetadata)) {
      info.cll_short_payload = true;
    } else {
      const auto* cll = reinterpret_cast<const AVContentLightMetadata*>(cll_side_data->data);
      info.cll_present = true;
      info.cll_max_cll = static_cast<std::int64_t>(cll->MaxCLL);
      info.cll_max_fall = static_cast<std::int64_t>(cll->MaxFALL);
    }
  }

  // 04-12-PLAN.md (VIDEO-09's third HDR family): the Dolby Vision
  // configuration record, same per-field boundary and short-payload
  // discipline (T-4-53, mirroring T-4-48) as mdcv/cll above.
  const AVPacketSideData* dovi_side_data =
      av_packet_side_data_get(codecpar->coded_side_data, codecpar->nb_coded_side_data, AV_PKT_DATA_DOVI_CONF);
  if (dovi_side_data != nullptr) {
    if (dovi_side_data->size < sizeof(AVDOVIDecoderConfigurationRecord)) {
      info.dovi_short_payload = true;
    } else {
      const auto* dovi = reinterpret_cast<const AVDOVIDecoderConfigurationRecord*>(dovi_side_data->data);
      info.dovi_present = true;
      info.dovi_version_major = static_cast<std::int64_t>(dovi->dv_version_major);
      info.dovi_version_minor = static_cast<std::int64_t>(dovi->dv_version_minor);
      info.dovi_profile = static_cast<std::int64_t>(dovi->dv_profile);
      info.dovi_level = static_cast<std::int64_t>(dovi->dv_level);
      info.dovi_rpu_present = dovi->rpu_present_flag != 0;
      info.dovi_el_present = dovi->el_present_flag != 0;
      info.dovi_bl_present = dovi->bl_present_flag != 0;
      info.dovi_bl_signal_compatibility_id = static_cast<std::int64_t>(dovi->dv_bl_signal_compatibility_id);
      info.dovi_md_compression = static_cast<std::int64_t>(dovi->dv_md_compression);
    }
  }

  // 05-14-PLAN.md (Gap 3, TIME-06): codecpar->sample_rate, audio streams
  // only, and only when positive -- see StreamInfo::sample_rate's own
  // comment for why this crosses the boundary (the priming sample-to-
  // ticks conversion in src/analyzers/timeline/av_sync.cpp).
  if (info.media_type == StreamMediaType::audio && codecpar->sample_rate > 0) {
    info.sample_rate = codecpar->sample_rate;
  }

  // 06-03-PLAN.md (AUDIO-01): the remaining audio-only codecpar fields,
  // same per-field boundary as every other value above -- src/analyzers/
  // audio/stream_params.cpp never sees an AVSampleFormat/AVChannelLayout
  // value, only these plain fields.
  if (info.media_type == StreamMediaType::audio) {
    const auto native_fmt = static_cast<AVSampleFormat>(codecpar->format);
    info.sample_fmt_raw = static_cast<std::int64_t>(native_fmt);
    // D-02 (06-CONTEXT.md): resolved to the PACKED-equivalent spelling here
    // -- mirrors probe/audio_decode.cpp's identical canonicalisation of a
    // decoded frame's own native format -- so `fltp` and `flt` record
    // identically and a planar/packed difference alone is never reported.
    const AVSampleFormat packed_fmt = av_get_alt_sample_fmt(native_fmt, /*planar=*/0);
    const AVSampleFormat name_fmt = packed_fmt != AV_SAMPLE_FMT_NONE ? packed_fmt : native_fmt;
    const char* sample_fmt_name = av_get_sample_fmt_name(name_fmt);
    if (sample_fmt_name != nullptr) {
      info.sample_fmt_name = sample_fmt_name;
    }

    // Never coerced to the sample format's container width -- codecpar->
    // bits_per_raw_sample is a real reported 0 when the codec declares
    // none, kept as std::nullopt rather than fabricated.
    if (codecpar->bits_per_raw_sample > 0) {
      info.bits_per_raw_sample = static_cast<std::int64_t>(codecpar->bits_per_raw_sample);
    }

    info.channels = static_cast<std::int64_t>(codecpar->ch_layout.nb_channels);

    // AVChannelLayout only -- never the legacy uint64_t channel_layout
    // mask, which is absent from these linked headers entirely. Mirrors
    // probe/audio_decode.cpp's identical av_channel_layout_describe call
    // on a decoded frame's own ch_layout.
    char layout_buf[64] = {0};
    const int layout_len = av_channel_layout_describe(&codecpar->ch_layout, layout_buf, sizeof(layout_buf));
    if (layout_len > 0) {
      info.channel_layout = layout_buf;
    }

    // 06-04-PLAN.md (AUDIO-03, D-12): read from the cache compute_sbr_signaling()
    // populated once in open() -- never re-derived here, so the value is
    // pass-independent (StreamInfo::sbr_signaling's own doc comment).
    if (static_cast<std::size_t>(index) < sbr_signaling_.size()) {
      info.sbr_signaling = sbr_signaling_[static_cast<std::size_t>(index)];
    }
  }

  // 06-04-PLAN.md (AUDIO-03, D-12): effective (SBR-decoded) rate. NOT a
  // formulaic doubling -- confirmed empirically (audio_sbr_implicit.mp4,
  // this project's own hand-written fixture) that
  // `avformat_find_stream_info()`'s own internal probing can ALREADY
  // resolve the doubled rate into `codecpar->sample_rate` for a short
  // enough stream (both WITH and WITHOUT `--content`, since this happens
  // during the header pass itself, before any decode-pass distinction
  // exists) -- doubling an already-doubled value would fabricate a false,
  // quadrupled rate, exactly the P0 class this project exists to prevent.
  // For an `implicit_decoded` stream that D-12's FALLBACK probe resolved,
  // this reads that probe's OWN directly-observed decoded rate
  // (implicit_probe_rate_hz_, cached by compute_sbr_signaling) instead,
  // which is correct whether or not `codecpar` already reflects the
  // doubling. 06-13-PLAN.md: a stream resolved by D-12's PRIMARY
  // header-pass mechanism leaves that cache at 0 and keeps
  // `codecpar->sample_rate` -- which, for that path, is the doubled rate
  // BY CONSTRUCTION, since noticing the doubling is how the primary
  // mechanism identified implicit SBR in the first place. For `explicit_asc`,
  // `codecpar->sample_rate` is set to the ALREADY-DOUBLED
  // `ext_sample_rate` directly by the MP4 demuxer (06-RESEARCH.md Q4,
  // isom.c), so it is used unchanged.
  const std::int64_t core_rate = info.sample_rate.value_or(0);
  std::int64_t effective_rate = core_rate;
  if (info.sbr_signaling == SbrSignaling::implicit_decoded &&
      static_cast<std::size_t>(index) < implicit_probe_rate_hz_.size() &&
      implicit_probe_rate_hz_[static_cast<std::size_t>(index)] > 0) {
    effective_rate = implicit_probe_rate_hz_[static_cast<std::size_t>(index)];
  }
  info.effective_sample_rate_hz = effective_rate;

  return info;
}

// 06-04-PLAN.md / 06-13-PLAN.md (AUDIO-03, D-12): see this method's own
// doc comment in demux_session.h. Iterates every stream ONCE via the
// already-open ctx_, parsing each AAC stream's ASC (mediadiff's own
// libav-free bit reader, probe/audio_config.h), reading the header pass's
// OWN post-find_stream_info codecpar evidence, and handing BOTH to
// resolve_sbr_signaling().
//
// 06-13: the SbrProbeFn below is now what D-12 always called it -- a
// FALLBACK. It is constructed on every stream (an empty std::function
// costs nothing to build) but resolve_sbr_signaling() only INVOKES it
// when the header pass resolved no profile at all for that stream, which
// no fixture in this project's own corpus does. 06-04's implementation
// invoked it for every AAC stream whose ASC lacked EXPLICIT SBR -- i.e.
// every ordinary AAC-LC file -- costing one whole extra
// avformat_open_input + avformat_find_stream_info per file (measured:
// 53,343,680 retired instructions on the 600 s PERF-03 reference, 17% of
// that whole leg, of which 95.5% was the second container open).
//
// 06-18-PLAN.md (CR-05): returns Error the instant any stream's
// resolve_sbr_signaling() call itself returns an Error (the fallback
// probe's own second open failed or timed out) -- propagated by
// DemuxSession::open() as a hard failure of the whole open, never silently
// downgraded to SbrSignaling::unknown for that stream.
mediadiff::expected<void, Error> DemuxSession::compute_sbr_signaling(const std::string& utf8_path) {
  sbr_signaling_.clear();
  implicit_probe_rate_hz_.clear();
  if (ctx_ == nullptr) {
    return {};
  }
  sbr_signaling_.reserve(ctx_->nb_streams);
  implicit_probe_rate_hz_.reserve(ctx_->nb_streams);
  for (unsigned i = 0; i < ctx_->nb_streams; ++i) {
    const AVCodecParameters* codecpar = ctx_->streams[i]->codecpar;
    const bool is_aac = codecpar->codec_type == AVMEDIA_TYPE_AUDIO && codecpar->codec_id == AV_CODEC_ID_AAC;

    std::optional<AudioSpecificConfig> asc;
    if (is_aac && codecpar->extradata != nullptr && codecpar->extradata_size > 0) {
      asc = parse_audio_specific_config(
          std::span<const std::uint8_t>(codecpar->extradata, static_cast<std::size_t>(codecpar->extradata_size)));
    }

    // 06-13-PLAN.md (D-12 as DECIDED): the header pass's OWN evidence,
    // read from the codecpar this session's single avformat_open_input +
    // avformat_find_stream_info already produced. libav resolves an AAC
    // profile only by decoding, so `profile_resolved` distinguishes "the
    // decoder looked and found plain LC" from "nothing was decoded at
    // all" -- the distinction that keeps `unknown` meaning "could not be
    // determined". The AV_PROFILE_* mapping happens HERE, at the libav
    // edge, because probe/audio_config.cpp is libav-free (D-07).
    HeaderPassSbrEvidence header_evidence;
    header_evidence.profile_resolved = codecpar->profile != AV_PROFILE_UNKNOWN;
    header_evidence.profile_is_he_aac =
        codecpar->profile == AV_PROFILE_AAC_HE || codecpar->profile == AV_PROFILE_AAC_HE_V2;
    header_evidence.resolved_sample_rate_hz = codecpar->sample_rate > 0 ? codecpar->sample_rate : 0;

    const int stream_index = static_cast<int>(i);
    // 06-18-PLAN.md (CR-05): the probe callback now simply forwards
    // detail::probe_implicit_sbr_via_second_open's own expected result --
    // no local capture of the decoded rate needed anymore, since
    // SbrResolution::decode_observed_rate_hz below already carries it for
    // every branch that resolves implicit_decoded (this fallback branch
    // included).
    const SbrProbeFn probe_fn =
        [&utf8_path, stream_index]() -> mediadiff::expected<std::optional<SbrProbeDecodeResult>, Error> {
      return detail::probe_implicit_sbr_via_second_open(utf8_path, stream_index);
    };
    const mediadiff::expected<SbrResolution, Error> resolution =
        resolve_sbr_signaling(is_aac, asc, header_evidence, probe_fn);
    if (!resolution) {
      return mediadiff::unexpected(resolution.error());
    }
    sbr_signaling_.push_back(resolution->signaling);
    implicit_probe_rate_hz_.push_back(resolution->decode_observed_rate_hz);
  }
  return {};
}

std::vector<ChapterInfo> DemuxSession::chapters() const {
  std::vector<ChapterInfo> result;
  if (ctx_ == nullptr) {
    return result;
  }
  result.reserve(ctx_->nb_chapters);
  for (unsigned i = 0; i < ctx_->nb_chapters; ++i) {
    const AVChapter* chapter = ctx_->chapters[i];
    ChapterInfo info;
    info.start = chapter->start;
    info.end = chapter->end;
    info.time_base = Rational{chapter->time_base.num, chapter->time_base.den};
    const AVDictionaryEntry* title_entry = av_dict_get(chapter->metadata, "title", nullptr, 0);
    if (title_entry != nullptr && title_entry->value != nullptr) {
      info.title = title_entry->value;
    }
    result.push_back(std::move(info));
  }
  return result;
}

std::vector<std::pair<std::string, std::string>> DemuxSession::container_tags() const {
  return ctx_ != nullptr ? dict_to_pairs(ctx_->metadata) : std::vector<std::pair<std::string, std::string>>{};
}

std::vector<std::pair<std::string, std::string>> DemuxSession::stream_tags(int index) const {
  if (ctx_ == nullptr || index < 0 || static_cast<unsigned>(index) >= ctx_->nb_streams) {
    return {};
  }
  return dict_to_pairs(ctx_->streams[index]->metadata);
}

std::int64_t DemuxSession::warning_count() const { return diagnostics_.warning_count; }

// 03-09-PLAN.md Task 1 (SIZE-01): avio_size() reads the underlying
// protocol's own size (for the file:// protocol this project's fixtures
// always use, an fstat-equivalent call) -- no bytes are read, no second
// file open happens. A negative return means "unknown" (libav's own
// AVERROR convention here, not a distinguishable error code) -- reported
// as std::nullopt rather than as 0 or a negative int64, so a caller can
// never mistake "unknown" for "an empty file".
std::optional<std::int64_t> DemuxSession::file_size_bytes() const {
  if (ctx_ == nullptr || ctx_->pb == nullptr) {
    return std::nullopt;
  }
  const std::int64_t size = avio_size(ctx_->pb);
  if (size < 0) {
    return std::nullopt;
  }
  return size;
}

// 05-04-PLAN.md (TIME-01/TIME-03): the container-declared member of
// timeline.duration's triple -- AV_NOPTS_VALUE (never coerced to 0) stays
// std::nullopt, mirroring StreamInfo::declared_duration_ticks' identical
// convention above. 05-17-PLAN.md (Gap 2, TIME-02/TIME-03): overridden by
// reprobe_ts_declared_durations()'s own stored value the same way
// stream_info()'s declared_duration_ticks is, above -- see that override's
// own comment for the full reasoning.
std::optional<std::int64_t> DemuxSession::container_duration_ticks() const {
  if (declared_duration_source_ == DeclaredDurationSource::overflow_corrected_reprobe) {
    return reprobed_container_duration_ticks_;
  }
  if (declared_duration_source_ == DeclaredDurationSource::withheld_wrap_uncorrectable) {
    return std::nullopt;
  }
  if (ctx_ == nullptr || ctx_->duration == AV_NOPTS_VALUE) {
    return std::nullopt;
  }
  return ctx_->duration;
}

namespace detail {

bool stream_layouts_match(std::span<const StreamLayoutKey> primary, std::span<const StreamLayoutKey> reprobe) {
  if (primary.size() != reprobe.size()) {
    return false;
  }
  for (std::size_t i = 0; i < primary.size(); ++i) {
    if (primary[i].codec_type != reprobe[i].codec_type || primary[i].stream_id != reprobe[i].stream_id) {
      return false;
    }
  }
  return true;
}

}  // namespace detail

}  // namespace mediadiff
