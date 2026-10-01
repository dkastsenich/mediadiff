#include "probe/lockstep.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/check_id.h"
#include "core/rational.h"
#include "core/snapshot.h"
#include "core/value.h"
#include "probe/pair_scorer.h"
#include "util/quality_math.h"
#include "util/version.h"

namespace mediadiff {

// ---------------------------------------------------------------------------
// FrameSlot: the single-slot rendezvous. Every wait is a predicate loop.
// ---------------------------------------------------------------------------

bool FrameSlot::publish(const TappedFrame& frame) {
  std::unique_lock<std::mutex> lock(mutex_);
  if (closed_) {
    return false;
  }
  entry_ = frame;
  has_entry_ = true;
  taken_ = false;
  ++published_;
  // Counted, not assumed: published and not yet released. The producer blocks
  // below until the consumer releases, so a second publish can never start
  // while one is in flight, and this stays 1.
  const std::int64_t occupancy = published_ - released_;
  if (occupancy > max_occupancy_) {
    max_occupancy_ = static_cast<int>(occupancy);
  }
  cv_.notify_all();
  cv_.wait(lock, [this] { return !has_entry_ || closed_; });
  return !closed_;
}

void FrameSlot::finish(const TapEnd& end) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (finished_) {
    return;
  }
  end_ = end;
  finished_ = true;
  cv_.notify_all();
}

bool FrameSlot::take(TappedFrame* out) {
  std::unique_lock<std::mutex> lock(mutex_);
  cv_.wait(lock, [this] { return (has_entry_ && !taken_) || finished_ || closed_; });
  if (has_entry_ && !taken_ && !closed_) {
    *out = entry_;
    taken_ = true;
    return true;
  }
  return false;
}

void FrameSlot::release() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!has_entry_) {
    return;
  }
  has_entry_ = false;
  taken_ = false;
  ++released_;
  cv_.notify_all();
}

void FrameSlot::close() {
  std::lock_guard<std::mutex> lock(mutex_);
  closed_ = true;
  if (has_entry_) {
    // The consumer has stopped using the frame (this function's contract):
    // closing releases it, so the producer's blocked publish() can return.
    has_entry_ = false;
    taken_ = false;
    ++released_;
  }
  cv_.notify_all();
}

bool FrameSlot::finished() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return finished_;
}

TapEnd FrameSlot::end() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return end_;
}

int FrameSlot::max_occupancy() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return max_occupancy_;
}

std::int64_t FrameSlot::published() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return published_;
}

namespace {

// ---------------------------------------------------------------------------
// The producer threads.
// ---------------------------------------------------------------------------

struct ProducerJob {
  const std::string* path = nullptr;
  const std::vector<AnalyzerSpec>* analyzers = nullptr;
  ProbeOptions options;
  FrameSlot* slot = nullptr;
  std::optional<mediadiff::expected<Fingerprint, Error>> result;
  detail::ProbeScanStats stats;
};

// One side's unchanged one-sided sweep. No exception leaves this function: a
// throw (std::bad_alloc, ...) becomes an Error, and `finish` always runs so the
// consumer never waits on a side that is gone. FrameSlot::finish is idempotent,
// so this is a no-op when the sweep already reported its real end.
void run_producer(ProducerJob* job) {
  try {
    job->result.emplace(detail::run_probe(*job->path, *job->analyzers, nullptr, job->options, &job->stats));
  } catch (const std::exception& error) {
    job->result.emplace(mediadiff::unexpected(Error{ErrorKind::internal, std::string("probe thread failed: ") + error.what()}));
  } catch (...) {
    job->result.emplace(mediadiff::unexpected(Error{ErrorKind::internal, "probe thread failed with an unknown exception"}));
  }
  job->slot->finish(TapEnd{});
}

// Owns the two threads: on EVERY path out of a scope -- normal, early stop or an
// exception on the calling thread -- both slots are closed (releasing any
// blocked producer) and both threads are joined before it is destroyed.
class ProducerThreads {
 public:
  ProducerThreads(FrameSlot& baseline, FrameSlot& candidate) : baseline_(baseline), candidate_(candidate) {}
  ProducerThreads(const ProducerThreads&) = delete;
  ProducerThreads& operator=(const ProducerThreads&) = delete;
  ~ProducerThreads() { close_and_join(); }

  // False when a thread could not be started (the started one is closed and
  // joined, the caller falls back to the sequential path).
  bool start(ProducerJob* baseline_job, ProducerJob* candidate_job) {
    try {
      baseline_thread_ = std::thread(run_producer, baseline_job);
      candidate_thread_ = std::thread(run_producer, candidate_job);
    } catch (const std::system_error&) {
      close_and_join();
      return false;
    }
    return true;
  }

  void close_and_join() {
    baseline_.close();
    candidate_.close();
    if (baseline_thread_.joinable()) {
      baseline_thread_.join();
    }
    if (candidate_thread_.joinable()) {
      candidate_thread_.join();
    }
  }

