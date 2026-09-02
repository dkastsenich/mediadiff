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

#include <cstdarg>
#include <cstdint>
#include <string>
#include <string_view>

#include "core/error.h"
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

  // The number of AV_LOG_WARNING-and-above lines libav emitted while this
  // session's own open() call was running (Task 2). 0 for a clean file.
  std::int64_t warning_count() const;

 private:
  DemuxSession(AVFormatContext* ctx, std::string format_name);

  AVFormatContext* ctx_ = nullptr;
  std::string format_name_;
  ProbeDiagnostics diagnostics_;
};

}  // namespace mediadiff
