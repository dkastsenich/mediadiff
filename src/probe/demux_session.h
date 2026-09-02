#pragma once

// PROBE-01: DemuxSession, the header pass every analyzer this phase (and
// every later phase) builds on. A move-only RAII owner of one
// AVFormatContext*, opened via avformat_open_input + avformat_find_stream_info
// and never anything past that -- no packet is read here (PacketScan, plan
// 03-03, is a separate pass). AVFormatContext is kept behind an opaque
// forward declaration so no libav header crosses this file's public
// surface -- every accessor returns a plain value/string/int, matching
// "core/ and compare/ include no libav header" (this plan's own
// acceptance criterion; src/analyzers/ reaches libav-derived data only
// through these accessors, never a raw AVFormatContext*).
//
// A per-file wall-clock budget (kDefaultProbeBudgetMs) bounds this open
// via an AVIOInterruptCB installed before avformat_open_input -- this
// bounds the between-packet and find_stream_info-loop read cases
// (ff_check_interrupt is called from both, confirmed against
// libavformat/demux.c:2709 and avio.c:515) but CANNOT preempt a syscall
// already blocked inside a single read() (libavformat/file.c never
// references the interrupt callback at all -- confirmed by its total
// absence from that file) -- an accepted residual, documented again at
// the callback's own definition in demux_session.cpp.
//
// AVFMT_FLAG_GENPTS is NEVER set here -- the probe layer must see the
// container's own reality, not libav's timestamp repairs. It is off by
// default in FFmpeg 8.1 (options_table.h's fflags default is
// AVFMT_FLAG_AUTO_BSF only), so "never touch AVFormatContext::flags at
// all" is a stronger, simpler invariant than "clear GENPTS after open".

#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/error.h"
#include "core/rational.h"
#include "util/expected.h"

// Opaque forward declaration -- demux_session.cpp is the only translation
// unit that includes libavformat/avformat.h and therefore the only one
// that ever sees the real definition (PImpl-by-forward-declaration). No
// consumer of this header needs the real type.
struct AVFormatContext;