 private:
  FrameSlot& baseline_;
  FrameSlot& candidate_;
  std::thread baseline_thread_;
  std::thread candidate_thread_;
};

// ---------------------------------------------------------------------------
// The consumer (the calling thread).
// ---------------------------------------------------------------------------

struct Side {
  FrameSlot* slot = nullptr;
  TappedFrame current{};
  bool have = false;
  bool done = false;
  std::int64_t taken = 0;
};

void fetch(Side& side) {
  if (side.have || side.done) {
    return;
  }
  if (side.slot->take(&side.current)) {
    side.have = true;
    ++side.taken;
  } else {
    side.done = true;
  }
}

void release(Side& side) {
  if (side.have) {
    side.slot->release();
    side.have = false;
  }
}

// Pairs the two sides' frames until one or both end or the scorer stops. On
// return neither side holds a frame; the caller closes both slots.
void consume(FrameSlot& baseline_slot, FrameSlot& candidate_slot, PairScorer& scorer) {
  Side baseline{&baseline_slot};
  Side candidate{&candidate_slot};
  for (;;) {
    fetch(baseline);
    fetch(candidate);
    if (baseline.done && candidate.done) {
      return;
    }
    if (baseline.done || candidate.done) {
      Side& live = baseline.done ? candidate : baseline;
      const Side& dead = baseline.done ? baseline : candidate;
      const bool live_is_baseline = &live == &baseline;
      if (dead.taken == 0) {
        // The other side never produced a frame: there is no partner to score
        // against. Stop, and let the caller's close() free the live producer to
        // finish its own sweep untapped -- it is never blocked by this side.
        scorer.stop(PairScorer::StopReason::no_partner);
        release(live);
        return;
      }
      // One side ended first: drain the other's remaining frames, counting each
      // unpaired, so its producer still runs to its own end (and never
      // deadlocks against a consumer that stopped taking).
      while (live.have) {
        scorer.count_unpaired(live_is_baseline, 1);
        release(live);
        fetch(live);
      }
      return;
    }

    switch (scorer.step(baseline.current, candidate.current)) {
      case PairScorer::Action::advance_both:
        release(baseline);
        release(candidate);
        break;
      case PairScorer::Action::advance_baseline:
        release(baseline);
        break;
      case PairScorer::Action::advance_candidate:
        release(candidate);
        break;
    }
    if (scorer.stopped()) {
      release(baseline);
      release(candidate);
      return;
    }
  }
}

// ---------------------------------------------------------------------------
// Assembling the two-file measurement, after both threads joined.
// ---------------------------------------------------------------------------

constexpr std::int64_t kMicro = 1000000;

nlohmann::ordered_json micro_json(std::int64_t micro) {
  return nlohmann::ordered_json{{"num", micro}, {"den", kMicro}};
}

nlohmann::ordered_json pair_json(const PairScore& pair) {
  nlohmann::ordered_json out{{"baseline_index", pair.baseline_index},
                             {"candidate_index", pair.candidate_index},
                             {"score", micro_json(pair.score_micro)}};
  if (pair.has_pts) {
    out["pts"] = nlohmann::ordered_json{{"value", pair.pts},
                                        {"tb", nlohmann::ordered_json{{"num", pair.tb_num}, {"den", pair.tb_den}}}};
  }
  return out;
}

// The placeholder the one-sided analyzer left on `fp` for `id` at Scope{video,
// 0}, or null.
Measurement* find_video_measurement(Fingerprint& fp, CheckId id) {
  const std::uint32_t index = static_cast<std::uint32_t>(id);
  for (Measurement& measurement : fp.measurements) {
    if (measurement.check_index == index && measurement.scope.kind == Scope::Kind::video &&
        measurement.scope.index == 0) {
      return &measurement;
    }
  }
  return nullptr;
}

void erase_video_measurement(Fingerprint& fp, CheckId id) {
  const std::uint32_t index = static_cast<std::uint32_t>(id);
  fp.measurements.erase(std::remove_if(fp.measurements.begin(), fp.measurements.end(),
                                       [index](const Measurement& measurement) {
                                         return measurement.check_index == index &&
                                                measurement.scope.kind == Scope::Kind::video &&
                                                measurement.scope.index == 0;
                                       }),
                        fp.measurements.end());
}

void set_skip(Measurement& measurement, SkipReason reason, nlohmann::ordered_json evidence) {
  measurement.value = Absent{};
  measurement.skip_reason = reason;
  measurement.evidence = std::move(evidence);
}

// D-04's decode-path record for one side: the library versions, build triplet
// and CPU flags, then the decoder settings the side decoded under.
std::string decode_path_signature(const TapEnd& end) {
  return compose_decode_path_signature() + " flags/" + end.flags_recorded;
}

// The skips every two-file check shares, written onto both placeholders: a side
// whose decoder could not be opened (requires_decode), and a side whose scan or
// decode stopped early (partial_scan: a prefix's worst frame says nothing about
// the rest). True when it wrote one.
bool apply_common_skips(Measurement& baseline_m, Measurement& candidate_m, const TapEnd& baseline_end,
                        const TapEnd& candidate_end) {
  if (!baseline_end.attempted || !candidate_end.attempted) {
    nlohmann::ordered_json b_evidence = nlohmann::ordered_json::object();
    nlohmann::ordered_json c_evidence = nlohmann::ordered_json::object();
    if (!baseline_end.fallback_reason.empty()) {
      b_evidence["fallback_reason"] = baseline_end.fallback_reason;
    }
    if (!candidate_end.fallback_reason.empty()) {
      c_evidence["fallback_reason"] = candidate_end.fallback_reason;
    }
    set_skip(baseline_m, SkipReason::requires_decode, std::move(b_evidence));
    set_skip(candidate_m, SkipReason::requires_decode, std::move(c_evidence));
    return true;
  }
  if (!baseline_end.complete || !candidate_end.complete) {
    // A prefix's worst frame says nothing about the rest: never a score.
    set_skip(baseline_m, SkipReason::partial_scan,
             nlohmann::ordered_json{{"reason", baseline_end.complete ? std::string("other_side_incomplete")
                                                                        : baseline_end.incomplete_reason}});
    set_skip(candidate_m, SkipReason::partial_scan,
             nlohmann::ordered_json{{"reason", candidate_end.complete ? std::string("other_side_incomplete")
                                                                         : candidate_end.incomplete_reason}});
    return true;
  }
  return false;
}

// Writes the two-file content.video.perceptual measurement into both
// fingerprints (D-01), replacing the one-sided placeholder each carries.
mediadiff::expected<void, Error> assemble_perceptual(const std::string& baseline_path, const PairScorer& scorer,
                                                       const TapEnd& baseline_end, const TapEnd& candidate_end,
                                                       Fingerprint& baseline, Fingerprint& candidate) {
  const CheckId id = CheckId::content_video_perceptual;
  Measurement* baseline_m = find_video_measurement(baseline, id);
  Measurement* candidate_m = find_video_measurement(candidate, id);
  if (baseline_m == nullptr && candidate_m == nullptr) {
    return {};
  }
  if (baseline_m == nullptr || candidate_m == nullptr || !baseline_end.has_primary || !candidate_end.has_primary) {
    // A file with no primary video stream on one side: nothing to score against.
    // The side that has one carries no measurement either (Pitfall 10).
    erase_video_measurement(baseline, id);
    erase_video_measurement(candidate, id);
    return {};
  }

  if (apply_common_skips(*baseline_m, *candidate_m, baseline_end, candidate_end)) {
    return {};
  }

  if (scorer.stop_reason() == PairScorer::StopReason::geometry_mismatch) {
    const std::string b_label =
        std::to_string(kThumbnailWidth) + "x" + std::to_string(scorer.mismatch_baseline_height());
    const std::string c_label =
        std::to_string(kThumbnailWidth) + "x" + std::to_string(scorer.mismatch_candidate_height());
    const nlohmann::ordered_json evidence{{"baseline_thumbnail", b_label}, {"candidate_thumbnail", c_label}};
    set_skip(*baseline_m, SkipReason::geometry_mismatch, evidence);
    set_skip(*candidate_m, SkipReason::geometry_mismatch, evidence);
    return {};
  }
  if (scorer.stop_reason() == PairScorer::StopReason::thumbnail_unavailable ||
      scorer.stop_reason() == PairScorer::StopReason::thumbnail_too_small) {
    const nlohmann::ordered_json evidence{{"reason", std::string(PairScorer::stop_reason_name(scorer.stop_reason()))}};
    set_skip(*baseline_m, SkipReason::insufficient_data, evidence);
    set_skip(*candidate_m, SkipReason::insufficient_data, evidence);
    return {};
  }
  const std::optional<PerceptualSummary> summary = scorer.summary();
  if (!summary.has_value()) {
    const nlohmann::ordered_json evidence{{"reason", std::string("no_pairs_scored")}};
    set_skip(*baseline_m, SkipReason::insufficient_data, evidence);
    set_skip(*candidate_m, SkipReason::insufficient_data, evidence);
    return {};
  }

  auto identity = compute_input_identity(baseline_path);
  if (!identity) {
    return mediadiff::unexpected(identity.error());
  }

  const std::string sampling_state =
      scorer.sample_stride() > 1 ? sampling_state_sampled(scorer.sample_stride()) : std::string(kSamplingStateFull);

  // The baseline's self-score is exactly 1 by construction of the integer
  // formula (a thumbnail against itself scores 1 << 24), so it is recorded as
  // the exact rational, never re-measured.
  baseline_m->value = RationalValue{kMicro, kMicro, Rational{1, 1}};
  baseline_m->skip_reason = SkipReason::none;
  baseline_m->evidence = nlohmann::ordered_json{
      {"self_score", true},
      {"baseline_stream_index", baseline_end.stream_index},
      {"scaler_path", baseline_end.scaler_record},
      {"decode_path_signature", decode_path_signature(baseline_end)},
      {"sampling_state", sampling_state},
  };

  nlohmann::ordered_json worst = nlohmann::ordered_json::array();
  for (const PairScore& pair : summary->worst) {
    worst.push_back(pair_json(pair));
  }
  nlohmann::ordered_json evidence{
      {"reference_identity", identity->xxh3_128},
      {"baseline_stream_index", baseline_end.stream_index},
      {"candidate_stream_index", candidate_end.stream_index},
      {"pairing", std::string(scorer.pairing() == PairingMode::time ? "time" : "index")},
  };
  if (scorer.pairing() == PairingMode::index) {
    evidence["pairing_fallback"] = scorer.pairing_fallback();
  }
  evidence["pairs_scored"] = summary->pairs_scored;
  evidence["unpaired_baseline"] = scorer.unpaired_baseline();
  evidence["unpaired_candidate"] = scorer.unpaired_candidate();
  evidence["unpaired_no_pts"] = scorer.unpaired_no_pts();
  evidence["mean"] = micro_json(summary->mean_micro);
  evidence["first_below_threshold"] =
      summary->first_below.has_value() ? pair_json(*summary->first_below) : nlohmann::ordered_json(nullptr);
  evidence["worst"] = std::move(worst);
  evidence["threshold"] = micro_json(kPerceptualThresholdMicro);
  evidence["scaler_path"] = candidate_end.scaler_record;
  evidence["decode_path_signature"] = decode_path_signature(candidate_end);
  evidence["sampling_state"] = sampling_state;

  candidate_m->value = RationalValue{summary->min_micro, kMicro, Rational{1, 1}};
  candidate_m->skip_reason = SkipReason::none;
  candidate_m->evidence = std::move(evidence);
  return {};
}

nlohmann::ordered_json milli_db_json(std::int64_t milli_db) {
  return nlohmann::ordered_json{{"num", milli_db}, {"den", 1000}};
}

// A VMAF score in evidence: the same {num, den} thousandths the compared value
// uses (97.43 is 97430/1000).
nlohmann::ordered_json thousandths_json(std::int64_t value) {
  return nlohmann::ordered_json{{"num", value}, {"den", kVmafQuantiserDen}};
}

nlohmann::ordered_json quality_point_json(const QualityPoint& point, bool psnr) {
  nlohmann::ordered_json out{{"value", psnr ? milli_db_json(point.value) : micro_json(point.value)},
                             {"baseline_index", point.baseline_index},
                             {"candidate_index", point.candidate_index}};
  if (point.has_pts) {
    out["pts"] = nlohmann::ordered_json{{"value", point.pts},
                                        {"tb", nlohmann::ordered_json{{"num", point.tb_num}, {"den", point.tb_den}}}};
  }
  return out;
}

// Both quality.* checks' evidence keys that are not specific to one metric: the
// path preconditions TRUST-04 compares (07-09's table), the pair's bit depth and
// the sampling record. quality scores are read from native planes, so there is
// no thumbnail scaler to record: `scaler_path` says so, identically on both
// sides.
constexpr const char* kNativeScalerPath = "native (no scaler)";

// Writes the two-file quality.vmaf measurements into both fingerprints once the
// shared prefix of assemble_quality has ruled every skip out (CONTENT-09; D-01,
// D-03). The compared value is the HARMONIC mean (it punishes bad frames); the
// minimum and the arithmetic mean ride in evidence. The baseline's value is its
// COMPUTED self-score -- libvmaf's harmonic mean of the baseline against itself
// (97.43 on identical input, research Q8) -- never an assumed 100, so identical
// media report a delta of exactly 0 after the single quantization to thousandths.
// Both sides carry the model, libvmaf's version and the two TRUST-04 path keys.
mediadiff::expected<void, Error> assemble_vmaf_target(const std::string& baseline_path, const PairScorer& scorer,
                                                        const TapEnd& baseline_end, const TapEnd& candidate_end,
                                                        Measurement& baseline_m, Measurement& candidate_m,
                                                        std::optional<InputIdentity>* identity) {
  const std::optional<VmafSummary>& summary = scorer.vmaf_summary();
  if (!summary.has_value() || summary->pairs_scored == 0) {
    const nlohmann::ordered_json evidence{{"reason", std::string("no_pairs_scored")}};
    set_skip(baseline_m, SkipReason::insufficient_data, evidence);
    set_skip(candidate_m, SkipReason::insufficient_data, evidence);
    return {};
  }
  if (!summary->finite) {
    // A non-finite pooled score is never written to the report (Pitfall 13).
    const nlohmann::ordered_json evidence{{"reason", std::string("non_finite_score")}};
    set_skip(baseline_m, SkipReason::insufficient_data, evidence);
    set_skip(candidate_m, SkipReason::insufficient_data, evidence);
    return {};
  }
  if (!identity->has_value()) {
    auto computed = compute_input_identity(baseline_path);
    if (!computed) {
      return mediadiff::unexpected(computed.error());
    }
    *identity = *computed;
  }
  const std::string model = kVmafModelVersion;
#if defined(MEDIADIFF_WITH_VMAF)
  const std::string version = VmafAccumulator::libvmaf_version();
#else
  // Unreachable on a build without libvmaf: no VmafSummary exists there (the
  // early return above reports no_pairs_scored), and VmafAccumulator is not linked.
  const std::string version;
#endif

  baseline_m.value = RationalValue{summary->self_harmonic, kVmafQuantiserDen, Rational{1, 1}};
  baseline_m.skip_reason = SkipReason::none;
  baseline_m.evidence = nlohmann::ordered_json{
      {"self_score", true},
      {"baseline_stream_index", baseline_end.stream_index},
      {"model", model},
      {"libvmaf_version", version},
      {"scaler_path", std::string(kNativeScalerPath)},
      {"decode_path_signature", decode_path_signature(baseline_end)},
      {"sampling_state", std::string(kSamplingStateFull)},
  };

  nlohmann::ordered_json evidence{
      {"reference_identity", (*identity)->xxh3_128},
      {"baseline_stream_index", baseline_end.stream_index},
      {"candidate_stream_index", candidate_end.stream_index},
      {"model", model},
      {"libvmaf_version", version},
      {"pairing", std::string(scorer.pairing() == PairingMode::time ? "time" : "index")},
  };
  if (scorer.pairing() == PairingMode::index) {
    evidence["pairing_fallback"] = scorer.pairing_fallback();
  }
  evidence["pairs_scored"] = summary->pairs_scored;
  evidence["unpaired_baseline"] = scorer.unpaired_baseline();
  evidence["unpaired_candidate"] = scorer.unpaired_candidate();
  evidence["unpaired_no_pts"] = scorer.unpaired_no_pts();
  evidence["harmonic_mean"] = thousandths_json(summary->harmonic_mean);
  evidence["min"] = thousandths_json(summary->min);
  evidence["mean"] = thousandths_json(summary->mean);
  evidence["bpc"] = scorer.quality_bpc();
  evidence["scaler_path"] = std::string(kNativeScalerPath);
  evidence["decode_path_signature"] = decode_path_signature(candidate_end);
  evidence["sampling_state"] = std::string(kSamplingStateFull);

  candidate_m.value = RationalValue{summary->harmonic_mean, kVmafQuantiserDen, Rational{1, 1}};
  candidate_m.skip_reason = SkipReason::none;
  candidate_m.evidence = std::move(evidence);
  return {};
}

// Writes the two-file quality.psnr / quality.ssim measurements into both
// fingerprints (D-01), replacing the one-sided placeholders each carries.
// CONTENT-08; D-03: the compared value is the floor MEAN over the scored pairs,
// the minimum rides in evidence.
mediadiff::expected<void, Error> assemble_quality(const std::string& baseline_path, const PairScorer& scorer,
                                                    const TapEnd& baseline_end, const TapEnd& candidate_end,
                                                    Fingerprint& baseline, Fingerprint& candidate) {
  enum class Kind { psnr, ssim, vmaf };
  struct Target {
    CheckId id;
    bool requested;
    Kind kind;
  };
  // quality.vmaf (07-11, CONTENT-09) is registered on every build, so its
  // placeholder is replaced or erased here like the others'; it is only ever
  // `requested` in a build that links libvmaf (fingerprint_pair refuses the
  // request otherwise), and its scored measurement is written by
  // assemble_vmaf_target.
  const std::array<Target, 3> targets = {{{CheckId::quality_psnr, scorer.quality_request().psnr, Kind::psnr},
                                          {CheckId::quality_ssim, scorer.quality_request().ssim, Kind::ssim},
                                          {CheckId::quality_vmaf, scorer.quality_request().vmaf, Kind::vmaf}}};
  std::optional<InputIdentity> identity;

  for (const Target& target : targets) {
    Measurement* baseline_m = find_video_measurement(baseline, target.id);
    Measurement* candidate_m = find_video_measurement(candidate, target.id);
    if (baseline_m == nullptr && candidate_m == nullptr) {
      continue;
    }
    if (baseline_m == nullptr || candidate_m == nullptr || !baseline_end.has_primary || !candidate_end.has_primary) {
      erase_video_measurement(baseline, target.id);
      erase_video_measurement(candidate, target.id);
      continue;
    }
    if (!target.requested) {
      // A live compare without the flag: an explicit, named skip on both sides.
      set_skip(*baseline_m, SkipReason::not_requested, nlohmann::ordered_json::object());
      set_skip(*candidate_m, SkipReason::not_requested, nlohmann::ordered_json::object());
      continue;
    }
    if (apply_common_skips(*baseline_m, *candidate_m, baseline_end, candidate_end)) {
      continue;
    }

    // CONTENT-09: VMAF's temporal features need consecutive frames, so a strided
    // frame set is refused outright (the other checks still honour the stride).
    if (target.kind == Kind::vmaf && scorer.sample_stride() >= 2) {
      const nlohmann::ordered_json evidence{{"sample_stride", scorer.sample_stride()}};
      set_skip(*baseline_m, SkipReason::sampling_conflict, evidence);
      set_skip(*candidate_m, SkipReason::sampling_conflict, evidence);
      continue;
    }

    // A geometry the two sides cannot pair (T-07-30): their own latch, naming
    // each side's frame layout.
    if (scorer.quality_stop() == PairScorer::QualityStop::geometry_mismatch) {
      const nlohmann::ordered_json evidence{{"baseline_native", scorer.quality_baseline_label()},
                                            {"candidate_native", scorer.quality_candidate_label()}};
      set_skip(*baseline_m, SkipReason::geometry_mismatch, evidence);
      set_skip(*candidate_m, SkipReason::geometry_mismatch, evidence);
      continue;
    }
    if (scorer.quality_stop() != PairScorer::QualityStop::none) {
      const nlohmann::ordered_json evidence{{"reason", std::string(PairScorer::quality_stop_name(scorer.quality_stop()))},
                                            {"baseline_native", scorer.quality_baseline_label()},
                                            {"candidate_native", scorer.quality_candidate_label()}};
      set_skip(*baseline_m, SkipReason::insufficient_data, evidence);
      set_skip(*candidate_m, SkipReason::insufficient_data, evidence);
      continue;
    }
    // The perceptual consumer stopped (no partner, a stop hook, a thumbnail it
    // could not make): what the quality scorers saw is a prefix, never a score.
    if (scorer.stopped()) {
      const nlohmann::ordered_json evidence{{"reason", std::string("pairing_stopped")},
                                            {"stop", std::string(PairScorer::stop_reason_name(scorer.stop_reason()))}};
      set_skip(*baseline_m, SkipReason::insufficient_data, evidence);
      set_skip(*candidate_m, SkipReason::insufficient_data, evidence);
      continue;
    }
    if (target.kind == Kind::vmaf) {
      // A libvmaf failure is the one thing here that is not a skip: the compare
      // cannot report a score and says so (T-07-36).
      if (scorer.vmaf_error().has_value()) {
        return mediadiff::unexpected(*scorer.vmaf_error());
      }
      if (scorer.vmaf_layout_unsupported()) {
        const nlohmann::ordered_json evidence{{"reason", std::string("unsupported_layout")},
                                              {"baseline_native", scorer.quality_baseline_label()},
                                              {"candidate_native", scorer.quality_candidate_label()}};
        set_skip(*baseline_m, SkipReason::geometry_mismatch, evidence);
        set_skip(*candidate_m, SkipReason::geometry_mismatch, evidence);
        continue;
      }
      if (scorer.vmaf_frame_too_small()) {
        const nlohmann::ordered_json evidence{{"reason", std::string("frame_too_small")},
                                              {"minimum_dimension", kVmafMinDimension}};
        set_skip(*baseline_m, SkipReason::insufficient_data, evidence);
        set_skip(*candidate_m, SkipReason::insufficient_data, evidence);
        continue;
      }
      auto assembled = assemble_vmaf_target(baseline_path, scorer, baseline_end, candidate_end, *baseline_m,
                                            *candidate_m, &identity);
      if (!assembled) {
        return mediadiff::unexpected(assembled.error());
      }
      continue;
    }
    if (target.kind == Kind::ssim && scorer.ssim_frame_too_small()) {
      const nlohmann::ordered_json evidence{{"reason", std::string("frame_too_small")}};
      set_skip(*baseline_m, SkipReason::insufficient_data, evidence);
      set_skip(*candidate_m, SkipReason::insufficient_data, evidence);
      continue;
    }

    std::int64_t pairs_scored = 0;
    std::int64_t mean = 0;
    std::int64_t baseline_value = 0;
    std::int64_t denominator = 0;
    std::int64_t identical_frames = 0;
    nlohmann::ordered_json min_json;
    nlohmann::ordered_json per_plane;
    const bool is_psnr = target.kind == Kind::psnr;
    if (is_psnr) {
      const std::optional<PsnrSummary> summary = scorer.psnr_summary();
      if (summary.has_value()) {
        pairs_scored = summary->pairs_scored;
        mean = summary->mean_milli_db;
        identical_frames = summary->identical_frames;
        min_json = quality_point_json(summary->min, true);
        per_plane = nlohmann::ordered_json::object();
        const std::array<const char*, 3> names = {"y", "u", "v"};
        for (int p = 0; p < summary->plane_count && p < 3; ++p) {
          per_plane[names[static_cast<std::size_t>(p)]] =
              milli_db_json(summary->plane_mean_milli_db[static_cast<std::size_t>(p)]);
        }
      }
      baseline_value = psnr_cap_milli_db(scorer.quality_bpc());
      denominator = 1000;
    } else {
      const std::optional<NativeSsimSummary> summary = scorer.ssim_summary();
      if (summary.has_value()) {
        pairs_scored = summary->pairs_scored;
        mean = summary->mean_micro;
        identical_frames = summary->identical_frames;
        min_json = quality_point_json(summary->min, false);
      }
      baseline_value = kMicro;
      denominator = kMicro;
    }
    if (pairs_scored == 0) {
      const nlohmann::ordered_json evidence{{"reason", std::string("no_pairs_scored")}};
      set_skip(*baseline_m, SkipReason::insufficient_data, evidence);
      set_skip(*candidate_m, SkipReason::insufficient_data, evidence);
      continue;
    }

    if (!identity.has_value()) {
      auto computed = compute_input_identity(baseline_path);
      if (!computed) {
        return mediadiff::unexpected(computed.error());
      }
      identity = *computed;
    }
    const std::string sampling_state =
        scorer.sample_stride() > 1 ? sampling_state_sampled(scorer.sample_stride()) : std::string(kSamplingStateFull);

    // The baseline's self-score is the metric's value for identical frames (the
    // PSNR cap, exactly 1 for SSIM), recorded as the exact rational and never
    // re-measured. Both sides carry the TRUST-04 path keys.
    baseline_m->value = RationalValue{baseline_value, denominator, Rational{1, 1}};
    baseline_m->skip_reason = SkipReason::none;
    baseline_m->evidence = nlohmann::ordered_json{
        {"self_score", true},
        {"baseline_stream_index", baseline_end.stream_index},
        {"bpc", scorer.quality_bpc()},
        {"scaler_path", std::string(kNativeScalerPath)},
        {"decode_path_signature", decode_path_signature(baseline_end)},
        {"sampling_state", sampling_state},
    };

    nlohmann::ordered_json evidence{
        {"reference_identity", identity->xxh3_128},
        {"baseline_stream_index", baseline_end.stream_index},
        {"candidate_stream_index", candidate_end.stream_index},
        {"pairing", std::string(scorer.pairing() == PairingMode::time ? "time" : "index")},
    };
    if (scorer.pairing() == PairingMode::index) {
      evidence["pairing_fallback"] = scorer.pairing_fallback();
    }
    evidence["pairs_scored"] = pairs_scored;
    evidence["unpaired_baseline"] = scorer.unpaired_baseline();
    evidence["unpaired_candidate"] = scorer.unpaired_candidate();
    evidence["unpaired_no_pts"] = scorer.unpaired_no_pts();
    evidence["mean"] = is_psnr ? milli_db_json(mean) : micro_json(mean);
    evidence["min"] = std::move(min_json);
    if (is_psnr) {
      evidence["per_plane"] = std::move(per_plane);
    }
    evidence["identical_frames"] = identical_frames;
    evidence["bpc"] = scorer.quality_bpc();
    evidence["scaler_path"] = std::string(kNativeScalerPath);
    evidence["decode_path_signature"] = decode_path_signature(candidate_end);
    evidence["sampling_state"] = sampling_state;

    candidate_m->value = RationalValue{mean, denominator, Rational{1, 1}};
    candidate_m->skip_reason = SkipReason::none;
    candidate_m->evidence = std::move(evidence);
  }
  return {};
}

// The sequential route: two independent one-sided probes, no tap, exactly what
// two fingerprint_input calls produce for media inputs.
mediadiff::expected<PairResult, Error> probe_sequentially(const std::string& baseline_path,
                                                            const std::string& candidate_path,
                                                            const std::vector<AnalyzerSpec>& analyzers,
                                                            ProbeOptions options) {
  options.frame_tap = nullptr;
  auto baseline = detail::run_probe(baseline_path, analyzers, nullptr, options);
  if (!baseline) {
    return mediadiff::unexpected(baseline.error());
  }
  auto candidate = detail::run_probe(candidate_path, analyzers, nullptr, options);
  if (!candidate) {
    return mediadiff::unexpected(candidate.error());
  }
  return PairResult{std::move(*baseline), std::move(*candidate)};
}

}  // namespace

