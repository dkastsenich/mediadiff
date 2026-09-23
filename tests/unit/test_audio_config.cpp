// 06-04-PLAN.md Task 1 / 06-13-PLAN.md (AUDIO-03, D-12):
// resolve_sbr_signaling() -- the no-decode ASC fast path (a top-level
// AOT_SBR object type, or a 0x2b7 backward-compatible sync extension),
// D-12's PRIMARY header-pass mechanism (the post-find_stream_info
// `codecpar` evidence, passed in as HeaderPassSbrEvidence), and the
// bounded one-packet FALLBACK (reached only when the header pass resolved
// no profile at all) -- all exercised directly against
// mediadiff::resolve_sbr_signaling() with a call-counting fake
// SbrProbeFn, so a test can prove the probe is NOT called in the cases
// the header pass already answers. No file, no real decoder, mirroring
// tests/unit/test_gop_classification.cpp's own SpsBitWriter-plus-
// synthetic-buffer convention for a small, hand-verified bitstream
// grammar.
//
// A second block of tests (marked "real fixture" below) exercises
// StreamInfo::sbr_signaling/effective_sample_rate_hz end to end through a
// real DemuxSession opened against 06-02's hand-written HE-AAC fixture
// pair, plus a NORMALLY-ENCODED AAC-LC fixture -- the case 06-04's
// implementation reported `unknown` for, because its one-packet fallback
// cannot decode a frame from a stream whose first packet is consumed
// entirely as encoder-delay priming (06-13-PLAN.md, .planning/WINDOWS.md).

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/error.h"
#include "probe/audio_config.h"
#include "probe/demux_session.h"
#include "support/fixture_paths.h"
#include "util/expected.h"

using mediadiff::AudioObjectType;
using mediadiff::AudioSpecificConfig;
using mediadiff::DemuxOptions;
using mediadiff::DemuxSession;
using mediadiff::Error;
using mediadiff::ErrorKind;
using mediadiff::HeaderPassSbrEvidence;
using mediadiff::implicit_sbr_is_possible;
using mediadiff::parse_audio_specific_config;
using mediadiff::resolve_sbr_signaling;
using mediadiff::SbrProbeDecodeResult;
using mediadiff::SbrResolution;
using mediadiff::SbrSignaling;
using mediadiff::StreamInfo;

// 06-18-PLAN.md (CR-05): a small RAII guard that restores
// DemuxSession's process-wide default wall-clock budget on scope exit --
// used by the zero-budget determinism test below so it never leaks a
// changed global budget into a later test in the same binary.
namespace {
struct ScopedWallClockBudget {
  std::int64_t previous = mediadiff::default_wall_clock_budget_ms();
  explicit ScopedWallClockBudget(std::int64_t ms) { mediadiff::set_default_wall_clock_budget_ms(ms); }
  ~ScopedWallClockBudget() { mediadiff::set_default_wall_clock_budget_ms(previous); }
};
}  // namespace

namespace {

std::string fixture(const std::string& name) { return mediadiff::test::fixture_dir() + "/" + name; }

// A plain, bounds-checked bit-at-a-time MSB-first writer, mirroring
// tests/unit/test_gop_classification.cpp's own SpsBitWriter (u()/to_bytes()
// only -- this grammar has no Exp-Golomb fields).
class AscBitWriter {
 public:
  void u(int n, std::uint32_t value) {
    for (int i = n - 1; i >= 0; --i) {
      bits_.push_back((value >> i) & 1U);
    }
  }
  std::vector<std::uint8_t> to_bytes(std::size_t pad_to_bits = 0) const {
    std::vector<bool> bits = bits_;
    while (bits.size() < pad_to_bits) {
      bits.push_back(false);
    }
    while (bits.size() % 8 != 0) {
      bits.push_back(false);
    }
    std::vector<std::uint8_t> out;
    for (std::size_t i = 0; i < bits.size(); i += 8) {
      std::uint8_t byte = 0;
      for (std::size_t b = 0; b < 8; ++b) {
        byte = static_cast<std::uint8_t>((byte << 1) | (bits[i + b] ? 1 : 0));
      }
      out.push_back(byte);
    }
    return out;
  }