namespace mediadiff {

// Wall-clock budget (30 s default) DemuxSession::open enforces around
// avformat_open_input + avformat_find_stream_info via an AVIOInterruptCB.
// doc 06 section 5's own perf target is 3 s for metadata + timeline on
// the 10-minute 1080p reference; 30 s is 10x headroom -- far beyond any
// legitimate probe, short enough that one pathological file cannot wedge
// a `dir` corpus run for hours. Configurable via --probe-timeout /
// `[probe] timeout_seconds` (03-02-PLAN.md Task 2).
inline constexpr std::int64_t kDefaultProbeBudgetMs = 30000;

// The current process-wide default wall-clock budget: starts at
// kDefaultProbeBudgetMs and is set at most once per CLI invocation, before
// any DemuxSession::open call, by the resolved --probe-timeout / `[probe]
// timeout_seconds` value (src/cli/options.cpp's resolve_probe_timeout_ms)
// -- the same "resolved once, read many times across dir mode's worker
// threads" pattern src/cli/commands/dir.cpp already established for
// mediadiff.toml itself. DemuxOptions's own default member initializer
// reads this, which is what lets src/probe/orchestrator.cpp's single
// `DemuxSession::open(path, DemuxOptions{})` call (unchanged since Task 1)
// automatically honor a CLI-configured timeout without orchestrator.cpp
// itself ever being touched again. Backed by a relaxed atomic: written
// once, on the main thread, before any worker thread is spawned, and read
// many times afterward -- relaxed ordering is sufficient for a
// write-once-then-read-only value with no other synchronization
// dependency.
std::int64_t default_wall_clock_budget_ms();
void set_default_wall_clock_budget_ms(std::int64_t ms);

struct DemuxOptions {
  std::int64_t wall_clock_budget_ms = default_wall_clock_budget_ms();
};

// The libav warning/error line count DemuxSession::open accumulates while
// it is inside libav (Task 2, PROBE-01 completion): folded into
// DemuxSession::warning_count() once open() returns, and from there into
// Fingerprint.envelope.diagnostics by src/probe/orchestrator.cpp. Counts
// only -- no line text is retained (there is no consumer of retained text
// yet; a later phase that wants it extends this struct rather than
// reinventing the counting half).
struct ProbeDiagnostics {
  std::int64_t warning_count = 0;
};

// The process-wide libav log callback (Task 2): installed exactly once,
// at process start, from src/cli/main.cpp's own single installer call --
// NEVER from a worker thread, since that libav entry point overwrites a
// single process-global function pointer and the last caller anywhere in
// the process wins for every thread (libavutil/log.c). Reads a
// thread_local accumulator pointer that DemuxSession::open sets before
// calling into libav and clears when it returns; when the pointer is null
// (no DemuxSession::open call active on this thread) the line is dropped.
// Counts only lines at AV_LOG_WARNING and above (numerically <=, libav's
// own convention: more severe means a lower number).
void probe_log_callback(void* avcl, int level, const char* fmt, va_list args);

// Attaches `diagnostics` as the thread_local accumulator probe_log_callback
// writes into. DemuxSession::open calls this internally around its own
// libav calls; it is exposed here (rather than kept file-local to
// demux_session.cpp) so tests/unit/test_demux_session.cpp can drive the
// callback's counting and thread-local-attribution behavior deterministically
// via a direct av_log() call, without depending on a specific fixture
// reliably triggering a real libav warning. Passing nullptr detaches.
void set_current_probe_diagnostics(ProbeDiagnostics* diagnostics);

namespace detail {

// Wall-clock budget state read by the AVIOInterruptCB DemuxSession::open
// installs. MUST OUTLIVE the AVFormatContext it was installed on: ffmpeg's
// avio layer captures the AVIOInterruptCB (callback + opaque) into its own
// URLContext at avio_open2() time -- a copy fully independent of
// AVFormatContext::interrupt_callback from that point forward. Clearing
// AVFormatContext::interrupt_callback after open() completes therefore does
// NOT reach that already-captured URLContext copy; the object this pointer
// names must instead remain valid memory for as long as reads can happen on
// this session (confirmed via AddressSanitizer stack-use-after-return
// against a stack-local InterruptState -- 03-03-PLAN.md Task 1's own
// Rule 1 fix; see demux_session.cpp's "disarm" comment for the full trace).
// DemuxSession owns exactly one of these via std::unique_ptr so its ADDRESS
// stays stable across a DemuxSession move (a member held by value would
// move to a new address on every move, re-dangling the pointer the io
// layer already captured at open time).
struct InterruptState {
  std::chrono::steady_clock::time_point start;
  std::int64_t budget_ms;
};

}  // namespace detail

// 03-04-PLAN.md Task 1: the five per-stream media kinds doc 02 section 2's
// container.track_count histogram fixes bin order over, plus `other` for
// any AVMediaType this project has no dedicated bin for (folded into the
// histogram's `data` bin -- doc 02's own table has no sixth bin, and a
// codec_type this project has never observed in practice is closer to
// "opaque data" than to any of the other four). Deliberately NOT the same
// enum as core/model.h's Scope::Kind, which has no `attachment` counterpart
// at all (03-CHECK-ROSTER.md / 03-PATTERNS.md's own note) -- callers map
// this enum to Scope::Kind themselves where a scope is needed, folding
// `attachment`/`other` into whatever the calling analyzer decides (usually:
// counted at Scope::Kind::global, never given a per-stream scope of their
// own).
enum class StreamMediaType : std::uint8_t {
  video,
  audio,
  subtitle,
  data,
  attachment,
  other,
};

// One probed stream's topology-relevant properties (container.track_count/
// track_types/track_order, CONT-09's explicit tmcd/caption naming).
// `codec_name` is libav's own stable string name (avcodec_get_name), never
// the numeric AVCodecID -- doc 02 section 2's own requirement that a
// recorded signature not shift when FFmpeg renumbers an enum across a
// version bump.
struct StreamInfo {
  StreamMediaType media_type = StreamMediaType::other;
  std::string codec_name;
  // codecpar->codec_tag == MKTAG('t','m','c','d') -- the MOV/MP4 timecode
  // track marker (confirmed against libavformat/mov.c's own mov_read_tmcd
  // dispatch, keyed on this exact tag). CONT-09: this is what lets
  // container.track_types name a dropped tmcd track explicitly rather than
  // only reflecting it in a generic `data` count.
  bool is_timecode = false;
  // codecpar->codec_id == AV_CODEC_ID_EIA_608 -- CEA-608 caption data,
  // confirmed present in this pinned FFmpeg's libavcodec/codec_id.h.
  // CONT-09's caption-track counterpart to is_timecode above.
  bool is_caption = false;
};

// One chapter's raw fields, straight off AVChapter -- start/end share ONE
// time_base (doc 02 section 2's own note on container.chapters), converted
// to mediadiff::Rational at this accessor (the probe edge, D-07) rather
// than left as AVRational so no analyzer ever needs a libav header.
struct ChapterInfo {
  std::int64_t start = 0;
  std::int64_t end = 0;
  Rational time_base{0, 1};
  std::string title;
};

class DemuxSession {
 public:
  DemuxSession(const DemuxSession&) = delete;
  DemuxSession& operator=(const DemuxSession&) = delete;
  DemuxSession(DemuxSession&& other) noexcept;
  DemuxSession& operator=(DemuxSession&& other) noexcept;
  ~DemuxSession();

