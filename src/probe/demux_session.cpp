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
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/error.h>
#include <libavutil/log.h>
}

#include <cerrno>

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

}  // namespace

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

ContainerFamily container_family_from_format_name(std::string_view format_name) {
  if (format_name == "mov") {
    return ContainerFamily::mp4;
  }
  if (format_name == "matroska") {
    return ContainerFamily::mkv;
  }
  if (format_name == "mpegts") {
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
      interrupt_state_(std::move(other.interrupt_state_)) {
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
  AVFormatContext* ctx = avformat_alloc_context();
  if (ctx == nullptr) {
    return mediadiff::unexpected(Error{ErrorKind::internal, "could not allocate AVFormatContext for '" + utf8_path + "'"});
  }

  // Attaches this call's own diagnostics accumulator to the thread_local
  // slot probe_log_callback reads (Task 2) -- cleared unconditionally on
  // every exit path via this RAII guard, so a fresh DemuxSession::open on
  // the same thread always starts from a null pointer, never a stale one
  // left behind by a prior call's early return.
  ProbeDiagnostics diagnostics;
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
  interrupt_state->budget_ms = std::numeric_limits<std::int64_t>::max();

  std::string fmt_name = first_token(ctx->iformat != nullptr ? ctx->iformat->name : nullptr);
  DemuxSession session(ctx, std::move(fmt_name), std::move(interrupt_state));
  session.diagnostics_ = diagnostics;
  return session;
}

std::string_view DemuxSession::format_name() const { return format_name_; }

int DemuxSession::stream_count() const { return ctx_ != nullptr ? static_cast<int>(ctx_->nb_streams) : 0; }

std::int64_t DemuxSession::warning_count() const { return diagnostics_.warning_count; }

}  // namespace mediadiff