 private:
  std::vector<bool> bits_;
};

// object_type=5 (AOT_SBR), sampling_index=7 (22050), chan_config=2
// (stereo), ext_sampling_index=4 (44100), inner_object_type=2 (AAC_LC) --
// mirrors tools/gen_he_aac.py's own make_sbr_explicit_bytes() ASC shape
// exactly (06-02-PLAN.md's own construction, hand-verified against
// mpeg4audio.c).
std::vector<std::uint8_t> explicit_sbr_asc_bytes() {
  AscBitWriter w;
  w.u(5, 5);  // AOT_SBR
  w.u(4, 7);  // 22050
  w.u(4, 2);  // stereo
  w.u(4, 4);  // ext sampling index -> 44100
  w.u(5, 2);  // inner object type: AAC_LC
  return w.to_bytes();
}

// A bare AOT_AAC_LC ASC (object_type=2, sampling_index=4 -> 44100,
// chan_config=2) with NO sync extension at all -- the genuinely ambiguous
// case (06-02's own make_sbr_implicit_bytes()/make_aac_handwritten_bytes()
// ASC shape). Padded to `pad_to_bits` so a caller can control how many
// trailing zero bits follow (kept well under 15 to avoid an accidental
// 0x2b7 collision, mirroring gen_he_aac.py's own build_asc safeguard).
std::vector<std::uint8_t> bare_lc_asc_bytes(std::size_t pad_to_bits = 0) {
  AscBitWriter w;
  w.u(5, 2);  // AOT_AAC_LC
  w.u(4, 4);  // 44100
  w.u(4, 2);  // stereo
  return w.to_bytes(pad_to_bits);
}

// A bare AOT_AAC_LC ASC followed by a legacy 0x2b7 backward-compatible
// sync extension declaring AOT_SBR with sbr_present=1 and an explicit
// extension sampling-frequency index -- Test 2's own construction, hand-
// verified against ff_mpeg4audio_get_config_gb's own sync_extension scan
// (mpeg4audio.c).
std::vector<std::uint8_t> sync_extension_sbr_asc_bytes() {
  AscBitWriter w;
  w.u(5, 2);   // AOT_AAC_LC (base)
  w.u(4, 7);   // 22050 (base/core rate)
  w.u(4, 2);   // stereo
  w.u(11, 0x2b7);  // sync extension marker
  w.u(5, 5);   // ext_object_type = AOT_SBR
  w.u(1, 1);   // sbr_present_flag = 1
  w.u(4, 4);   // ext sampling index -> 44100
  return w.to_bytes();
}

}  // namespace

// --- Test 5/6 (parse_audio_specific_config's own escape/truncation rules,
// re-verified here as this task's own extension point) ---------------------

TEST_CASE("audio_config - object type 31 escapes to 32+the following 6 bits, and 42 (AOT_USAC) round-trips",
          "[unit]") {
  AscBitWriter w;
  w.u(5, 31);  // escape
  w.u(6, 10);  // 32 + 10 == 42 (AOT_USAC)
  w.u(4, 4);   // 44100
  w.u(4, 1);   // mono
  const auto asc = parse_audio_specific_config(w.to_bytes());
  REQUIRE(asc.has_value());
  REQUIRE(asc->object_type == static_cast<std::int32_t>(AudioObjectType::usac));
  REQUIRE(asc->object_type == 42);
}

TEST_CASE("audio_config - a truncated ASC (one byte, zero bytes, and a buffer shorter than the declared escape) "
          "yields std::nullopt",
          "[unit]") {
  REQUIRE_FALSE(parse_audio_specific_config({}).has_value());

  const std::vector<std::uint8_t> one_byte = {0x00};
  REQUIRE_FALSE(parse_audio_specific_config(one_byte).has_value());

  // sampling_frequency_index == 15 declares a 24-bit escape, but only 4
  // more bits follow it (well short of 24) -- must yield std::nullopt,
  // never read past the buffer.
  AscBitWriter w;
  w.u(5, 2);   // AOT_AAC_LC
  w.u(4, 15);  // escape sampling-frequency index
  w.u(4, 0);   // far short of the declared 24-bit escape rate
  const auto short_escape = parse_audio_specific_config(w.to_bytes());
  REQUIRE_FALSE(short_escape.has_value());
}

