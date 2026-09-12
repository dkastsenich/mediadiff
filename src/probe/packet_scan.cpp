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

mediadiff::expected<PacketScanOutputs, Error> run_packet_scan(DemuxSession& session, const PacketScanRequest& request) {
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

  const PacketScanLimits& limits = request.limits;

  PacketScanOutputs outputs;
  PacketScanResult& result = outputs.packets;
  const std::size_t stream_count = static_cast<std::size_t>(ctx->nb_streams);
  result.per_stream.resize(stream_count);
  for (std::size_t i = 0; i < stream_count; ++i) {
    const AVRational tb = ctx->streams[i]->time_base;
    result.per_stream[i].tb = Rational{tb.num, tb.den};
  }

  // PROBE-03 (04-01-PLAN.md Task 2): per-stream parser lifetime, mirroring
  // ScratchPacket's own "one RAII holder, freed on every exit path"
  // discipline (T-4-02). Only allocated at all when parsing was
  // requested -- an ordinary Phase-3 caller (the inline
  // PacketScanLimits-only overload, packet_scan.h) never pays for this
  // vector.
  std::vector<detail::StreamParserState> parser_states;
  if (request.parse_access_units) {
    outputs.access_units = ParserScanResult{};
    outputs.access_units->per_stream.resize(stream_count);
    parser_states.resize(stream_count);
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
      // stream's own ceiling was ever reached. The parser result (when
      // requested) is truncated by the same event, for the same reason.
      result.partial = true;
      if (outputs.access_units.has_value()) {
        outputs.access_units->partial = true;
      }
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

    const std::size_t stream_index = static_cast<std::size_t>(pkt.get()->stream_index);
    StreamPacketScan& stream = result.per_stream[stream_index];

    // doc 02's per-stream packet-count ceiling (checked first -- cheap,
    // and avoids ever evaluating the byte-budget arithmetic for a stream
    // that has already stopped for this, unrelated, reason).
    if (static_cast<std::int64_t>(stream.packets.size()) >= limits.max_packets_per_stream) {
      stream.partial = true;
      result.partial = true;
      if (outputs.access_units.has_value()) {
        // This stream stopped collecting packets entirely -- its own
        // access-unit array is truncated by the same event.
        outputs.access_units->per_stream[stream_index].partial = true;
        outputs.access_units->partial = true;
      }
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
      if (outputs.access_units.has_value()) {
        outputs.access_units->per_stream[stream_index].partial = true;
        outputs.access_units->partial = true;
      }
      pkt.unref();
      continue;
    }
    accounted_bytes = next_total;

    const PacketRecord record = detail::make_packet_record(*pkt.get());
    stream.byte_total += record.size;
    stream.packets.push_back(record);

    // PROBE-03: the parser fusion point -- AFTER the PacketRecord append,
    // BEFORE pkt.unref(), inside this SAME loop iteration (never a second
    // av_read_frame sweep, never a second orchestrator dispatch arm; see
    // probe/orchestrator.cpp's own comment on Pass::parser_scan). Only
    // reached for a packet whose OWN PacketRecord was just accepted --
    // a packet refused above (either ceiling) never reaches the parser
    // either, matching the accepted-only shape the D-01 budget below
    // assumes.
    if (request.parse_access_units) {
      StreamParserScan& pstream = outputs.access_units->per_stream[stream_index];
      detail::StreamParserState& pstate = parser_states[stream_index];
      pstate.ensure_initialized(*ctx->streams[stream_index]->codecpar);
      pstream.has_parser = pstate.has_parser();

      if (pstate.has_parser()) {
        AccessUnitRecord au{};
        const bool is_key = (pkt.get()->flags & AV_PKT_FLAG_KEY) != 0;
        if (pstate.parse_packet(pkt.get()->data, pkt.get()->size, pkt.get()->pts, pkt.get()->dts, pkt.get()->pos,
                                 is_key, &au)) {
          // The SAME accounted_bytes running total and the SAME
          // limits.max_bytes ceiling the PacketRecord append above just
          // checked -- no second, independent per-AU budget (T-4-01).
          std::int64_t au_next_total = 0;
          const bool au_would_fit = detail::checked_add(accounted_bytes, static_cast<std::int64_t>(sizeof(AccessUnitRecord)),
                                                          &au_next_total) &&
                                     au_next_total <= limits.max_bytes;
          if (!au_would_fit) {
            pstream.partial = true;
            outputs.access_units->partial = true;
          } else {
            accounted_bytes = au_next_total;
            pstream.access_units.push_back(au);
          }
        }
      }
      // 04-09-PLAN.md Task 2 (`video.gop.refs`): not per-AU-budgeted (a
      // single optional int64 per stream, unlike AccessUnitRecord) --
      // refreshed every iteration rather than only once, so it reflects
      // `pstate`'s own state as soon as its first SPS resolves, whether or
      // not this particular packet produced an access unit.
      pstream.ref_frame_count = pstate.ref_frame_count();
    }

    pkt.unref();
  }

  result.accounted_bytes = accounted_bytes;
  return outputs;
}

}  // namespace mediadiff
