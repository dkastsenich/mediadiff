// 07-11-PLAN.md (CONTENT-09; D-01, D-03; TRUST-04): quality.vmaf.
//
// libvmaf is linked only by a build configured with MEDIADIFF_WITH_VMAF=ON, so
// this file proves two contracts and runs the one its build can:
//   * every build (the first three tests): the id is registered and documented,
//     a live compare without `--vmaf` reports it skipped:not_requested, a
//     snapshot holds it only as skipped:requires_media, and -- on a build WITHOUT
//     libvmaf -- `--vmaf` is a usage error (exit 64) that names the build option;
//   * a VMAF build (the rest): the score itself, against what libvmaf is known to
//     do, with the model pinned and recorded, the baseline self-score computed,
//     `--sample N` refused and the path preconditions enforced.
// A test for the other build's contract is SKIPPED with its reason, never a
// vacuous pass.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/macros.h>
#include <libavutil/pixdesc.h>
#include <libavutil/pixfmt.h>
}

#include <nlohmann/json.hpp>

#if defined(MEDIADIFF_TEST_WITH_VMAF)
#include <libvmaf/libvmaf.h>
#endif

#include "cli_harness.h"
#include "compare/engine.h"
#include "core/model.h"
#include "core/policy.h"
#include "core/registry.h"
#include "probe/lockstep.h"
#include "probe/orchestrator.h"
#include "probe/pair_scorer.h"
#include "probe/vmaf_scorer.h"
#include "support/fixture_paths.h"
#include "util/version.h"

using mediadiff::test::CliResult;
using mediadiff::test::run_cli;