// --- Shared helpers for the resolve_sbr_signaling block ------------------

namespace {

// A probe that must never be called: every test below asserts its own
// call count explicitly, so a regression that reintroduces 06-04's
// "probe first, ask questions later" ordering fails HERE by name rather
// than only showing up as an instruction-count ratchet failure in CI.
//
// 06-18-PLAN.md (CR-05): `error`, when set, makes `fn()` return that Error
// instead of `result` -- the fake's own way of exercising CR-05's
// Error-propagation contract without a real container open.
struct CountingProbe {
  int calls = 0;
  std::optional<SbrProbeDecodeResult> result;
  std::optional<Error> error;

  mediadiff::SbrProbeFn fn() {
    return [this]() -> mediadiff::expected<std::optional<SbrProbeDecodeResult>, Error> {
      ++calls;
      if (error.has_value()) {
        return mediadiff::unexpected(*error);
      }
      return result;
    };
  }
};

// What the header pass reports for an ordinary AAC-LC stream: libav
// decoded frames inside avformat_find_stream_info and resolved
// AV_PROFILE_AAC_LOW at the stream's own declared rate.
HeaderPassSbrEvidence header_resolved_lc(std::int64_t rate_hz) {
  HeaderPassSbrEvidence h;
  h.profile_resolved = true;
  h.profile_is_he_aac = false;
  h.resolved_sample_rate_hz = rate_hz;
  return h;
}

// What the header pass reports when it could not decode a single frame --
// codecpar->profile is still AV_PROFILE_UNKNOWN. This is the ONLY state
// in which D-12's bounded fallback probe is reachable.
HeaderPassSbrEvidence header_resolved_nothing() { return HeaderPassSbrEvidence{}; }

}  // namespace

// --- Test 1: explicit top-level AOT_SBR -> explicit_asc, NO decode -------

TEST_CASE("audio_config - resolve_sbr_signaling: a top-level AOT_SBR ASC resolves to explicit_asc with no decode",
          "[unit]") {
  const auto asc = parse_audio_specific_config(explicit_sbr_asc_bytes());
  REQUIRE(asc.has_value());
  REQUIRE(asc->has_explicit_sbr);

  CountingProbe probe;
  // Deliberately hand it header evidence that WOULD read as implicit if
  // consulted (an HE profile at a doubled rate) -- explicit signaling is
  // decided from the ASC alone and must outrank it, since explicit-versus-
  // implicit is exactly what audio.profile exists to carry.
  HeaderPassSbrEvidence header;
  header.profile_resolved = true;
  header.profile_is_he_aac = true;
  header.resolved_sample_rate_hz = 44100;

  const auto result = resolve_sbr_signaling(/*codec_id_is_aac=*/true, asc, header, probe.fn());
  REQUIRE(result.has_value());
  REQUIRE(result->signaling == SbrSignaling::explicit_asc);
  REQUIRE(probe.calls == 0);
}

// --- Test 2: a 0x2b7 backward-compatible sync extension -> explicit_asc,
// NO decode -----------------------------------------------------------------

TEST_CASE("audio_config - resolve_sbr_signaling: a 0x2b7 sync-extension SBR marker resolves to explicit_asc with "
          "no decode",
          "[unit]") {
  const auto asc = parse_audio_specific_config(sync_extension_sbr_asc_bytes());
  REQUIRE(asc.has_value());
  REQUIRE(asc->has_explicit_sbr);
  REQUIRE(asc->extension_sampling_frequency_hz == 44100);

  CountingProbe probe;
  const auto result =
      resolve_sbr_signaling(/*codec_id_is_aac=*/true, asc, header_resolved_lc(22050), probe.fn());
  REQUIRE(result.has_value());
  REQUIRE(result->signaling == SbrSignaling::explicit_asc);
  REQUIRE(probe.calls == 0);
}