namespace detail {

mediadiff::expected<PairResult, Error> run_pair_probe(const std::string& baseline_path,
                                                        const std::string& candidate_path,
                                                        const std::vector<AnalyzerSpec>& analyzers,
                                                        const ProbeOptions& options, PairProbeLog* log,
                                                        const QualityRequest& quality) {
  FrameSlot baseline_slot;
  FrameSlot candidate_slot;

  ProducerJob baseline_job;
  baseline_job.path = &baseline_path;
  baseline_job.analyzers = &analyzers;
  baseline_job.options = options;
  baseline_job.options.frame_tap = &baseline_slot;
  baseline_job.slot = &baseline_slot;

  ProducerJob candidate_job;
  candidate_job.path = &candidate_path;
  candidate_job.analyzers = &analyzers;
  candidate_job.options = options;
  candidate_job.options.frame_tap = &candidate_slot;
  candidate_job.slot = &candidate_slot;

  PairScorer scorer(options.sample_stride, log != nullptr ? log->stop_after_scored_pairs : 0, quality);

  {
    ProducerThreads threads(baseline_slot, candidate_slot);
    if (!threads.start(&baseline_job, &candidate_job)) {
      // No thread could be started: the same answer, one side at a time.
      return probe_sequentially(baseline_path, candidate_path, analyzers, options);
    }
    consume(baseline_slot, candidate_slot, scorer);
    // Close both slots (freeing a producer blocked in publish) and join both
    // threads: from here on nothing runs concurrently with this function.
    threads.close_and_join();
  }

  if (log != nullptr) {
    log->baseline_read_frame_calls = baseline_job.stats.read_frame_call_count;
    log->candidate_read_frame_calls = candidate_job.stats.read_frame_call_count;
    log->baseline_max_occupancy = baseline_slot.max_occupancy();
    log->candidate_max_occupancy = candidate_slot.max_occupancy();
    log->baseline_frames_published = baseline_slot.published();
    log->candidate_frames_published = candidate_slot.published();
    log->pairs_paired = scorer.pairs_paired();
    log->pairs_scored = scorer.pairs_scored();
    log->unpaired_baseline = scorer.unpaired_baseline();
    log->unpaired_candidate = scorer.unpaired_candidate();
    log->stopped_early = scorer.stopped();
    log->stop_reason = PairScorer::stop_reason_name(scorer.stop_reason());
  }

  // The first producer error, baseline then candidate.
  if (!baseline_job.result.has_value() || !*baseline_job.result) {
    return mediadiff::unexpected(baseline_job.result.has_value() ? baseline_job.result->error()
                                                                 : Error{ErrorKind::internal, "probe thread did not run"});
  }
  if (!candidate_job.result.has_value() || !*candidate_job.result) {
    return mediadiff::unexpected(candidate_job.result.has_value() ? candidate_job.result->error()
                                                                  : Error{ErrorKind::internal, "probe thread did not run"});
  }

  PairResult result{std::move(**baseline_job.result), std::move(**candidate_job.result)};
  if (options.content_enabled) {
    // 07-11: flush and pool libvmaf (a no-op unless a VMAF build was asked for it).
    scorer.finish_vmaf();
    auto assembled = assemble_perceptual(baseline_path, scorer, baseline_slot.end(), candidate_slot.end(),
                                         result.baseline, result.candidate);
    if (!assembled) {
      return mediadiff::unexpected(assembled.error());
    }
    auto assembled_quality = assemble_quality(baseline_path, scorer, baseline_slot.end(), candidate_slot.end(),
                                              result.baseline, result.candidate);
    if (!assembled_quality) {
      return mediadiff::unexpected(assembled_quality.error());
    }
  }
  return result;
}

}  // namespace detail

