#pragma once

// 07-08-PLAN.md (CONTENT-11, CONTENT-07; 07-RESEARCH.md Q10 design A): the
// lockstep driver that lets `compare` score two decoded video sequences against
// each other WITHOUT ever holding two decoded sequences in memory.
//
// Architecture. `compare` of two media files runs each side as an UNCHANGED
// one-sided sweep (detail::run_probe, one av_read_frame loop per file, every
// sink fused into it -- hash, thumbnail, frozen and black detectors, captions,
// first-frame HDR) on its own std::thread. The only addition is a `FrameTap`
// the video decode state publishes each decoded primary-stream frame through,
// AFTER every one-sided sink has run. Each side has ONE single-slot rendezvous
// (`FrameSlot`): `publish()` stores the frame and blocks until the consumer
// calls `release()`, so at most one decoded frame per side is ever in flight
// (plus libav's own reorder buffer, which is not ours to bound). The consumer --
// the PairScorer of probe/pair_scorer.h -- runs on the CALLING thread.
//
// Determinism (TRUST-05). The consumer, never thread timing, decides which side
// advances; each producer's one-sided measurements depend only on its own file
// (the tap only ever WAITS, it never changes a sink's input); and the two-file
// measurement is written into both fingerprints only AFTER both producer
// threads have joined. Byte-identical `--json` across runs follows.
//
// Cancellation with plain std::thread, std::mutex and std::condition_variable
// only (CLAUDE.md toolchain parity: no cooperative-cancellation types, no
// coroutines): `close()` sets a flag under the slot's mutex and
// wakes every waiter; a blocked `publish()` returns false and its producer
// finishes its OWN sweep without tapping any further frames. Every exit path of
// the consumer -- normal end, early end of one side, a latched stop, a
// producer error -- closes both slots and joins both threads before returning.
// Every wait is a predicate loop, so a spurious wakeup can never advance a
// side. No exception crosses this boundary: a thread body catches everything
// and reports it as an Error.

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "core/error.h"
#include "core/model.h"
#include "core/registry.h"
#include "probe/orchestrator.h"
#include "probe/pass.h"
#include "probe/video_thumbnail.h"
#include "util/expected.h"

// Opaque forward declaration at global scope, matching libav's own C
// declaration site (mirrors probe/video_decode.h).
struct AVFrame;

namespace mediadiff {

// One decoded primary-stream frame, as the consumer sees it. Every pointer is
// VALID ONLY until the matching FrameSlot::release(): the producer is blocked
// in publish() for exactly that long, so it cannot overwrite the thumbnail or
// free the frame. The consumer never retains a pointer past release(); only
// derived integers and its own copies of thumbnails outlive a pair (T-07-25).
struct TappedFrame {
  // The libav stream index this frame came from.
  int stream_index = -1;
  // The frame's position among every frame the decoder produced for the stream
  // (the same decode index frame_hash's divergence locator reports), whether or
  // not it was hashable or stored.
  std::int64_t decode_index = 0;
  // The frame's own presentation timestamp in the stream's time base, when it
  // has one (a raw elementary stream delivers none).
  bool has_pts = false;
  std::int64_t pts = 0;
  // The stream's time base (seconds per tick) and its frame interval in
  // seconds (num/den); an interval of {0, 0} is unknown (MPEG-TS at open).
  std::int64_t tb_num = 0;
  std::int64_t tb_den = 1;
  std::int64_t interval_num = 0;
  std::int64_t interval_den = 0;
  // The 128-wide 8-bit luma thumbnail of this frame, or null when the
  // thumbnail could not be made (the scorer then stops with a named reason).
  const Thumbnail* thumbnail = nullptr;
  // The decoded frame itself, for 07-10's native-resolution scorers. Borrowed;
  // valid only until release().
  const AVFrame* frame = nullptr;
};

// What one side reports when its sweep is over (the producer calls
// FrameTap::finish with it from the decode state's finalize, and a thread
// wrapper calls finish with an empty one if the sweep ended without that).
struct TapEnd {
  // A primary video stream (the first non-attached-picture one) existed and the
  // tap was bound to it.
  bool has_primary = false;
  int stream_index = -1;
  // The primary stream's rank among ALL video streams (attached pictures
  // included), i.e. the Scope index every video.* measurement uses.
  int video_scope_index = -1;
  // False when the decoder could not be opened (`fallback_reason` says why).
  bool attempted = false;
  std::string fallback_reason;
  // False when the sweep over the primary stream did not run to a meaningful
  // end; `incomplete_reason` names why (scan_partial, undecodable, or the
  // decode truncation reason). A prefix's worst frame says nothing about the
  // rest, so the perceptual check skips instead of reporting one.
  bool complete = true;
  std::string incomplete_reason;
  // D-04's path record for this side: the 07-05 scaler identity of the
  // thumbnail the side produced, and the decoder flags it decoded under.
  std::string scaler_record;
  int thumbnail_height = 0;
  std::string flags_recorded;
  // Every frame the tap accepted (publish returned true).
  std::int64_t frames_published = 0;
};

// The producer-facing half of a rendezvous: what VideoDecodeState talks to.
class FrameTap {
 public:
  virtual ~FrameTap() = default;
  // Stores `frame` and blocks until the consumer releases it or the tap is
  // closed. Returns false when closed: the producer must not publish again.
  virtual bool publish(const TappedFrame& frame) = 0;
  // Marks the end of this side's stream and records `end`. Idempotent: only the
  // first call records anything.
  virtual void finish(const TapEnd& end) = 0;
};

// A single-slot rendezvous between one producer thread and the consumer.
class FrameSlot final : public FrameTap {
 public:
  FrameSlot() = default;
  FrameSlot(const FrameSlot&) = delete;
  FrameSlot& operator=(const FrameSlot&) = delete;