// --- D-12's PRIMARY mechanism: the header pass's own post-
// find_stream_info codecpar, at zero additional cost, with the bounded
// fallback probe never invoked -------------------------------------------

TEST_CASE("audio_config - resolve_sbr_signaling: a bare AAC-LC ASC whose header pass resolved an HE-AAC profile "
          "resolves to implicit_decoded WITHOUT calling the bounded probe",
          "[unit]") {
  const auto asc = parse_audio_specific_config(bare_lc_asc_bytes());
  REQUIRE(asc.has_value());
  REQUIRE_FALSE(asc->has_explicit_sbr);

  CountingProbe probe;
  HeaderPassSbrEvidence header;
  header.profile_resolved = true;
  header.profile_is_he_aac = true;
  header.resolved_sample_rate_hz = 88200;

  const auto result = resolve_sbr_signaling(/*codec_id_is_aac=*/true, asc, header, probe.fn());
  REQUIRE(result.has_value());
  REQUIRE(result->signaling == SbrSignaling::implicit_decoded);
  REQUIRE(probe.calls == 0);
}

TEST_CASE("audio_config - resolve_sbr_signaling: a bare AAC-LC ASC whose header pass resolved exactly DOUBLE the "
          "declared rate resolves to implicit_decoded WITHOUT calling the bounded probe",
          "[unit]") {
  const auto asc = parse_audio_specific_config(bare_lc_asc_bytes());
  REQUIRE(asc.has_value());
  REQUIRE(asc->sampling_frequency_hz == 44100);

  CountingProbe probe;
  HeaderPassSbrEvidence header;
  header.profile_resolved = true;
  header.profile_is_he_aac = false;  // profile alone would say LC
  header.resolved_sample_rate_hz = 88200;  // but the rate was doubled

  const auto result = resolve_sbr_signaling(/*codec_id_is_aac=*/true, asc, header, probe.fn());
  REQUIRE(result.has_value());
  REQUIRE(result->signaling == SbrSignaling::implicit_decoded);
  REQUIRE(probe.calls == 0);
}

// This is the regression test for 06-13's own quality defect: an ordinary
// AAC-LC stream whose header pass resolved a real LC profile at the
// declared (undoubled) rate is a POSITIVE "no SBR" determination.
// Reporting `unknown` here is a latent false finding, because D-14 makes
// `unknown` compare as its own value -- so a later build that learns the
// answer turns an unchanged file into a finding.
TEST_CASE("audio_config - resolve_sbr_signaling: a bare AAC-LC ASC whose header pass resolved a plain LC profile "
          "at the declared rate resolves to none, NOT unknown, and never calls the bounded probe",
          "[unit]") {
  const auto asc = parse_audio_specific_config(bare_lc_asc_bytes());
  REQUIRE(asc.has_value());

  CountingProbe probe;
  const auto result =
      resolve_sbr_signaling(/*codec_id_is_aac=*/true, asc, header_resolved_lc(44100), probe.fn());
  REQUIRE(result.has_value());
  REQUIRE(result->signaling == SbrSignaling::none);
  REQUIRE(probe.calls == 0);
}

// The ADTS/MPEG-TS shape: no extradata at all, so no ASC -- but the header
// pass still resolved a real profile by decoding, which is enough to
// determine there is no SBR.
TEST_CASE("audio_config - resolve_sbr_signaling: no ASC at all but a header-pass-resolved LC profile resolves to "
          "none, not unknown",
          "[unit]") {
  CountingProbe probe;
  const auto result =
      resolve_sbr_signaling(/*codec_id_is_aac=*/true, std::nullopt, header_resolved_lc(44100), probe.fn());
  REQUIRE(result.has_value());
  REQUIRE(result->signaling == SbrSignaling::none);
  REQUIRE(probe.calls == 0);
}