namespace {

namespace fs = std::filesystem;
using json = nlohmann::ordered_json;

constexpr const char* kVmaf = "quality.vmaf";

std::string fixture(const std::string& name) { return mediadiff::test::fixture_dir() + "/" + name; }

std::string scratch(const std::string& name) {
  const fs::path dir = fs::temp_directory_path() / "mediadiff_test_vmaf";
  std::error_code ec;
  fs::create_directories(dir, ec);
  return (dir / name).string();
}

json compare_report(const std::string& a, const std::string& b, const std::vector<std::string>& extra = {},
                    int* exit_code = nullptr) {
  std::vector<std::string> args = {"compare", fixture(a), fixture(b), "--json"};
  args.insert(args.end(), extra.begin(), extra.end());
  const CliResult result = run_cli(args);
  const json report = json::parse(result.out, nullptr, false);
  INFO("stdout: " << result.out << "\nstderr: " << result.err);
  REQUIRE_FALSE(report.is_discarded());
  if (exit_code != nullptr) {
    *exit_code = result.exit_code;
  }
  return report;
}

// The finding for `id`, or null JSON when the report has none.
json finding_of(const json& report, const std::string& id) {
  for (const auto& finding : report.at("findings")) {
    if (finding.at("id") == id) {
      return finding;
    }
  }
  return json(nullptr);
}

std::int64_t num_of(const json& rational) { return rational.at("num").get<std::int64_t>(); }

const char* const kSkipOnDefault = "this build does not link libvmaf (MEDIADIFF_WITH_VMAF is OFF)";

// --- an independent oracle: libvmaf driven directly by the test -------------
//
// The test collects every decoded frame of both fixtures through its own
// FrameTap and feeds libvmaf itself (its own pictures, its own row copies, one
// context, the same model name), so the CLI's numbers are checked against what
// libvmaf says for the same pictures -- not read back from the scorer under test.
// The pairing the oracle assumes (frame i with frame i) is asserted, not assumed.

struct FramePlanes {
  std::int64_t pts = 0;
  int width = 0;
  int height = 0;
  std::vector<std::uint8_t> y;
  std::vector<std::uint8_t> u;
  std::vector<std::uint8_t> v;
};

std::vector<std::uint8_t> copy_plane(const AVFrame& frame, int plane, int w, int h) {
  std::vector<std::uint8_t> out(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
  for (int row = 0; row < h; ++row) {
    std::memcpy(out.data() + static_cast<std::size_t>(row) * static_cast<std::size_t>(w),
                frame.data[plane] + static_cast<std::ptrdiff_t>(row) * frame.linesize[plane],
                static_cast<std::size_t>(w));
  }
  return out;
}

class PlaneTap final : public mediadiff::FrameTap {
 public:
  bool publish(const mediadiff::TappedFrame& tapped) override {
    REQUIRE(tapped.frame != nullptr);
    const AVFrame& frame = *tapped.frame;
    REQUIRE(frame.format == AV_PIX_FMT_YUV420P);
    FramePlanes planes;
    planes.pts = frame.pts;
    planes.width = frame.width;
    planes.height = frame.height;
    planes.y = copy_plane(frame, 0, frame.width, frame.height);
    planes.u = copy_plane(frame, 1, (frame.width + 1) / 2, (frame.height + 1) / 2);
    planes.v = copy_plane(frame, 2, (frame.width + 1) / 2, (frame.height + 1) / 2);
    frames.push_back(std::move(planes));
    return true;
  }
  void finish(const mediadiff::TapEnd&) override {}
  std::vector<FramePlanes> frames;
};

std::vector<FramePlanes> frames_of(const std::string& name) {
  PlaneTap tap;
  mediadiff::ProbeOptions options;
  options.frame_tap = &tap;
  auto fp = mediadiff::detail::run_probe(fixture(name), mediadiff::all_analyzers(), nullptr, options);
  REQUIRE(fp.has_value());
  return std::move(tap.frames);
}

struct OracleScores {
  std::int64_t pairs = 0;
  std::int64_t harmonic = 0;
  std::int64_t min = 0;
  std::int64_t mean = 0;
};

#if defined(MEDIADIFF_TEST_WITH_VMAF)

// round(x * 1000), half away from zero, written out here rather than shared with
// the product.
std::int64_t thousandths(double score) {
  const double scaled = score * 1000.0;
  return static_cast<std::int64_t>(scaled < 0.0 ? -std::floor(-scaled + 0.5) : std::floor(scaled + 0.5));
}

void fill_picture(VmafPicture* picture, const FramePlanes& frame) {
  const std::array<const std::vector<std::uint8_t>*, 3> planes = {&frame.y, &frame.u, &frame.v};
  for (int c = 0; c < 3; ++c) {
    const unsigned w = picture->w[c];
    const unsigned h = picture->h[c];
    const unsigned source_w = c == 0 ? static_cast<unsigned>(frame.width) : (static_cast<unsigned>(frame.width) + 1) / 2;
    for (unsigned row = 0; row < h; ++row) {
      std::memcpy(static_cast<std::uint8_t*>(picture->data[c]) + static_cast<std::ptrdiff_t>(row) * picture->stride[c],
                  planes[static_cast<std::size_t>(c)]->data() + static_cast<std::size_t>(row) * source_w, w);
    }
  }
}

OracleScores oracle_vmaf(const std::vector<FramePlanes>& reference, const std::vector<FramePlanes>& distorted) {
  REQUIRE(reference.size() == distorted.size());
  REQUIRE_FALSE(reference.empty());
  VmafContext* context = nullptr;
  VmafConfiguration config{};
  config.log_level = VMAF_LOG_LEVEL_NONE;
  config.n_threads = 0;
  config.n_subsample = 1;
  REQUIRE(vmaf_init(&context, config) == 0);
  VmafModel* model = nullptr;
  VmafModelConfig model_config{};
  model_config.name = "oracle";
  REQUIRE(vmaf_model_load(&model, &model_config, "vmaf_v0.6.1") == 0);
  REQUIRE(vmaf_use_features_from_model(context, model) == 0);
  for (std::size_t i = 0; i < reference.size(); ++i) {
    REQUIRE(reference[i].width == distorted[i].width);
    REQUIRE(reference[i].height == distorted[i].height);
    REQUIRE(reference[i].pts == distorted[i].pts);
    VmafPicture ref{};
    VmafPicture dist{};
    REQUIRE(vmaf_picture_alloc(&ref, VMAF_PIX_FMT_YUV420P, 8, static_cast<unsigned>(reference[i].width),
                               static_cast<unsigned>(reference[i].height)) == 0);
    REQUIRE(vmaf_picture_alloc(&dist, VMAF_PIX_FMT_YUV420P, 8, static_cast<unsigned>(distorted[i].width),
                               static_cast<unsigned>(distorted[i].height)) == 0);
    fill_picture(&ref, reference[i]);
    fill_picture(&dist, distorted[i]);
    REQUIRE(vmaf_read_pictures(context, &ref, &dist, static_cast<unsigned>(i)) == 0);
  }
  REQUIRE(vmaf_read_pictures(context, nullptr, nullptr, 0) == 0);
  const unsigned last = static_cast<unsigned>(reference.size() - 1);
  OracleScores out;
  out.pairs = static_cast<std::int64_t>(reference.size());
  double harmonic = 0.0;
  double minimum = 0.0;
  double mean = 0.0;
  REQUIRE(vmaf_score_pooled(context, model, VMAF_POOL_METHOD_HARMONIC_MEAN, &harmonic, 0, last) == 0);
  REQUIRE(vmaf_score_pooled(context, model, VMAF_POOL_METHOD_MIN, &minimum, 0, last) == 0);
  REQUIRE(vmaf_score_pooled(context, model, VMAF_POOL_METHOD_MEAN, &mean, 0, last) == 0);
  vmaf_model_destroy(model);
  vmaf_close(context);
  out.harmonic = thousandths(harmonic);
  out.min = thousandths(minimum);
  out.mean = thousandths(mean);
  return out;
}

#else

// A build without libvmaf never reaches an oracle call (every test that needs
// one SKIPs first); this keeps the file one translation unit on every build.
OracleScores oracle_vmaf(const std::vector<FramePlanes>&, const std::vector<FramePlanes>&) { return {}; }

#endif

// --- synthetic frames for the scorer-level cases ---------------------------

struct FrameDeleter {
  void operator()(AVFrame* f) const { av_frame_free(&f); }
};
using FramePtr = std::unique_ptr<AVFrame, FrameDeleter>;

int pattern(int x, int y, int plane) { return (x * 7 + y * 13 + plane * 29 + (x * y) % 11) & 255; }

// A frame of `format` whose samples are pattern() << `extra_shift` (so a 10-bit
// frame holds the 8-bit content shifted left by two). Plane extents come from the
// format's own chroma subsampling.
FramePtr make_frame(AVPixelFormat format, int w, int h, int extra_shift = 0) {
  FramePtr frame(av_frame_alloc());
  frame->format = format;
  frame->width = w;
  frame->height = h;
  REQUIRE(av_frame_get_buffer(frame.get(), 0) == 0);
  REQUIRE(av_frame_make_writable(frame.get()) == 0);
  const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(format);
  REQUIRE(desc != nullptr);
  const bool wide = desc->comp[0].depth > 8;
  for (int p = 0; p < desc->nb_components; ++p) {
    const int pw = p == 0 ? w : AV_CEIL_RSHIFT(w, desc->log2_chroma_w);
    const int ph = p == 0 ? h : AV_CEIL_RSHIFT(h, desc->log2_chroma_h);
    for (int y = 0; y < ph; ++y) {
      for (int x = 0; x < pw; ++x) {
        const int v = pattern(x, y, p) << extra_shift;
        if (wide) {
          frame->data[p][y * frame->linesize[p] + 2 * x] = static_cast<std::uint8_t>(v & 0xFF);
          frame->data[p][y * frame->linesize[p] + 2 * x + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
        } else {
          frame->data[p][y * frame->linesize[p] + x] = static_cast<std::uint8_t>(v);
        }
      }
    }
  }
  return frame;
}

mediadiff::Thumbnail thumbnail() {
  mediadiff::Thumbnail t;
  t.width = 128;
  t.height = 96;
  t.pixels.assign(static_cast<std::size_t>(t.width) * static_cast<std::size_t>(t.height), 100);
  return t;
}

mediadiff::TappedFrame tapped(const AVFrame* frame, const mediadiff::Thumbnail* thumb, std::int64_t index) {
  mediadiff::TappedFrame f;
  f.decode_index = index;
  f.has_pts = true;
  f.pts = index * 40;
  f.tb_num = 1;
  f.tb_den = 1000;
  f.interval_num = 1;
  f.interval_den = 25;
  f.thumbnail = thumb;
  f.frame = frame;
  return f;
}

// Scores two consecutive pairs of (baseline, candidate) through the real
// PairScorer with VMAF requested, then pools.
mediadiff::PairScorer score_two_pairs(const AVFrame* baseline, const AVFrame* candidate) {
  const mediadiff::Thumbnail thumb = thumbnail();
  mediadiff::QualityRequest request;
  request.vmaf = true;
  mediadiff::PairScorer scorer(1, 0, request);
  for (std::int64_t index = 0; index < 2; ++index) {
    REQUIRE(scorer.step(tapped(baseline, &thumb, index), tapped(candidate, &thumb, index)) ==
            mediadiff::PairScorer::Action::advance_both);
  }
  scorer.finish_vmaf();
  return scorer;
}

}  // namespace

TEST_CASE("vmaf - default build usage error", "[integration]") {
  if (mediadiff::vmaf_built_in()) {
    SKIP("this build links libvmaf; the default-build usage error is the contract of a build without it");
  }
  const CliResult result =
      run_cli({"compare", fixture("video_hash_base.mp4"), fixture("video_hash_base.ts"), "--vmaf"});
  INFO("stdout: " << result.out << "\nstderr: " << result.err);
  CHECK(result.exit_code == 64);
  // Names the option that turns it on, never a silent skip.
  CHECK(result.err.find("MEDIADIFF_WITH_VMAF") != std::string::npos);
  CHECK(result.err.find("--vmaf") != std::string::npos);
  // dir has the same flag and the same refusal.
  const CliResult dir = run_cli({"dir", fixture("."), fixture("."), "--content", "--vmaf"});
  CHECK(dir.exit_code == 64);
  CHECK(dir.err.find("MEDIADIFF_WITH_VMAF") != std::string::npos);
}

TEST_CASE("vmaf - registered and not requested", "[integration]") {
  // Registered on EVERY build, so a config or snapshot that names the id never
  // breaks.
  const CliResult listed = run_cli({"list-checks"});
  REQUIRE(listed.exit_code == 0);
  CHECK(listed.out.find("quality.vmaf  group=quality  semantic=tol  unit=score") != std::string::npos);

  const CliResult explained = run_cli({"explain", kVmaf});
  REQUIRE(explained.exit_code == 0);
  CHECK(explained.out.find("vmaf_v0.6.1") != std::string::npos);
  CHECK(explained.out.find("MEDIADIFF_WITH_VMAF") != std::string::npos);

  // A live compare without the flag: an explicit, named skip on both sides.
  const json report = compare_report("video_hash_base.mp4", "video_hash_base.ts");
  const json finding = finding_of(report, kVmaf);
  REQUIRE_FALSE(finding.is_null());
  CHECK(finding.at("status") == "skipped");
  CHECK(finding.at("skip_reason") == "not_requested");
  CHECK(finding.at("baseline").is_null());
  CHECK(finding.at("candidate").is_null());
}

TEST_CASE("vmaf - one sided", "[integration]") {
  // A snapshot never stores a VMAF score (D-01): the id is held only as the
  // honest "not measured here" skip.
  const std::string snap = scratch("one_sided.snap.json");
  const CliResult taken = run_cli({"snapshot", fixture("video_hash_base.mp4"), "--out", snap, "--force"});
  INFO("snapshot stderr: " << taken.err);
  REQUIRE(taken.exit_code == 0);

  std::ifstream in(snap);
  const json doc = json::parse(in, nullptr, false);
  REQUIRE_FALSE(doc.is_discarded());
  int seen = 0;
  for (const auto& measurement : doc.at("measurements")) {
    if (measurement.at("id") != kVmaf) {
      continue;
    }
    ++seen;
    INFO("snapshot entry: " << measurement.dump());
    CHECK(measurement.at("skip_reason") == "requires_media");
    CHECK(measurement.at("value").is_null());
    CHECK(measurement.at("evidence").empty());
  }
  CHECK(seen == 1);

  // The snapshot as either side of a compare is the same skip, never a stored
  // score presented as a current one -- and `--vmaf` does not change that.
  const CliResult against = run_cli({"compare", fixture("video_hash_base.mp4"), snap, "--json"});
  REQUIRE(against.exit_code == 0);
  const json report = json::parse(against.out, nullptr, false);
  REQUIRE_FALSE(report.is_discarded());
  const json finding = finding_of(report, kVmaf);
  REQUIRE_FALSE(finding.is_null());
  CHECK(finding.at("status") == "skipped");
  CHECK(finding.at("skip_reason") == "requires_media");
}

// --- every build: the arithmetic that turns libvmaf's double into the check's integer ---

TEST_CASE("vmaf - score quantisation", "[integration]") {
  using mediadiff::kVmafQuantiserDen;
  using mediadiff::quantize_vmaf_score;
  REQUIRE(kVmafQuantiserDen == 1000);
  // Exactly representable half cases: half away from zero, in both directions.
  CHECK(quantize_vmaf_score(97.4375) == 97438);   // 97437.5
  CHECK(quantize_vmaf_score(-97.4375) == -97438);
  CHECK(quantize_vmaf_score(97.4374) == 97437);
  CHECK(quantize_vmaf_score(100.0) == 100000);
  CHECK(quantize_vmaf_score(0.0) == 0);
  // A score that is not a finite number is never an integer (Pitfall 13): the caller
  // reports skipped:insufficient_data.
  CHECK_FALSE(quantize_vmaf_score(std::numeric_limits<double>::quiet_NaN()).has_value());
  CHECK_FALSE(quantize_vmaf_score(std::numeric_limits<double>::infinity()).has_value());
  CHECK_FALSE(quantize_vmaf_score(-std::numeric_limits<double>::infinity()).has_value());
  CHECK_FALSE(quantize_vmaf_score(1e300).has_value());
}

TEST_CASE("vmaf - a library request on a build without libvmaf is refused, not skipped", "[integration]") {
  if (mediadiff::vmaf_built_in()) {
    SKIP("this build links libvmaf; the refusal is the contract of a build without it");
  }
  mediadiff::QualityRequest request;
  request.vmaf = true;
  auto pair = mediadiff::fingerprint_pair(fixture("video_hash_base.mp4"), fixture("video_hash_base.ts"),
                                          mediadiff::builtin_registry(), mediadiff::ProbeOptions{}, request);
  REQUIRE_FALSE(pair.has_value());
  CHECK(pair.error().kind == mediadiff::ErrorKind::usage);
  CHECK(pair.error().message.find("MEDIADIFF_WITH_VMAF") != std::string::npos);
}

// --- a build with libvmaf ---------------------------------------------------

TEST_CASE("vmaf - identical media", "[integration]") {
  if (!mediadiff::vmaf_built_in()) {
    SKIP(kSkipOnDefault);
  }
  const json report = compare_report("video_hash_base.mp4", "video_hash_base.ts", {"--vmaf"});
  const json finding = finding_of(report, kVmaf);
  REQUIRE_FALSE(finding.is_null());
  INFO("finding: " << finding.dump());
  CHECK(finding.at("status") == "pass");

  // The baseline's value is COMPUTED (libvmaf of the baseline against itself), and a
  // stream-copy remux decodes to identical pixels, so the two sides are the same
  // number -- whatever libvmaf returns for this clip (research Q8: not 100).
  const json& baseline = finding.at("baseline");
  const json& candidate = finding.at("candidate");
  REQUIRE_FALSE(baseline.is_null());
  REQUIRE_FALSE(candidate.is_null());
  CHECK(num_of(baseline) == num_of(candidate));
  CHECK(baseline.at("den") == 1000);
  CHECK(candidate.at("den") == 1000);
  CHECK(num_of(baseline) > 0);
  CHECK(num_of(baseline) <= 100000);

  const json& cand = finding.at("evidence").at("candidate");
  const json& base = finding.at("evidence").at("baseline");
  CHECK(cand.at("model") == "vmaf_v0.6.1");
  CHECK(base.at("model") == "vmaf_v0.6.1");
  CHECK_FALSE(cand.at("libvmaf_version").get<std::string>().empty());
  CHECK(cand.at("libvmaf_version") == base.at("libvmaf_version"));
  CHECK(base.at("self_score") == true);
  CHECK(cand.contains("reference_identity"));
  CHECK(cand.at("pairing") == "index");
  CHECK(cand.at("sampling_state") == "full");
  CHECK(num_of(cand.at("harmonic_mean")) == num_of(candidate));

  // The independent oracle: libvmaf driven directly by this test over the same
  // decoded pictures. The computed self-score IS libvmaf's own harmonic mean of the
  // baseline against itself.
  const std::vector<FramePlanes> base_frames = frames_of("video_hash_base.mp4");
  const std::vector<FramePlanes> copy_frames = frames_of("video_hash_base.ts");
  REQUIRE(base_frames.size() == copy_frames.size());
  const OracleScores oracle = oracle_vmaf(base_frames, base_frames);
  CHECK(oracle.pairs == cand.at("pairs_scored").get<std::int64_t>());
  CHECK(oracle.harmonic == num_of(baseline));
  CHECK(oracle.harmonic == num_of(candidate));
  CHECK(oracle.min == num_of(cand.at("min")));
  CHECK(oracle.mean == num_of(cand.at("mean")));
}

TEST_CASE("vmaf - degraded", "[integration]") {
  if (!mediadiff::vmaf_built_in()) {
    SKIP(kSkipOnDefault);
  }
  int exit_code = 0;
  const json report = compare_report("video_hash_base.mp4", "video_perc_degraded.mp4", {"--vmaf"}, &exit_code);
  const json finding = finding_of(report, kVmaf);
  REQUIRE_FALSE(finding.is_null());
  INFO("finding: " << finding.dump());
  const std::int64_t baseline = num_of(finding.at("baseline"));
  const std::int64_t candidate = num_of(finding.at("candidate"));
  // More than the 0.5 point tolerance (500 thousandths) below the self-score.
  CHECK(candidate < baseline - 500);
  CHECK(finding.at("status") == "fail");
  CHECK(exit_code != 0);

  const json& evidence = finding.at("evidence").at("candidate");
  CHECK(evidence.contains("min"));
  CHECK(evidence.contains("mean"));
  // The harmonic mean is the gate, and it never exceeds the arithmetic mean.
  CHECK(num_of(evidence.at("harmonic_mean")) == candidate);
  CHECK(num_of(evidence.at("harmonic_mean")) <= num_of(evidence.at("mean")));
  CHECK(num_of(evidence.at("min")) <= num_of(evidence.at("harmonic_mean")));

  // Against libvmaf itself, fed the same decoded pictures.
  const OracleScores oracle = oracle_vmaf(frames_of("video_hash_base.mp4"), frames_of("video_perc_degraded.mp4"));
  CHECK(oracle.pairs == evidence.at("pairs_scored").get<std::int64_t>());
  CHECK(oracle.harmonic == candidate);
  CHECK(oracle.min == num_of(evidence.at("min")));
  CHECK(oracle.mean == num_of(evidence.at("mean")));
  const OracleScores self = oracle_vmaf(frames_of("video_hash_base.mp4"), frames_of("video_hash_base.mp4"));
  CHECK(self.harmonic == baseline);
}

TEST_CASE("vmaf - sampling conflict", "[integration]") {
  if (!mediadiff::vmaf_built_in()) {
    SKIP(kSkipOnDefault);
  }
  const json sampled = compare_report("video_hash_base.mp4", "video_perc_degraded.mp4", {"--vmaf", "--sample", "2"});
  const json finding = finding_of(sampled, kVmaf);
  REQUIRE_FALSE(finding.is_null());
  INFO("finding: " << finding.dump());
  CHECK(finding.at("status") == "skipped");
  CHECK(finding.at("skip_reason") == "sampling_conflict");
  CHECK(finding.at("baseline").is_null());
  CHECK(finding.at("candidate").is_null());
  // The other checks still honour the stride.
  const json perceptual = finding_of(sampled, "content.video.perceptual");
  REQUIRE_FALSE(perceptual.is_null());
  CHECK(perceptual.at("evidence").at("candidate").at("sampling_state") == "sampled:2");

  // --sample 1 is full: it scores normally.
  const json full = compare_report("video_hash_base.mp4", "video_perc_degraded.mp4", {"--vmaf", "--sample", "1"});
  const json scored = finding_of(full, kVmaf);
  REQUIRE_FALSE(scored.is_null());
  CHECK(scored.at("skip_reason") == "none");
  CHECK_FALSE(scored.at("candidate").is_null());
}

TEST_CASE("vmaf - geometry", "[integration]") {
  if (!mediadiff::vmaf_built_in()) {
    SKIP(kSkipOnDefault);
  }
  const json report = compare_report("video_hash_base.mp4", "video_perc_upscaled.mp4", {"--vmaf"});
  const json finding = finding_of(report, kVmaf);
  REQUIRE_FALSE(finding.is_null());
  INFO("finding: " << finding.dump());
  CHECK(finding.at("status") == "skipped");
  CHECK(finding.at("skip_reason") == "geometry_mismatch");
  // The perceptual score still runs on its thumbnails.
  const json perceptual = finding_of(report, "content.video.perceptual");
  REQUIRE_FALSE(perceptual.is_null());
  CHECK(perceptual.at("status") != "skipped");
}

TEST_CASE("vmaf - deterministic", "[integration]") {
  if (!mediadiff::vmaf_built_in()) {
    SKIP(kSkipOnDefault);
  }
  const std::vector<std::string> args = {"compare", fixture("video_hash_base.mp4"), fixture("video_perc_degraded.mp4"),
                                         "--vmaf", "--json"};
  const CliResult first = run_cli(args);
  const CliResult second = run_cli(args);
  REQUIRE_FALSE(first.out.empty());
  CHECK(first.out == second.out);
  CHECK(first.exit_code == second.exit_code);
}

TEST_CASE("vmaf - snapshot sides are requires_media even with the flag", "[integration]") {
  if (!mediadiff::vmaf_built_in()) {
    SKIP(kSkipOnDefault);
  }
  const std::string snap = scratch("flagged.snap.json");
  const CliResult taken = run_cli({"snapshot", fixture("video_hash_base.mp4"), "--out", snap, "--force"});
  REQUIRE(taken.exit_code == 0);
  const CliResult against = run_cli({"compare", fixture("video_hash_base.mp4"), snap, "--vmaf", "--json"});
  INFO("stderr: " << against.err);
  const json report = json::parse(against.out, nullptr, false);
  REQUIRE_FALSE(report.is_discarded());
  const json finding = finding_of(report, kVmaf);
  REQUIRE_FALSE(finding.is_null());
  CHECK(finding.at("status") == "skipped");
  CHECK(finding.at("skip_reason") == "requires_media");
}

TEST_CASE("vmaf - TRUST-04 path preconditions", "[integration]") {
  if (!mediadiff::vmaf_built_in()) {
    SKIP(kSkipOnDefault);
  }
  // quality.vmaf carries BOTH path keys on BOTH sides, so 07-09's generic table in
  // compare_tol guards it exactly as it guards quality.psnr and quality.ssim (D-04):
  // a differing decode_path_signature, or a key on one side only, is
  // skipped:path_incomparable even though the two scores are identical and inside
  // tolerance.
  const mediadiff::CheckRegistry& registry = mediadiff::builtin_registry();
  mediadiff::QualityRequest request;
  request.vmaf = true;
  auto pair = mediadiff::fingerprint_pair(fixture("video_hash_base.mp4"), fixture("video_hash_base.ts"), registry,
                                          mediadiff::ProbeOptions{}, request);
  REQUIRE(pair.has_value());

  const std::uint32_t id = *registry.find(kVmaf);
  const auto measurement_of = [id](mediadiff::Fingerprint& fp) -> mediadiff::Measurement& {
    const auto it = std::find_if(fp.measurements.begin(), fp.measurements.end(),
                                 [id](const mediadiff::Measurement& m) { return m.check_index == id; });
    INFO("check index " << id);
    REQUIRE(it != fp.measurements.end());
    return *it;
  };
  for (mediadiff::Fingerprint* fp : {&pair->baseline, &pair->candidate}) {
    const mediadiff::Measurement& m = measurement_of(*fp);
    REQUIRE(m.skip_reason == mediadiff::SkipReason::none);
    CHECK(m.evidence.contains("scaler_path"));
    CHECK(m.evidence.contains("decode_path_signature"));
    CHECK_FALSE(m.evidence.at("decode_path_signature").get<std::string>().empty());
    CHECK(m.evidence.contains("model"));
    CHECK(m.evidence.contains("libvmaf_version"));
  }
  CHECK(measurement_of(pair->baseline).evidence.at("decode_path_signature") ==
        measurement_of(pair->candidate).evidence.at("decode_path_signature"));

  auto policy = mediadiff::resolve_policy(registry, mediadiff::ProfileId::sw_encoder);
  REQUIRE(policy.has_value());
  const auto outcome = [&](const mediadiff::Fingerprint& b, const mediadiff::Fingerprint& c) {
    auto findings = mediadiff::compare_fingerprints(b, c, *policy, registry);
    REQUIRE(findings.has_value());
    const auto it = std::find_if(findings->begin(), findings->end(),
                                 [](const mediadiff::Finding& f) { return f.id == kVmaf; });
    INFO("no quality.vmaf finding");
    REQUIRE(it != findings->end());
    return std::make_pair(it->status, it->skip_reason);
  };

  // Unmodified: pass.
  {
    const auto [status, reason] = outcome(pair->baseline, pair->candidate);
    CHECK(status == mediadiff::Status::pass);
    CHECK(reason == mediadiff::SkipReason::none);
  }
  // A differing decode path on the candidate side.
  {
    mediadiff::Fingerprint candidate = pair->candidate;
    measurement_of(candidate).evidence["decode_path_signature"] = "avcodec/0.0.0 flags/other";
    const auto [status, reason] = outcome(pair->baseline, candidate);
    CHECK(status == mediadiff::Status::skipped);
    CHECK(reason == mediadiff::SkipReason::path_incomparable);
  }
  // A key present on one side only is a mismatch, never assumed equal.
  {
    mediadiff::Fingerprint candidate = pair->candidate;
    measurement_of(candidate).evidence.erase("scaler_path");
    const auto [status, reason] = outcome(pair->baseline, candidate);
    CHECK(status == mediadiff::Status::skipped);
    CHECK(reason == mediadiff::SkipReason::path_incomparable);
  }
}

TEST_CASE("vmaf - layouts and bit depths through the scorer", "[integration]") {
  if (!mediadiff::vmaf_built_in()) {
    SKIP(kSkipOnDefault);
  }
  // Each layout libvmaf accepts scores the same content on both sides identically:
  // the candidate is the baseline, so its harmonic mean is the computed self-score.
  const std::array<std::pair<AVPixelFormat, const char*>, 4> layouts = {{{AV_PIX_FMT_YUV420P, "yuv420p"},
                                                                          {AV_PIX_FMT_YUV422P, "yuv422p"},
                                                                          {AV_PIX_FMT_YUV444P, "yuv444p"},
                                                                          {AV_PIX_FMT_GRAY8, "gray"}}};
  for (const auto& [format, name] : layouts) {
    INFO("layout " << name);
    const FramePtr frame = make_frame(format, 64, 48);
    const mediadiff::PairScorer scorer = score_two_pairs(frame.get(), frame.get());
    REQUIRE_FALSE(scorer.vmaf_error().has_value());
    REQUIRE(scorer.vmaf_summary().has_value());
    CHECK(scorer.vmaf_summary()->finite);
    CHECK(scorer.vmaf_summary()->pairs_scored == 2);
    CHECK(scorer.vmaf_summary()->harmonic_mean == scorer.vmaf_summary()->self_harmonic);
    CHECK(scorer.vmaf_summary()->harmonic_mean > 0);
  }

  // 8-bit against the same content at 10 bits: promoted by an exact left shift,
  // so the two sides are identical pictures at libvmaf's 10 bits.
  {
    const FramePtr eight = make_frame(AV_PIX_FMT_YUV420P, 64, 48);
    const FramePtr ten = make_frame(AV_PIX_FMT_YUV420P10LE, 64, 48, 2);
    const mediadiff::PairScorer scorer = score_two_pairs(eight.get(), ten.get());
    REQUIRE_FALSE(scorer.vmaf_error().has_value());
    REQUIRE(scorer.vmaf_summary().has_value());
    CHECK(scorer.quality_bpc() == 10);
    CHECK(scorer.vmaf_summary()->harmonic_mean == scorer.vmaf_summary()->self_harmonic);
    // Non-vacuity: a genuinely different 10-bit picture scores lower than itself.
    const FramePtr different = make_frame(AV_PIX_FMT_YUV420P10LE, 64, 48, 1);
    const mediadiff::PairScorer lower = score_two_pairs(eight.get(), different.get());
    REQUIRE(lower.vmaf_summary().has_value());
    CHECK(lower.vmaf_summary()->harmonic_mean < lower.vmaf_summary()->self_harmonic);
  }

  // A layout libvmaf has no format for (4:1:1) is not converted: it is the
  // geometry_mismatch latch, and PSNR/SSIM are not asked for here.
  {
    const FramePtr odd = make_frame(AV_PIX_FMT_YUV411P, 64, 48);
    const mediadiff::PairScorer scorer = score_two_pairs(odd.get(), odd.get());
    CHECK(scorer.vmaf_layout_unsupported());
    CHECK_FALSE(scorer.vmaf_error().has_value());
    CHECK_FALSE(scorer.vmaf_summary().has_value());
  }

  // Different dimensions: the shared quality latch stops VMAF before any copy.
  {
    const FramePtr small = make_frame(AV_PIX_FMT_YUV420P, 64, 48);
    const FramePtr large = make_frame(AV_PIX_FMT_YUV420P, 128, 96);
    const mediadiff::PairScorer scorer = score_two_pairs(small.get(), large.get());
    CHECK(scorer.quality_stop() == mediadiff::PairScorer::QualityStop::geometry_mismatch);
    CHECK_FALSE(scorer.vmaf_summary().has_value());
  }
}

TEST_CASE("vmaf - frames below libvmaf's minimum size are skipped, never fed to it", "[integration]") {
  if (!mediadiff::vmaf_built_in()) {
    SKIP(kSkipOnDefault);
  }
  // Measured against libvmaf 3.2.0 on this build: a picture 16 pixels or smaller in
  // either dimension aborts inside libvmaf's own allocator ("malloc(): invalid size"),
  // and 17 and above is clean (also under MALLOC_CHECK_=3). The scorer therefore
  // never hands libvmaf such a picture: it latches frame_too_small and the check
  // reports skipped:insufficient_data.
  REQUIRE(mediadiff::kVmafMinDimension == 17);
  for (const auto& [w, h] : {std::pair<int, int>{16, 64}, {64, 16}, {16, 16}, {8, 8}, {2, 2}}) {
    INFO("size " << w << "x" << h);
    const FramePtr frame = make_frame(AV_PIX_FMT_YUV420P, w, h);
    const mediadiff::PairScorer scorer = score_two_pairs(frame.get(), frame.get());
    CHECK(scorer.vmaf_frame_too_small());
    CHECK_FALSE(scorer.vmaf_error().has_value());
    CHECK_FALSE(scorer.vmaf_summary().has_value());
  }
  // The smallest size libvmaf accepts scores.
  for (const auto& [w, h] : {std::pair<int, int>{17, 17}, {17, 40}, {40, 17}}) {
    INFO("size " << w << "x" << h);
    const FramePtr frame = make_frame(AV_PIX_FMT_YUV420P, w, h);
    const mediadiff::PairScorer scorer = score_two_pairs(frame.get(), frame.get());
    CHECK_FALSE(scorer.vmaf_frame_too_small());
    REQUIRE_FALSE(scorer.vmaf_error().has_value());
    REQUIRE(scorer.vmaf_summary().has_value());
    CHECK(scorer.vmaf_summary()->finite);
  }
}