  // Opens `utf8_path` and reads its stream info, constructed only through
  // this static factory. Maps AVERROR(ENOENT)/AVERROR(EACCES) to
  // ErrorKind::input_open ("the bytes did not open at all"); every other
  // failure (AVERROR_INVALIDDATA, a budget-triggered AVERROR_EXIT, ...)
  // to ErrorKind::input_unsupported ("opened, or tried to, but is not
  // something this parser understands / took too long"), each message
  // naming the concrete path. Never throws.
  static mediadiff::expected<DemuxSession, Error> open(const std::string& utf8_path, const DemuxOptions& options);

  // The first comma-delimited token of AVInputFormat::name (doc 02
  // section 2's "container.format" extraction rule) -- e.g. libav reports
  // MP4 as "mov,mp4,m4a,3gp,3g2,mj2"; this returns "mov". Matroska
  // reports "matroska,webm"; this returns "matroska".
  std::string_view format_name() const;

  int stream_count() const;

  // 03-04-PLAN.md Task 1: one stream's topology-relevant properties.
  // `index` is trusted to be < stream_count() by every call site (an
  // analyzer, never end-user input) -- out of range returns a
  // default-constructed StreamInfo{} rather than reading past the array,
  // matching CheckRegistry::at's own "caller-trusted index" convention.
  StreamInfo stream_info(int index) const;

  // 03-04-PLAN.md Task 1: every AVChapter, in AVFormatContext::chapters
  // array order -- title is that chapter's own "title" metadata key, empty
  // string if absent.
  std::vector<ChapterInfo> chapters() const;

  // 03-04-PLAN.md Tasks 2-3 (meta.tags, meta.tags.language): the raw
  // key/value pairs of the container-level and one stream's own metadata
  // dictionary, in AVDictionary iteration order. Values are libav's raw
  // bytes with no encoding guarantee -- meta.cpp, not this accessor, is
  // responsible for the UTF-8 replacement-character sanitization doc 02's
  // T-3-15 mitigation requires before any value enters a compared Value or
  // evidence.
  std::vector<std::pair<std::string, std::string>> container_tags() const;
  std::vector<std::pair<std::string, std::string>> stream_tags(int index) const;

  // The number of AV_LOG_WARNING-and-above lines libav emitted while this
  // session's own open() call was running (Task 2). 0 for a clean file.
  std::int64_t warning_count() const;

  // 03-09-PLAN.md Task 1 (SIZE-01): the container's own byte size, queried
  // directly from the already-open AVIOContext (avio_size) -- no second
  // file open, no dependency on PacketScan having completed. This is what
  // lets size.file (src/analyzers/size/size.cpp) report independently of
  // PacketScanResult::partial (D-02): the file's size on disk is a
  // property of the FILE, not of the scan, so it is deliberately read
  // through this accessor rather than derived from any packet total.
  // std::nullopt when the underlying protocol cannot report a size (e.g.
  // a genuinely non-seekable/streamed input) -- no fixture this project
  // generates produces one, but the accessor stays honest rather than
  // fabricating 0.
  std::optional<std::int64_t> file_size_bytes() const;

  // Internal borrow of the raw AVFormatContext* for other src/probe/
  // translation units that need to call libav directly against the SAME
  // already-open session -- src/probe/packet_scan.cpp's av_read_frame
  // sweep (03-03-PLAN.md Task 1) is the first caller. NOT part of the
  // public, analyzer-facing surface documented at the top of this file:
  // AVFormatContext stays behind its opaque forward declaration, so a
  // caller outside src/probe/ that somehow obtains this pointer still
  // cannot dereference it (no complete type in scope) -- only a
  // translation unit that itself includes libavformat/avformat.h (i.e.
  // another src/probe/*.cpp) can do anything with it. Analyzers never
  // call this; they receive an already-populated ProbeResults from the
  // orchestrator instead (this plan's own prohibition: "no analyzer
  // opens the input file itself").
  AVFormatContext* native_context() const { return ctx_; }

 private:
  DemuxSession(AVFormatContext* ctx, std::string format_name, std::unique_ptr<detail::InterruptState> interrupt_state);

  AVFormatContext* ctx_ = nullptr;
  std::string format_name_;
  ProbeDiagnostics diagnostics_;
  // Kept alive for this session's whole lifetime -- see detail::InterruptState's
  // own doc comment for why this cannot be a stack-local temporary scoped
  // to open() alone.
  std::unique_ptr<detail::InterruptState> interrupt_state_;
};

}  // namespace mediadiff