TEST_CASE("audio_config - resolve_sbr_signaling: no ASC at all but a header-pass-resolved HE-AAC profile resolves "
          "to implicit_decoded",
          "[unit]") {
  CountingProbe probe;
  HeaderPassSbrEvidence header;
  header.profile_resolved = true;
  header.profile_is_he_aac = true;
  header.resolved_sample_rate_hz = 44100;

  const auto result = resolve_sbr_signaling(/*codec_id_is_aac=*/true, std::nullopt, header, probe.fn());
  REQUIRE(result.has_value());
  REQUIRE(result->signaling == SbrSignaling::implicit_decoded);
  REQUIRE(probe.calls == 0);
}

// --- D-12's FALLBACK: reachable ONLY when the header pass resolved no
// profile at all (libav could not decode a single frame) -----------------

TEST_CASE("audio_config - resolve_sbr_signaling: with NO header-pass profile, a bare AOT_AAC_LC ASC over a "
          "genuinely SBR stream resolves to implicit_decoded via exactly one bounded probe call",
          "[unit]") {
  const auto asc = parse_audio_specific_config(bare_lc_asc_bytes());
  REQUIRE(asc.has_value());
  REQUIRE_FALSE(asc->has_explicit_sbr);

  CountingProbe probe;
  SbrProbeDecodeResult r;
  r.declared_sample_rate_hz = 44100;
  r.decoded_sample_rate_hz = 88200;  // doubled -- genuinely implicit SBR
  r.he_profile = false;
  probe.result = r;

  const auto result =
      resolve_sbr_signaling(/*codec_id_is_aac=*/true, asc, header_resolved_nothing(), probe.fn());
  REQUIRE(result.has_value());
  REQUIRE(result->signaling == SbrSignaling::implicit_decoded);
  REQUIRE(probe.calls == 1);
}

TEST_CASE("audio_config - resolve_sbr_signaling: with NO header-pass profile, a bare AOT_AAC_LC ASC over a stream "
          "with NO SBR at all resolves to none via exactly one bounded probe call",
          "[unit]") {
  const auto asc = parse_audio_specific_config(bare_lc_asc_bytes());
  REQUIRE(asc.has_value());

  CountingProbe probe;
  SbrProbeDecodeResult r;
  r.declared_sample_rate_hz = 44100;
  r.decoded_sample_rate_hz = 44100;  // NOT doubled -- plain LC, no SBR
  r.he_profile = false;
  probe.result = r;

  const auto result =
      resolve_sbr_signaling(/*codec_id_is_aac=*/true, asc, header_resolved_nothing(), probe.fn());
  REQUIRE(result.has_value());
  REQUIRE(result->signaling == SbrSignaling::none);
  REQUIRE(probe.calls == 1);
}

TEST_CASE("audio_config - resolve_sbr_signaling: the bounded probe itself failing (std::nullopt) resolves to "
          "unknown, never a guess",
          "[unit]") {
  const auto asc = parse_audio_specific_config(bare_lc_asc_bytes());
  REQUIRE(asc.has_value());

  CountingProbe probe;  // probe.result stays std::nullopt
  const auto result =
      resolve_sbr_signaling(/*codec_id_is_aac=*/true, asc, header_resolved_nothing(), probe.fn());
  REQUIRE(result.has_value());
  REQUIRE(result->signaling == SbrSignaling::unknown);
  REQUIRE(probe.calls == 1);
}

// 06-18-PLAN.md (CR-05): the RED this task fixes -- before the fix, ANY
// probe failure (an Error included) fell through to `unknown` exactly like
// a clean std::nullopt. A fake probe returning
// Error{input_unsupported, "x"} now propagates that Error UNCHANGED out of
// resolve_sbr_signaling(), rather than being silently rendered as a value.
TEST_CASE("audio_config - resolve_sbr_signaling: a probe Error propagates unchanged as this function's own Error, "
          "not as SbrSignaling::unknown (CR-05)",
          "[unit]") {
  const auto asc = parse_audio_specific_config(bare_lc_asc_bytes());
  REQUIRE(asc.has_value());
  REQUIRE(asc->sampling_frequency_hz == 44100);

  CountingProbe probe;
  probe.error = Error{ErrorKind::input_unsupported, "x"};

  const auto result =
      resolve_sbr_signaling(/*codec_id_is_aac=*/true, asc, header_resolved_nothing(), probe.fn());
  REQUIRE_FALSE(result.has_value());
  REQUIRE(result.error().kind == ErrorKind::input_unsupported);
  REQUIRE(result.error().message == "x");
  REQUIRE(probe.calls == 1);
}

