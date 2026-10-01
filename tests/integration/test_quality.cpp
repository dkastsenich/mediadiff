// 07-10-PLAN.md (CONTENT-08; D-01, D-03, D-04, D-08; TRUST-04): quality.psnr and
// quality.ssim through the real CLI and the in-process lockstep.
//
// The numbers of a live comparison are checked against an INDEPENDENT oracle:
// the test collects every decoded frame's planes of both fixtures through its own
// FrameTap and recomputes each pair's PSNR and SSIM in floating point (which
// exists only here -- the product is integer end to end), so nothing below reads
// an expected value back from the scorer it tests. The fixtures' truth by
// construction (scripts/gen_corpus.sh):
//   * video_hash_base.mp4 and video_hash_base.ts decode to identical pixels (the
//     TS is a stream copy): every pair scores the cap / exactly 1;
//   * video_perc_degraded.mp4 is the base picture scaled to 88x72, back up to
//     352x288 and encoded at -q:v 31, so the detail is really gone;
//   * video_perc_upscaled.mp4 is the base at 704x576: native resolution differs.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

#include <nlohmann/json.hpp>

#include "cli_harness.h"
#include "compare/engine.h"
#include "core/model.h"
#include "core/policy.h"
#include "core/registry.h"
#include "core/value.h"
#include "probe/lockstep.h"
#include "probe/orchestrator.h"
#include "probe/pair_scorer.h"
#include "probe/video_thumbnail.h"
#include "support/fixture_paths.h"
#include "util/quality_math.h"

using mediadiff::test::CliResult;
using mediadiff::test::run_cli;

