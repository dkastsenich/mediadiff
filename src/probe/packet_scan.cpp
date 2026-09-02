#include "probe/packet_scan.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

extern "C" {
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

#include "core/rational.h"
#include "probe/demux_session.h"

namespace mediadiff {

namespace {

// Backing store for default_packet_scan_max_bytes()/
// set_default_packet_scan_max_bytes() -- mirrors
// src/probe/demux_session.cpp's own g_default_wall_clock_budget_ms:
// written at most once per CLI invocation, on the main thread, before any
// worker thread starts; read many times afterward. Relaxed ordering is
// sufficient for that write-once-then-read-only shape. Starts already
// converted to bytes at threads=1 (derive_per_file_cap_bytes' own
// identity case), matching what every single-file command resolves to
// before it ever calls the setter.
std::atomic<std::int64_t> g_default_packet_scan_max_bytes{kDefaultProbeMemoryBudgetMb * 1024 * 1024};

}  // namespace

std::int64_t default_packet_scan_max_bytes() {
  return g_default_packet_scan_max_bytes.load(std::memory_order_relaxed);
}

void set_default_packet_scan_max_bytes(std::int64_t bytes) {
  g_default_packet_scan_max_bytes.store(bytes, std::memory_order_relaxed);
}

std::int64_t derive_per_file_cap_bytes(std::int64_t budget_bytes, int threads) {
  if (threads <= 1) {
    return budget_bytes;
  }
  return budget_bytes / threads;
}

namespace detail {

PacketRecord make_packet_record(const AVPacket& pkt) {
  PacketRecord record;
  record.pts = pkt.pts;
  record.dts = pkt.dts;
  record.duration = pkt.duration;
  record.size = pkt.size;
  record.pos = pkt.pos;
  record.flags = pkt.flags;
  return record;
}

}  // namespace detail

namespace {

// RAII wrapper around one reused AVPacket* -- allocated once with
// av_packet_alloc(), av_packet_unref() after every record (whether
// appended or refused), av_packet_free() on every exit path including an
// early return, per T-3-13's own mitigation (a leak per packet over a
// potentially 5,000,000-packet sweep is itself a memory-exhaustion
// vector).
class ScratchPacket {
 public:
  ScratchPacket() : pkt_(av_packet_alloc()) {}
  ScratchPacket(const ScratchPacket&) = delete;
  ScratchPacket& operator=(const ScratchPacket&) = delete;
  ~ScratchPacket() { av_packet_free(&pkt_); }

  bool valid() const { return pkt_ != nullptr; }
  AVPacket* get() const { return pkt_; }
  void unref() { av_packet_unref(pkt_); }

 private:
  AVPacket* pkt_ = nullptr;
};

}  // namespace

mediadiff::expected<PacketScanResult, Error> run_packet_scan(DemuxSession& session, const PacketScanLimits& limits) {
  AVFormatContext* ctx = session.native_context();
  if (ctx == nullptr) {
    // Defensive only -- every DemuxSession this function is ever handed
    // came from a successful DemuxSession::open() call and was never
    // moved-from in between (src/probe/orchestrator.cpp's own `session`
    // local is exactly such an object). Guarded so a future caller
    // mistake degrades to a clean Error rather than a null-pointer
    // dereference inside av_read_frame.
    return mediadiff::unexpected(Error{ErrorKind::internal, "run_packet_scan called on a closed/moved-from DemuxSession"});
  }

  PacketScanResult result;
  const std::size_t stream_count = static_cast<std::size_t>(ctx->nb_streams);
  result.per_stream.resize(stream_count);
  for (std::size_t i = 0; i < stream_count; ++i) {
    const AVRational tb = ctx->streams[i]->time_base;
    result.per_stream[i].tb = Rational{tb.num, tb.den};
  }

  ScratchPacket pkt;
  if (!pkt.valid()) {
    return mediadiff::unexpected(Error{ErrorKind::internal, "could not allocate AVPacket for a packet scan"});
  }

  std::int64_t accounted_bytes = 0;
  for (;;) {
    const int rc = av_read_frame(ctx, pkt.get());
    ++result.read_frame_call_count;

    if (rc == AVERROR_EOF) {
      // The clean end of the readable region -- not a truncation.
      break;
    }
    if (rc < 0) {
      // Any other negative return ends the sweep early: the D-02 case.
      // The whole result is partial, whether or not any individual
      // stream's own ceiling was ever reached.
      result.partial = true;
      break;
    }

    if (pkt.get()->stream_index < 0 ||
        static_cast<std::size_t>(pkt.get()->stream_index) >= result.per_stream.size()) {
      // Defensive only -- libav's own contract is that stream_index is
      // always a valid index into AVFormatContext::streams for a packet
      // it just handed back via av_read_frame. Never observed in
      // practice; guarded so a future libav behavior change degrades to
      // "drop this one packet" rather than an out-of-bounds write.
      pkt.unref();
      continue;
    }

    StreamPacketScan& stream = result.per_stream[static_cast<std::size_t>(pkt.get()->stream_index)];

    // doc 02's per-stream packet-count ceiling (checked first -- cheap,
    // and avoids ever evaluating the byte-budget arithmetic for a stream
    // that has already stopped for this, unrelated, reason).
    if (static_cast<std::int64_t>(stream.packets.size()) >= limits.max_packets_per_stream) {
      stream.partial = true;
      result.partial = true;
      pkt.unref();
      continue;
    }

    // D-01's accounted-byte budget, shared across every stream in this
    // file (the SAME running total every stream's append checks against
    // -- the cap bounds the whole file's packet store, not one stream's
    // share of it). Checked and incremented BEFORE the append: the
    // append that WOULD cross the cap does not happen, so the accounted
    // total never exceeds the cap even transiently (Test 2).
    std::int64_t next_total = 0;
    const bool would_fit =
        detail::checked_add(accounted_bytes, static_cast<std::int64_t>(sizeof(PacketRecord)), &next_total) &&
        next_total <= limits.max_bytes;
    if (!would_fit) {
      stream.partial = true;
      result.partial = true;
      pkt.unref();
      continue;
    }
    accounted_bytes = next_total;

    const PacketRecord record = detail::make_packet_record(*pkt.get());
    stream.byte_total += record.size;
    stream.packets.push_back(record);

    pkt.unref();
  }

  result.accounted_bytes = accounted_bytes;
  return result;
}

}  // namespace mediadiff