mediadiff::expected<PairResult, Error> fingerprint_pair(const std::string& baseline_path,
                                                          const std::string& candidate_path,
                                                          const CheckRegistry& registry,
                                                          const ProbeOptions& options,
                                                          const QualityRequest& quality) {
  if (quality.vmaf && !vmaf_built_in()) {
    // The CLI turns this into a usage error before it gets here; a library
    // caller gets the same refusal rather than a silent not_requested.
    return mediadiff::unexpected(
        Error{ErrorKind::usage, "VMAF scoring requires a build configured with MEDIADIFF_WITH_VMAF=ON"});
  }
  ProbeOptions plain = options;
  plain.frame_tap = nullptr;

  auto baseline_input = detail::resolve_input(baseline_path, registry);
  if (!baseline_input) {
    return mediadiff::unexpected(baseline_input.error());
  }
  const bool baseline_is_media = !baseline_input->has_value();

  auto candidate_input = detail::resolve_input(candidate_path, registry);
  if (!candidate_input) {
    if (baseline_is_media) {
      // fingerprint_input(baseline) ran to completion before the candidate was
      // looked at: a baseline that fails to probe still reports first.
      auto baseline = detail::run_probe(baseline_path, all_analyzers(), nullptr, plain);
      if (!baseline) {
        return mediadiff::unexpected(baseline.error());
      }
    }
    return mediadiff::unexpected(candidate_input.error());
  }
  const bool candidate_is_media = !candidate_input->has_value();

  if (baseline_is_media && candidate_is_media && plain.content_enabled) {
    return detail::run_pair_probe(baseline_path, candidate_path, all_analyzers(), plain, nullptr, quality);
  }

  // Sequential: a snapshot side keeps its read_snapshot short-circuit, a media
  // side is probed one-sidedly (so its perceptual measurement is the honest
  // skipped:requires_media or requires_decode), and `--no-content` decodes
  // nothing.
  PairResult result;
  if (baseline_is_media) {
    auto baseline = detail::run_probe(baseline_path, all_analyzers(), nullptr, plain);
    if (!baseline) {
      return mediadiff::unexpected(baseline.error());
    }
    result.baseline = std::move(*baseline);
  } else {
    result.baseline = std::move(**baseline_input);
  }
  if (candidate_is_media) {
    auto candidate = detail::run_probe(candidate_path, all_analyzers(), nullptr, plain);
    if (!candidate) {
      return mediadiff::unexpected(candidate.error());
    }
    result.candidate = std::move(*candidate);
  } else {
    result.candidate = std::move(**candidate_input);
  }
  return result;
}

}  // namespace mediadiff