// --- The fallback's own gate: it cannot fire on a stream whose declared
// config rules implicit SBR out ------------------------------------------

TEST_CASE("audio_config - resolve_sbr_signaling: with NO header-pass profile, an ASC declaring a non-AAC-LC "
          "object type resolves to none WITHOUT calling the bounded probe",
          "[unit]") {
  AscBitWriter w;
  w.u(5, 23);  // AOT_AAC_LD -- not a candidate for IMPLICIT SBR signaling
  w.u(4, 4);   // 44100
  w.u(4, 2);   // stereo
  const auto asc = parse_audio_specific_config(w.to_bytes());
  REQUIRE(asc.has_value());
  REQUIRE(asc->object_type == 23);

  CountingProbe probe;
  const auto result =
      resolve_sbr_signaling(/*codec_id_is_aac=*/true, asc, header_resolved_nothing(), probe.fn());
  REQUIRE(result.has_value());
  REQUIRE(result->signaling == SbrSignaling::none);
  REQUIRE(probe.calls == 0);
}

// implicit_sbr_is_possible() directly -- including the explicit record
// that the plausible-sounding "44100 is too high to be an SBR core rate"
// gate is FALSE for this project, whose own audio_sbr_implicit.mp4
// fixture is exactly a 44100 Hz core decoding at 88200 Hz.
TEST_CASE("audio_config - implicit_sbr_is_possible: a 44100 Hz AAC-LC core IS a candidate (88200 is a legal "
          "Table 1.16 rate, and audio_sbr_implicit.mp4 is exactly that case)",
          "[unit]") {
  const auto asc = parse_audio_specific_config(bare_lc_asc_bytes());
  REQUIRE(asc.has_value());
  REQUIRE(asc->sampling_frequency_hz == 44100);
  REQUIRE(implicit_sbr_is_possible(*asc));
}

TEST_CASE("audio_config - implicit_sbr_is_possible: a 96000 Hz core is NOT a candidate (192000 is not an "
          "expressible AAC rate)",
          "[unit]") {
  AscBitWriter w;
  w.u(5, 2);  // AOT_AAC_LC
  w.u(4, 0);  // 96000
  w.u(4, 2);  // stereo
  const auto asc = parse_audio_specific_config(w.to_bytes());
  REQUIRE(asc.has_value());
  REQUIRE(asc->sampling_frequency_hz == 96000);
  REQUIRE_FALSE(implicit_sbr_is_possible(*asc));
}

TEST_CASE("audio_config - implicit_sbr_is_possible: an explicitly-SBR ASC is not an IMPLICIT candidate",
          "[unit]") {
  const auto asc = parse_audio_specific_config(explicit_sbr_asc_bytes());
  REQUIRE(asc.has_value());
  REQUIRE(asc->has_explicit_sbr);
  REQUIRE_FALSE(implicit_sbr_is_possible(*asc));
}

// --- Test 7: a non-AAC codec resolves to none WITHOUT attempting any ASC
// parse -- codec_id_is_aac=false short-circuits before `asc` is even
// consulted, and the probe is never invoked -----------------------------

TEST_CASE("audio_config - resolve_sbr_signaling: a non-AAC codec resolves to none without any ASC parse or probe "
          "call",
          "[unit]") {
  CountingProbe probe;
  // Even an ASC that WOULD resolve explicit if consulted must not change
  // the outcome -- codec_id_is_aac=false gates the whole decision.
  const auto asc = parse_audio_specific_config(explicit_sbr_asc_bytes());
  const auto result =
      resolve_sbr_signaling(/*codec_id_is_aac=*/false, asc, header_resolved_lc(44100), probe.fn());
  REQUIRE(result.has_value());
  REQUIRE(result->signaling == SbrSignaling::none);
  REQUIRE(probe.calls == 0);
}