  // Producer side.
  bool publish(const TappedFrame& frame) override;
  void finish(const TapEnd& end) override;

  // Consumer side. `take` blocks until a frame is available (true, `*out` set;
  // the slot stays occupied until release) or the stream ended or the slot was
  // closed with nothing to take (false).
  bool take(TappedFrame* out);
  // Frees the producer to decode its next frame.
  void release();
  // Wakes every waiter and refuses every later publish. The consumer must have
  // stopped using the current frame (close implicitly releases it).
  void close();

  // True once finish() was called.
  bool finished() const;
  // The recorded end-of-stream report (an empty TapEnd until finish()).
  TapEnd end() const;
  // The most frames ever held in flight at once (published and not yet
  // released). Structurally at most 1; it is counted, not assumed, so a test can
  // assert the memory claim against the slot's own counter.
  int max_occupancy() const;
  // Frames accepted by publish().
  std::int64_t published() const;

 private:
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  TappedFrame entry_{};
  bool has_entry_ = false;
  bool taken_ = false;
  bool closed_ = false;
  bool finished_ = false;
  TapEnd end_{};
  std::int64_t published_ = 0;
  std::int64_t released_ = 0;
  int max_occupancy_ = 0;
};

// Both fingerprints of a media-vs-media compare, index-aligned with the
// arguments of fingerprint_pair.
struct PairResult {
  Fingerprint baseline;
  Fingerprint candidate;
};

// The compare path's probe entry: probes both inputs and, when BOTH are media
// files and content decode is enabled, does so in lockstep (see the header
// comment) and writes the two-file measurements (content.video.perceptual)
// into both fingerprints after the threads joined. Any other combination -- a
// snapshot on either side, or `--no-content` -- takes the sequential path
// exactly as two fingerprint_input calls would, so a snapshot side keeps its
// read_snapshot short-circuit and every JSON-shaped rejection unchanged. The
// first error is reported in baseline-then-candidate order.
mediadiff::expected<PairResult, Error> fingerprint_pair(const std::string& baseline_path,
                                                          const std::string& candidate_path,
                                                          const CheckRegistry& registry,
                                                          const ProbeOptions& options);

namespace detail {

// Test-visible account of one lockstep run. The two inputs are test hooks;
// production code passes no log at all.
struct PairProbeLog {
  // Input (test hook): stop scoring after this many SCORED pairs and close both
  // slots, the way a consumer that gives up would. 0 means never.
  std::int64_t stop_after_scored_pairs = 0;

  // Each side's `av_read_frame` call count (PacketScanResult::
  // read_frame_call_count): equal to a packet-scan-only run of the same file
  // when the lockstep adds no second sweep (CONTENT-07).
  std::int64_t baseline_read_frame_calls = 0;
  std::int64_t candidate_read_frame_calls = 0;
  // Each slot's own occupancy counter and accepted-frame count.
  int baseline_max_occupancy = 0;
  int candidate_max_occupancy = 0;
  std::int64_t baseline_frames_published = 0;
  std::int64_t candidate_frames_published = 0;
  // The scorer's own counts.
  std::int64_t pairs_paired = 0;
  std::int64_t pairs_scored = 0;
  std::int64_t unpaired_baseline = 0;
  std::int64_t unpaired_candidate = 0;
  // True when the consumer stopped before both sides ended, and why (empty
  // otherwise): geometry_mismatch, thumbnail_unavailable, thumbnail_too_small,
  // no_partner or test_stop.
  bool stopped_early = false;
  std::string stop_reason;
};

// The lockstep itself, with an injectable analyzer list (a test's capture
// analyzer rides beside all_analyzers()) and an optional log. Both inputs are
// probed as media; the caller (fingerprint_pair) has already ruled out
// snapshots. Every thread is joined before this returns, on every path.
mediadiff::expected<PairResult, Error> run_pair_probe(const std::string& baseline_path,
                                                        const std::string& candidate_path,
                                                        const std::vector<AnalyzerSpec>& analyzers,
                                                        const ProbeOptions& options, PairProbeLog* log);

}  // namespace detail

}  // namespace mediadiff