namespace {

using json = nlohmann::ordered_json;

constexpr const char* kPsnr = "quality.psnr";
constexpr const char* kSsim = "quality.ssim";
constexpr std::int64_t kMicro = 1000000;

std::string fixture(const std::string& name) { return mediadiff::test::fixture_dir() + "/" + name; }

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

const json& candidate_evidence(const json& finding) {
  REQUIRE_FALSE(finding.is_null());
  return finding.at("evidence").at("candidate");
}

std::int64_t num_of(const json& rational) { return rational.at("num").get<std::int64_t>(); }

// --- the independent oracle -------------------------------------------------

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

// Collects every 8-bit yuv420p frame's three planes, in decode order.
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

double psnr_db(double sse, double samples) {
  if (sse == 0.0) {
    return 60.0;  // the 8-bit cap
  }
  return std::min(60.0, 10.0 * std::log10(255.0 * 255.0 * samples / sse));
}

double plane_sse_of(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b) {
  REQUIRE(a.size() == b.size());
  double sse = 0.0;
  for (std::size_t i = 0; i < a.size(); ++i) {
    const double d = static_cast<double>(a[i]) - static_cast<double>(b[i]);
    sse += d * d;
  }
  return sse;
}

// The textbook SSIM of the luma planes over the same 8x8 windows stepped by 4.
double luma_ssim(const FramePlanes& a, const FramePlanes& b) {
  const double c1 = (0.01 * 255.0) * (0.01 * 255.0);
  const double c2 = (0.03 * 255.0) * (0.03 * 255.0);
  double total = 0.0;
  int windows = 0;
  for (int wy = 0; wy + 8 <= a.height; wy += 4) {
    for (int wx = 0; wx + 8 <= a.width; wx += 4) {
      double ma = 0.0;
      double mb = 0.0;
      for (int dy = 0; dy < 8; ++dy) {
        for (int dx = 0; dx < 8; ++dx) {
          const std::size_t i = static_cast<std::size_t>((wy + dy) * a.width + wx + dx);
          ma += a.y[i];
          mb += b.y[i];
        }
      }
      ma /= 64.0;
      mb /= 64.0;
      double va = 0.0;
      double vb = 0.0;
      double cov = 0.0;
      for (int dy = 0; dy < 8; ++dy) {
        for (int dx = 0; dx < 8; ++dx) {
          const std::size_t i = static_cast<std::size_t>((wy + dy) * a.width + wx + dx);
          va += (a.y[i] - ma) * (a.y[i] - ma);
          vb += (b.y[i] - mb) * (b.y[i] - mb);
          cov += (a.y[i] - ma) * (b.y[i] - mb);
        }
      }
      va /= 64.0;
      vb /= 64.0;
      cov /= 64.0;
      total += ((2.0 * ma * mb + c1) * (2.0 * cov + c2)) / ((ma * ma + mb * mb + c1) * (va + vb + c2));
      ++windows;
    }
  }
  return total / windows;
}

struct Oracle {
  std::vector<double> psnr_mdb;               // combined, per pair
  std::array<std::vector<double>, 3> plane_mdb;  // Y, U, V per pair
  std::vector<double> ssim;                   // per pair
  std::vector<std::int64_t> pts;              // the baseline frame's PTS
};

Oracle oracle(const std::string& baseline, const std::string& candidate) {
  const std::vector<FramePlanes> a = frames_of(baseline);
  const std::vector<FramePlanes> b = frames_of(candidate);
  REQUIRE(a.size() == b.size());
  REQUIRE_FALSE(a.empty());
  Oracle out;
  for (std::size_t i = 0; i < a.size(); ++i) {
    REQUIRE(a[i].width == b[i].width);
    REQUIRE(a[i].height == b[i].height);
    const double sy = plane_sse_of(a[i].y, b[i].y);
    const double su = plane_sse_of(a[i].u, b[i].u);
    const double sv = plane_sse_of(a[i].v, b[i].v);
    const double ny = static_cast<double>(a[i].y.size());
    const double nu = static_cast<double>(a[i].u.size());
    out.psnr_mdb.push_back(1000.0 * psnr_db(sy + su + sv, ny + nu + nu));
    out.plane_mdb[0].push_back(1000.0 * psnr_db(sy, ny));
    out.plane_mdb[1].push_back(1000.0 * psnr_db(su, nu));
    out.plane_mdb[2].push_back(1000.0 * psnr_db(sv, nu));
    out.ssim.push_back(luma_ssim(a[i], b[i]));
    out.pts.push_back(a[i].pts);
  }
  return out;
}

double mean_of(const std::vector<double>& values) {
  double sum = 0.0;
  for (const double v : values) {
    sum += v;
  }
  return sum / static_cast<double>(values.size());
}

// --- synthetic frames for the scorer-level cases ---------------------------

struct FrameDeleter {
  void operator()(AVFrame* f) const { av_frame_free(&f); }
};
using FramePtr = std::unique_ptr<AVFrame, FrameDeleter>;

int pattern(int x, int y, int plane) { return (x * 7 + y * 13 + plane * 29 + (x * y) % 11) & 255; }

FramePtr make_frame(AVPixelFormat format, int w, int h) {
  FramePtr frame(av_frame_alloc());
  frame->format = format;
  frame->width = w;
  frame->height = h;
  REQUIRE(av_frame_get_buffer(frame.get(), 0) == 0);
  REQUIRE(av_frame_make_writable(frame.get()) == 0);
  return frame;
}

// 8-bit yuv420p, or 10-bit little-endian yuv420p10le holding the SAME content
// shifted left by two (so promoting the 8-bit side is exact). `bump` adds a
// value to the first four luma samples of the 10-bit side (a controlled
// difference).
FramePtr yuv420(bool ten_bit, int w, int h, int bump = 0) {
  FramePtr frame = make_frame(ten_bit ? AV_PIX_FMT_YUV420P10LE : AV_PIX_FMT_YUV420P, w, h);
  for (int p = 0; p < 3; ++p) {
    const int pw = p == 0 ? w : (w + 1) / 2;
    const int ph = p == 0 ? h : (h + 1) / 2;
    for (int y = 0; y < ph; ++y) {
      for (int x = 0; x < pw; ++x) {
        int v = pattern(x, y, p);
        if (ten_bit) {
          v = (v << 2) + ((p == 0 && y == 0 && x < 4) ? bump : 0);
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

mediadiff::Thumbnail thumbnail(int height = 96) {
  mediadiff::Thumbnail t;
  t.width = 128;
  t.height = height;
  t.pixels.resize(static_cast<std::size_t>(t.width) * static_cast<std::size_t>(height));
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < t.width; ++x) {
      t.pixels[static_cast<std::size_t>(y) * 128 + static_cast<std::size_t>(x)] =
          static_cast<std::uint8_t>(pattern(x, y, 0));
    }
  }
  return t;
}

mediadiff::TappedFrame tapped(const AVFrame* frame, const mediadiff::Thumbnail* thumb, std::int64_t index = 0) {
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

}  // namespace

TEST_CASE("quality - identical media psnr", "[integration]") {
  int exit_code = -1;
  const json report = compare_report("video_hash_base.mp4", "video_hash_base.ts", {"--psnr"}, &exit_code);
  const json finding = finding_of(report, kPsnr);
  REQUIRE_FALSE(finding.is_null());
  // Both sides 60 dB: the baseline's self-score IS the cap, and identical media
  // reach it exactly, so the delta is exactly 0.
  CHECK(num_of(finding.at("baseline")) == 60000);
  CHECK(finding.at("baseline").at("den") == 1000);
  CHECK(num_of(finding.at("candidate")) == 60000);
  CHECK(finding.at("candidate").at("den") == 1000);
  CHECK(finding.at("status") == "pass");
  const json& evidence = candidate_evidence(finding);
  // The TS is a stream copy, so every one of the pairs is identical.
  CHECK(evidence.at("identical_frames") == evidence.at("pairs_scored"));
  CHECK(evidence.at("pairs_scored").get<std::int64_t>() > 0);
  CHECK(evidence.at("bpc") == 8);
  CHECK(num_of(evidence.at("mean")) == 60000);
  CHECK(num_of(evidence.at("min").at("value")) == 60000);
  for (const char* plane : {"y", "u", "v"}) {
    CHECK(num_of(evidence.at("per_plane").at(plane)) == 60000);
  }
  // quality.* groups under `content`, like every other content check.
  CHECK(finding.at("group") == "content");
  // The other quality check was not asked for.
  const json ssim = finding_of(report, kSsim);
  REQUIRE_FALSE(ssim.is_null());
  CHECK(ssim.at("status") == "skipped");
  CHECK(ssim.at("skip_reason") == "not_requested");
}

TEST_CASE("quality - degraded psnr", "[integration]") {
  int exit_code = 0;
  const json report = compare_report("video_hash_base.mp4", "video_perc_degraded.mp4", {"--psnr"}, &exit_code);
  const json finding = finding_of(report, kPsnr);
  REQUIRE_FALSE(finding.is_null());
  const json& evidence = candidate_evidence(finding);

  const std::int64_t mean = num_of(evidence.at("mean"));
  // More than 0.5 dB under the 60 dB cap, so the check fails and gates.
  CHECK(mean < 60000 - 500);
  CHECK(num_of(finding.at("candidate")) == mean);
  CHECK(num_of(finding.at("baseline")) == 60000);
  CHECK(finding.at("status") == "fail");
  CHECK(exit_code != 0);

  // Against the independent floating-point oracle: the gated value is the mean
  // of the per-frame PSNRs (each rounded to a milli-dB, then floored), so it sits
  // within 1.5 milli-dB of the real-valued mean.
  const Oracle expected = oracle("video_hash_base.mp4", "video_perc_degraded.mp4");
  CHECK(evidence.at("pairs_scored").get<std::int64_t>() == static_cast<std::int64_t>(expected.psnr_mdb.size()));
  INFO("mean " << mean << " oracle " << mean_of(expected.psnr_mdb));
  CHECK(std::fabs(static_cast<double>(mean) - mean_of(expected.psnr_mdb)) < 1.5);

  // The minimum frame is NAMED: its index, PTS and value (D-03).
  const json& min = evidence.at("min");
  const std::int64_t min_value = num_of(min.at("value"));
  const double oracle_min = *std::min_element(expected.psnr_mdb.begin(), expected.psnr_mdb.end());
  CHECK(std::fabs(static_cast<double>(min_value) - oracle_min) < 1.0);
  CHECK(min_value < mean);
  const std::size_t index = min.at("baseline_index").get<std::size_t>();
  REQUIRE(index < expected.psnr_mdb.size());
  CHECK(min.at("candidate_index").get<std::size_t>() == index);
  CHECK(std::fabs(expected.psnr_mdb[index] - oracle_min) < 1.0);
  REQUIRE(min.contains("pts"));
  CHECK(min.at("pts").at("value").get<std::int64_t>() == expected.pts[index]);

  // The three per-plane means, each against the oracle.
  const std::array<const char*, 3> names = {"y", "u", "v"};
  for (std::size_t p = 0; p < 3; ++p) {
    const double got = static_cast<double>(num_of(evidence.at("per_plane").at(names[p])));
    INFO("plane " << names[p] << " got " << got << " oracle " << mean_of(expected.plane_mdb[p]));
    CHECK(std::fabs(got - mean_of(expected.plane_mdb[p])) < 1.5);
  }
}

TEST_CASE("quality - ssim", "[integration]") {
  // Identical media: exactly 1 on both sides.
  {
    const json report = compare_report("video_hash_base.mp4", "video_hash_base.ts", {"--ssim"});
    const json finding = finding_of(report, kSsim);
    REQUIRE_FALSE(finding.is_null());
    CHECK(num_of(finding.at("baseline")) == kMicro);
    CHECK(finding.at("baseline").at("den") == kMicro);
    CHECK(num_of(finding.at("candidate")) == kMicro);
    CHECK(finding.at("candidate").at("den") == kMicro);
    CHECK(finding.at("status") == "pass");
    const json& evidence = candidate_evidence(finding);
    CHECK(evidence.at("identical_frames") == evidence.at("pairs_scored"));
    CHECK_FALSE(evidence.contains("per_plane"));
    const json psnr = finding_of(report, kPsnr);
    REQUIRE_FALSE(psnr.is_null());
    CHECK(psnr.at("skip_reason") == "not_requested");
  }

  // Degraded: a lower candidate mean (the gate) and a named minimum.
  int exit_code = 0;
  const json report = compare_report("video_hash_base.mp4", "video_perc_degraded.mp4", {"--ssim"}, &exit_code);
  const json finding = finding_of(report, kSsim);
  REQUIRE_FALSE(finding.is_null());
  const json& evidence = candidate_evidence(finding);
  const std::int64_t mean = num_of(finding.at("candidate"));
  CHECK(mean < kMicro - 5000);
  CHECK(num_of(evidence.at("mean")) == mean);
  CHECK(finding.at("status") == "fail");
  CHECK(exit_code != 0);

  const Oracle expected = oracle("video_hash_base.mp4", "video_perc_degraded.mp4");
  INFO("mean " << mean << " oracle " << mean_of(expected.ssim) * 1e6);
  CHECK(std::fabs(static_cast<double>(mean) / 1e6 - mean_of(expected.ssim)) < 1e-5);
  const std::int64_t min_value = num_of(evidence.at("min").at("value"));
  const double oracle_min = *std::min_element(expected.ssim.begin(), expected.ssim.end());
  CHECK(std::fabs(static_cast<double>(min_value) / 1e6 - oracle_min) < 1e-5);
  CHECK(min_value <= mean);
  const std::size_t index = evidence.at("min").at("baseline_index").get<std::size_t>();
  REQUIRE(index < expected.ssim.size());
  CHECK(std::fabs(expected.ssim[index] - oracle_min) < 1e-5);
}

TEST_CASE("quality - not requested", "[integration]") {
  const json report = compare_report("video_hash_base.mp4", "video_hash_base.ts");
  for (const char* id : {kPsnr, kSsim}) {
    const json finding = finding_of(report, id);
    REQUIRE_FALSE(finding.is_null());
    CHECK(finding.at("status") == "skipped");
    CHECK(finding.at("skip_reason") == "not_requested");
  }

  // On BOTH sides: the two fingerprints each carry the named skip, never a value
  // on one and a skip on the other.
  const mediadiff::CheckRegistry& registry = mediadiff::builtin_registry();
  auto pair = mediadiff::fingerprint_pair(fixture("video_hash_base.mp4"), fixture("video_hash_base.ts"), registry,
                                          mediadiff::ProbeOptions{});
  REQUIRE(pair.has_value());
  for (const char* id : {kPsnr, kSsim}) {
    const std::uint32_t index = *registry.find(id);
    for (const mediadiff::Fingerprint* fp : {&pair->baseline, &pair->candidate}) {
      std::size_t seen = 0;
      for (const mediadiff::Measurement& m : fp->measurements) {
        if (m.check_index == index) {
          ++seen;
          CHECK(m.skip_reason == mediadiff::SkipReason::not_requested);
          CHECK(std::holds_alternative<mediadiff::Absent>(m.value));
        }
      }
      CHECK(seen == 1);
    }
  }
}

TEST_CASE("quality - geometry", "[integration]") {
  // 352x288 against 704x576: the native planes cannot be paired, but both
  // pictures thumbnail to 128x104, so the perceptual score still runs.
  const json report =
      compare_report("video_hash_base.mp4", "video_perc_upscaled.mp4", {"--psnr", "--ssim"});
  for (const char* id : {kPsnr, kSsim}) {
    const json finding = finding_of(report, id);
    REQUIRE_FALSE(finding.is_null());
    CHECK(finding.at("status") == "skipped");
    CHECK(finding.at("skip_reason") == "geometry_mismatch");
  }
  const json perceptual = finding_of(report, "content.video.perceptual");
  REQUIRE_FALSE(perceptual.is_null());
  CHECK(perceptual.at("skip_reason") == "none");
  CHECK(perceptual.at("status") != "skipped");

  // Both fingerprints name the layouts that did not pair.
  const mediadiff::CheckRegistry& registry = mediadiff::builtin_registry();
  mediadiff::QualityRequest request;
  request.psnr = true;
  auto pair = mediadiff::fingerprint_pair(fixture("video_hash_base.mp4"), fixture("video_perc_upscaled.mp4"),
                                          registry, mediadiff::ProbeOptions{}, request);
  REQUIRE(pair.has_value());
  const std::uint32_t index = *registry.find(kPsnr);
  for (const mediadiff::Measurement& m : pair->candidate.measurements) {
    if (m.check_index == index) {
      CHECK(m.evidence.at("baseline_native") == "352x288 yuv420p");
      CHECK(m.evidence.at("candidate_native") == "704x576 yuv420p");
    }
  }
}

TEST_CASE("quality - bit depth promotion", "[integration]") {
  // An 8-bit frame against the same content at 10 bits (every sample shifted
  // left by two): promoting the 8-bit side by an exact left shift makes them
  // identical, so PSNR is the 10-bit cap and SSIM is exactly 1.
  const FramePtr eight = yuv420(false, 64, 48);
  const FramePtr ten = yuv420(true, 64, 48);
  const mediadiff::Thumbnail thumb = thumbnail();

  mediadiff::PairScorer scorer(1, 0, mediadiff::QualityRequest{true, true, false});
  REQUIRE(scorer.step(tapped(eight.get(), &thumb), tapped(ten.get(), &thumb)) ==
          mediadiff::PairScorer::Action::advance_both);
  REQUIRE(scorer.quality_stop() == mediadiff::PairScorer::QualityStop::none);
  CHECK(scorer.quality_bpc() == 10);
  const auto psnr = scorer.psnr_summary();
  REQUIRE(psnr.has_value());
  CHECK(psnr->mean_milli_db == 72000);
  CHECK(psnr->mean_milli_db == mediadiff::psnr_cap_milli_db(10));
  CHECK(psnr->identical_frames == 1);
  CHECK(psnr->plane_count == 3);
  const auto ssim = scorer.ssim_summary();
  REQUIRE(ssim.has_value());
  CHECK(ssim->mean_micro == kMicro);
  CHECK(ssim->identical_frames == 1);

  // The mirror (10-bit baseline, 8-bit candidate) agrees.
  mediadiff::PairScorer mirror(1, 0, mediadiff::QualityRequest{true, true, false});
  mirror.step(tapped(ten.get(), &thumb), tapped(eight.get(), &thumb));
  REQUIRE(mirror.psnr_summary().has_value());
  CHECK(mirror.psnr_summary()->mean_milli_db == 72000);

  // Non-vacuity: a +40 difference on four 10-bit luma samples is a real drop. The
  // oracle: SSE = 4 * 40^2 = 6400 over 64*48 + 2 * 32*24 = 4608 samples, so
  // PSNR = 10 * log10(1023^2 * 4608 / 6400).
  const FramePtr bumped = yuv420(true, 64, 48, 40);
  mediadiff::PairScorer drop(1, 0, mediadiff::QualityRequest{true, true, false});
  drop.step(tapped(eight.get(), &thumb), tapped(bumped.get(), &thumb));
  const auto dropped = drop.psnr_summary();
  REQUIRE(dropped.has_value());
  const double want = 1000.0 * 10.0 * std::log10(1023.0 * 1023.0 * 4608.0 / 6400.0);
  INFO("psnr " << dropped->mean_milli_db << " oracle " << want);
  CHECK(std::fabs(static_cast<double>(dropped->mean_milli_db) - want) < 1.0);
  CHECK(dropped->identical_frames == 0);
  REQUIRE(drop.ssim_summary().has_value());
  CHECK(drop.ssim_summary()->mean_micro < kMicro);
}

TEST_CASE("quality - hand computed sse on a tiny gray plane", "[integration]") {
  // 16x16 gray8: the candidate adds 3 to four samples, so SSE = 4 * 9 = 36 over
  // 256 samples: PSNR = 10 * log10(255^2 * 256 / 36), one plane only.
  FramePtr a = make_frame(AV_PIX_FMT_GRAY8, 16, 16);
  FramePtr b = make_frame(AV_PIX_FMT_GRAY8, 16, 16);
  for (int y = 0; y < 16; ++y) {
    for (int x = 0; x < 16; ++x) {
      const std::uint8_t v = static_cast<std::uint8_t>(pattern(x, y, 0));
      a->data[0][y * a->linesize[0] + x] = v;
      b->data[0][y * b->linesize[0] + x] = static_cast<std::uint8_t>(v + ((y == 3 && x < 4) ? 3 : 0));
    }
  }
  // pattern() < 250 on the bumped samples, so the +3 does not wrap.
  for (int x = 0; x < 4; ++x) {
    REQUIRE(pattern(x, 3, 0) < 250);
  }
  const mediadiff::Thumbnail thumb = thumbnail();
  mediadiff::PairScorer scorer(1, 0, mediadiff::QualityRequest{true, false, false});
  scorer.step(tapped(a.get(), &thumb), tapped(b.get(), &thumb));
  REQUIRE(scorer.psnr_summary().has_value());
  const double want = 1000.0 * 10.0 * std::log10(255.0 * 255.0 * 256.0 / 36.0);
  INFO("psnr " << scorer.psnr_summary()->mean_milli_db << " oracle " << want);
  CHECK(std::fabs(static_cast<double>(scorer.psnr_summary()->mean_milli_db) - want) < 1.0);
  CHECK(scorer.psnr_summary()->plane_count == 1);
  CHECK_FALSE(scorer.ssim_summary().has_value());  // not requested
}

TEST_CASE("quality - scorer latches its own stops and perceptual runs on", "[integration]") {
  const mediadiff::Thumbnail thumb = thumbnail();

  // Different display dimensions: the quality scorers latch geometry_mismatch,
  // naming both layouts; perceptual (its own thumbnails are equal) is unaffected.
  {
    const FramePtr small = yuv420(false, 64, 48);
    const FramePtr large = yuv420(false, 128, 96);
    mediadiff::PairScorer scorer(1, 0, mediadiff::QualityRequest{true, true, false});
    scorer.step(tapped(small.get(), &thumb), tapped(large.get(), &thumb));
    CHECK(scorer.quality_stop() == mediadiff::PairScorer::QualityStop::geometry_mismatch);
    CHECK(scorer.quality_baseline_label() == "64x48 yuv420p");
    CHECK(scorer.quality_candidate_label() == "128x96 yuv420p");
    CHECK_FALSE(scorer.psnr_summary().has_value());
    CHECK_FALSE(scorer.stopped());
    CHECK(scorer.pairs_scored() == 1);
  }
  // A different chroma layout is a plane-layout mismatch too.
  {
    const FramePtr yuv = yuv420(false, 64, 48);
    FramePtr gray = make_frame(AV_PIX_FMT_GRAY8, 64, 48);
    mediadiff::PairScorer scorer(1, 0, mediadiff::QualityRequest{true, false, false});
    scorer.step(tapped(yuv.get(), &thumb), tapped(gray.get(), &thumb));
    CHECK(scorer.quality_stop() == mediadiff::PairScorer::QualityStop::geometry_mismatch);
    CHECK(scorer.pairs_scored() == 1);
  }
  // RGB has no luma plane: unsupported, not a crash and not a score.
  {
    const FramePtr yuv = yuv420(false, 64, 48);
    FramePtr rgb = make_frame(AV_PIX_FMT_RGB24, 64, 48);
    mediadiff::PairScorer scorer(1, 0, mediadiff::QualityRequest{true, false, false});
    scorer.step(tapped(yuv.get(), &thumb), tapped(rgb.get(), &thumb));
    CHECK(scorer.quality_stop() == mediadiff::PairScorer::QualityStop::unsupported_format);
    CHECK_FALSE(scorer.psnr_summary().has_value());
  }
  // A pair with no frame to read (a thumbnail-only tap).
  {
    mediadiff::PairScorer scorer(1, 0, mediadiff::QualityRequest{true, false, false});
    scorer.step(tapped(nullptr, &thumb), tapped(nullptr, &thumb));
    CHECK(scorer.quality_stop() == mediadiff::PairScorer::QualityStop::frame_unavailable);
  }
  // A frame smaller than one SSIM window: no SSIM, but PSNR is unaffected.
  {
    FramePtr a = make_frame(AV_PIX_FMT_GRAY8, 6, 6);
    FramePtr b = make_frame(AV_PIX_FMT_GRAY8, 6, 6);
    for (int y = 0; y < 6; ++y) {
      for (int x = 0; x < 6; ++x) {
        a->data[0][y * a->linesize[0] + x] = static_cast<std::uint8_t>(10 * x);
        b->data[0][y * b->linesize[0] + x] = static_cast<std::uint8_t>(10 * x + 2);
      }
    }
    mediadiff::PairScorer scorer(1, 0, mediadiff::QualityRequest{true, true, false});
    scorer.step(tapped(a.get(), &thumb), tapped(b.get(), &thumb));
    CHECK(scorer.ssim_frame_too_small());
    CHECK_FALSE(scorer.ssim_summary().has_value());
    REQUIRE(scorer.psnr_summary().has_value());
    CHECK(scorer.psnr_summary()->mean_milli_db < 60000);
  }
}

TEST_CASE("quality - sampling", "[integration]") {
  const json full = compare_report("video_hash_base.mp4", "video_perc_degraded.mp4", {"--psnr"});
  const json sampled = compare_report("video_hash_base.mp4", "video_perc_degraded.mp4", {"--psnr", "--sample", "2"});
  const json full_finding = finding_of(full, kPsnr);
  const json sampled_finding = finding_of(sampled, kPsnr);
  const json& f = candidate_evidence(full_finding);
  const json& s = candidate_evidence(sampled_finding);
  const std::int64_t full_pairs = f.at("pairs_scored").get<std::int64_t>();
  // Every second pair: ceil(pairs / 2), the ordinals 0, 2, 4, ...
  CHECK(s.at("pairs_scored").get<std::int64_t>() == (full_pairs + 1) / 2);
  CHECK(s.at("sampling_state") == "sampled:2");
  CHECK(f.at("sampling_state") == "full");
  CHECK(sampled_finding.at("evidence").at("baseline").at("sampling_state") == "sampled:2");
}

TEST_CASE("quality - deterministic", "[integration]") {
  const std::vector<std::string> args = {"compare",
                                         fixture("video_hash_base.mp4"),
                                         fixture("video_perc_degraded.mp4"),
                                         "--psnr",
                                         "--ssim",
                                         "--json"};
  const CliResult first = run_cli(args);
  const CliResult second = run_cli(args);
  REQUIRE_FALSE(first.out.empty());
  CHECK(first.out == second.out);
  CHECK(first.exit_code == second.exit_code);
}

TEST_CASE("quality - TRUST-04 path preconditions", "[integration]") {
  // quality.psnr and quality.ssim carry BOTH path keys on BOTH sides, so
  // 07-09's generic table in compare_tol guards them (D-04): a differing
  // decode_path_signature, or a key on one side only, is skipped:path_incomparable
  // even though the two scores are identical and inside tolerance.
  const mediadiff::CheckRegistry& registry = mediadiff::builtin_registry();
  mediadiff::QualityRequest request;
  request.psnr = true;
  request.ssim = true;
  auto pair = mediadiff::fingerprint_pair(fixture("video_hash_base.mp4"), fixture("video_hash_base.ts"), registry,
                                          mediadiff::ProbeOptions{}, request);
  REQUIRE(pair.has_value());

  const std::array<std::uint32_t, 2> ids = {*registry.find(kPsnr), *registry.find(kSsim)};
  const auto measurement_of = [](mediadiff::Fingerprint& fp, std::uint32_t id) -> mediadiff::Measurement& {
    const auto it = std::find_if(fp.measurements.begin(), fp.measurements.end(),
                                 [id](const mediadiff::Measurement& m) { return m.check_index == id; });
    INFO("check index " << id);
    REQUIRE(it != fp.measurements.end());
    return *it;
  };
  for (const std::uint32_t id : ids) {
    for (mediadiff::Fingerprint* fp : {&pair->baseline, &pair->candidate}) {
      const mediadiff::Measurement& m = measurement_of(*fp, id);
      REQUIRE(m.skip_reason == mediadiff::SkipReason::none);
      CHECK(m.evidence.contains("scaler_path"));
      CHECK(m.evidence.contains("decode_path_signature"));
      CHECK_FALSE(m.evidence.at("decode_path_signature").get<std::string>().empty());
    }
    // The two sides of a live run carry the same record.
    CHECK(measurement_of(pair->baseline, id).evidence.at("decode_path_signature") ==
          measurement_of(pair->candidate, id).evidence.at("decode_path_signature"));
  }

  auto policy = mediadiff::resolve_policy(registry, mediadiff::ProfileId::sw_encoder);
  REQUIRE(policy.has_value());
  const auto statuses = [&](const mediadiff::Fingerprint& b, const mediadiff::Fingerprint& c) {
    auto findings = mediadiff::compare_fingerprints(b, c, *policy, registry);
    REQUIRE(findings.has_value());
    std::vector<std::pair<mediadiff::Status, mediadiff::SkipReason>> out;
    for (const mediadiff::Finding& finding : *findings) {
      if (finding.id == kPsnr || finding.id == kSsim) {
        out.emplace_back(finding.status, finding.skip_reason);
      }
    }
    return out;
  };

  // Unmodified: both pass.
  {
    const auto live = statuses(pair->baseline, pair->candidate);
    REQUIRE(live.size() == 2);
    for (const auto& [status, reason] : live) {
      CHECK(status == mediadiff::Status::pass);
      CHECK(reason == mediadiff::SkipReason::none);
    }
  }
  // A differing decode path on the candidate side.
  {
    mediadiff::Fingerprint candidate = pair->candidate;
    for (const std::uint32_t id : ids) {
      measurement_of(candidate, id).evidence["decode_path_signature"] = "avcodec/0.0.0 flags/other";
    }
    const auto result = statuses(pair->baseline, candidate);
    REQUIRE(result.size() == 2);
    for (const auto& [status, reason] : result) {
      CHECK(status == mediadiff::Status::skipped);
      CHECK(reason == mediadiff::SkipReason::path_incomparable);
    }
  }
  // A key present on one side only is a mismatch, never assumed equal.
  {
    mediadiff::Fingerprint candidate = pair->candidate;
    for (const std::uint32_t id : ids) {
      measurement_of(candidate, id).evidence.erase("scaler_path");
    }
    const auto result = statuses(pair->baseline, candidate);
    REQUIRE(result.size() == 2);
    for (const auto& [status, reason] : result) {
      CHECK(status == mediadiff::Status::skipped);
      CHECK(reason == mediadiff::SkipReason::path_incomparable);
    }
  }
}