TEST_CASE("audio_config - resolve_sbr_signaling: a missing ASC AND no header-pass profile resolves to unknown",
          "[unit]") {
  CountingProbe probe;
  const auto result =
      resolve_sbr_signaling(/*codec_id_is_aac=*/true, std::nullopt, header_resolved_nothing(), probe.fn());
  REQUIRE(result.has_value());
  REQUIRE(result->signaling == SbrSignaling::unknown);
  REQUIRE(probe.calls == 0);
}

// ===========================================================================
// Real-probe determinism (06-18-PLAN.md, CR-05):
// detail::probe_implicit_sbr_via_second_open exercised DIRECTLY against
// real fixtures -- proving the zero-wall-clock-budget timeout Error and the
// Error-vs-nullopt classification (flagged assumption A1) without going
// through resolve_sbr_signaling's own decision tree.
// ===========================================================================

TEST_CASE("audio_config - the real probe under a zero wall-clock budget returns a timeout Error, never a value "
          "(CR-05)",
          "[unit]") {
  const ScopedWallClockBudget guard(0);
  const auto probe = mediadiff::detail::probe_implicit_sbr_via_second_open(fixture("audio_sbr_implicit.mp4"), 0);
  REQUIRE_FALSE(probe.has_value());
  REQUIRE(probe.error().kind == ErrorKind::input_unsupported);
  REQUIRE(probe.error().message.find("wall-clock budget") != std::string::npos);
}

TEST_CASE("audio_config - the real probe at the default wall-clock budget decodes audio_sbr_implicit.mp4's stream "
          "0 to 88200 Hz",
          "[unit]") {
  const auto probe = mediadiff::detail::probe_implicit_sbr_via_second_open(fixture("audio_sbr_implicit.mp4"), 0);
  REQUIRE(probe.has_value());
  REQUIRE(probe->has_value());
  REQUIRE((*probe)->decoded_sample_rate_hz == 88200);
}

TEST_CASE("audio_config - the real probe on audio_hash_base.mp4 deterministically returns no frame on two "
          "consecutive calls (its first packet is entirely encoder-delay priming)",
          "[unit]") {
  const auto probe1 = mediadiff::detail::probe_implicit_sbr_via_second_open(fixture("audio_hash_base.mp4"), 0);
  const auto probe2 = mediadiff::detail::probe_implicit_sbr_via_second_open(fixture("audio_hash_base.mp4"), 0);
  REQUIRE(probe1.has_value());
  REQUIRE(probe2.has_value());
  REQUIRE_FALSE(probe1->has_value());
  REQUIRE_FALSE(probe2->has_value());
}

// ===========================================================================
// Real-fixture tests: StreamInfo::sbr_signaling/effective_sample_rate_hz
// through a real DemuxSession, proving src/probe/demux_session.cpp's own
// bounded one-packet decode (the REAL SbrProbeFn implementation) actually
// distinguishes implicit SBR from plain LC against 06-02's hand-written
// HE-AAC fixture pair. Test 8's own must_have.
// ===========================================================================

TEST_CASE("audio_config - audio_sbr_explicit.mp4: sbr_signaling is explicit_asc, effective rate equals the core "
          "rate (already doubled by the demuxer itself, never doubled again here)",
          "[unit]") {
  auto session = DemuxSession::open(fixture("audio_sbr_explicit.mp4"), DemuxOptions{});
  REQUIRE(session.has_value());
  const StreamInfo info = session->stream_info(0);
  REQUIRE(info.sbr_signaling == SbrSignaling::explicit_asc);
  REQUIRE(info.sample_rate.has_value());
  REQUIRE(info.effective_sample_rate_hz == *info.sample_rate);
}

TEST_CASE("audio_config - audio_sbr_implicit.mp4: sbr_signaling is implicit_decoded from the HEADER PASS's own "
          "post-find_stream_info codecpar, effective rate is that resolved rate (never a formulaic doubling)",
          "[unit]") {
  // 06-13-PLAN.md (D-12 as DECIDED): verified against THIS project's
  // linked FFmpeg 8.1 -- for this fixture avformat_find_stream_info()
  // resolves codecpar->sample_rate to the SBR-doubled 88200 (the ASC
  // declares 44100) AND codecpar->profile to AV_PROFILE_AAC_HE. Either
  // signal alone identifies implicit SBR, so D-12's primary mechanism
  // answers here with no second open and no bounded fallback decode.
  // A formulaic `core_rate * 2` would fabricate a false, quadrupled
  // 176400 Hz -- which is exactly why the effective rate is read, never
  // computed.
  auto session = DemuxSession::open(fixture("audio_sbr_implicit.mp4"), DemuxOptions{});
  REQUIRE(session.has_value());
  const StreamInfo info = session->stream_info(0);
  REQUIRE(info.sbr_signaling == SbrSignaling::implicit_decoded);
  REQUIRE(info.sample_rate.has_value());
  REQUIRE(info.effective_sample_rate_hz == *info.sample_rate);
  REQUIRE(info.effective_sample_rate_hz == 88200);
}

TEST_CASE("audio_config - audio_aac_handwritten.mp4: a plain AAC-LC stream with no SBR at all resolves to none, "
          "effective rate equals the core rate",
          "[unit]") {
  auto session = DemuxSession::open(fixture("audio_aac_handwritten.mp4"), DemuxOptions{});
  REQUIRE(session.has_value());
  const StreamInfo info = session->stream_info(0);
  REQUIRE(info.sbr_signaling == SbrSignaling::none);
  REQUIRE(info.sample_rate.has_value());
  REQUIRE(info.effective_sample_rate_hz == *info.sample_rate);
}

// 06-13-PLAN.md: the regression test for the defect 06-04's
// implementation shipped. audio_hash_base.mp4 is an ORDINARY
// encoder-produced AAC-LC file, and its first packet is consumed entirely
// as 1024-sample encoder-delay priming (AV_PKT_DATA_SKIP_SAMPLES
// start_skip=1024) -- so the bounded one-packet fallback probe gets
// EAGAIN from avcodec_receive_frame and can never resolve it, which is
// why this file reported `unknown` before this fix. It must report
// `none`: the header pass resolved AV_PROFILE_AAC_LOW at the undoubled
// declared rate, which IS a determination. `unknown` here would be a
// latent false finding, since D-14 makes `unknown` compare as its own
// value.
TEST_CASE("audio_config - audio_hash_base.mp4 (a NORMALLY-ENCODED AAC-LC file whose first packet is entirely "
          "encoder-delay priming) resolves to none, never unknown",
          "[unit]") {
  auto session = DemuxSession::open(fixture("audio_hash_base.mp4"), DemuxOptions{});
  REQUIRE(session.has_value());
  const StreamInfo info = session->stream_info(0);
  REQUIRE(info.sbr_signaling == SbrSignaling::none);
  REQUIRE(info.sample_rate.has_value());
  REQUIRE(*info.sample_rate == 44100);
  REQUIRE(info.effective_sample_rate_hz == 44100);
}

// The same determination for an MPEG-TS stream copy, which carries ADTS
// and therefore no ASC extradata at all -- the header pass's own resolved
// profile is the whole basis for the answer there.
TEST_CASE("audio_config - audio_hash_base.ts (ADTS, no ASC extradata at all) resolves to none from the header "
          "pass's own resolved profile",
          "[unit]") {
  auto session = DemuxSession::open(fixture("audio_hash_base.ts"), DemuxOptions{});
  REQUIRE(session.has_value());
  const StreamInfo info = session->stream_info(0);
  REQUIRE(info.sbr_signaling == SbrSignaling::none);
}

// --- A non-audio stream never attempts any SBR resolution -----------------

TEST_CASE("audio_config - a video stream (video_base.mp4) resolves sbr_signaling to none, effective rate is 0",
          "[unit]") {
  auto session = DemuxSession::open(fixture("video_base.mp4"), DemuxOptions{});
  REQUIRE(session.has_value());
  const StreamInfo info = session->stream_info(0);
  REQUIRE(info.sbr_signaling == SbrSignaling::none);
  REQUIRE(info.effective_sample_rate_hz == 0);
}
