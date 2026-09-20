// 06-04-PLAN.md Task 1 (AUDIO-03, D-12): resolve_sbr_signaling() -- the
// no-decode ASC fast path (a top-level AOT_SBR object type, or a 0x2b7
// backward-compatible sync extension) and the bounded one-packet fallback
// (only when the ASC parses as a bare, ambiguous object type), both
// exercised directly against mediadiff::resolve_sbr_signaling() with a
// call-counting fake SbrProbeFn -- no file, no real decoder, mirroring
// tests/unit/test_gop_classification.cpp's own SpsBitWriter-plus-synthetic-
// buffer convention for a small, hand-verified bitstream grammar.
//
// A second block of tests (marked "real fixture" below) exercises
// StreamInfo::sbr_signaling/effective_sample_rate_hz end to end through a
// real DemuxSession opened against 06-02's hand-written HE-AAC fixture
// pair, proving the REAL bounded decode (src/probe/demux_session.cpp's
// own probe_implicit_sbr_via_second_open) actually distinguishes implicit
// SBR from plain LC.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "probe/audio_config.h"
#include "probe/demux_session.h"
#include "support/fixture_paths.h"

using mediadiff::AudioObjectType;
using mediadiff::AudioSpecificConfig;
using mediadiff::DemuxOptions;
using mediadiff::DemuxSession;
using mediadiff::parse_audio_specific_config;
using mediadiff::resolve_sbr_signaling;
using mediadiff::SbrProbeDecodeResult;
using mediadiff::SbrSignaling;
using mediadiff::StreamInfo;

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

// --- Test 1: explicit top-level AOT_SBR -> explicit_asc, NO decode -------

TEST_CASE("audio_config - resolve_sbr_signaling: a top-level AOT_SBR ASC resolves to explicit_asc with no decode",
          "[unit]") {
  const auto asc = parse_audio_specific_config(explicit_sbr_asc_bytes());
  REQUIRE(asc.has_value());
  REQUIRE(asc->has_explicit_sbr);

  int probe_calls = 0;
  const mediadiff::SbrProbeFn probe = [&probe_calls]() -> std::optional<SbrProbeDecodeResult> {
    ++probe_calls;
    return std::nullopt;
  };
  const SbrSignaling result = resolve_sbr_signaling(/*codec_id_is_aac=*/true, asc, probe);
  REQUIRE(result == SbrSignaling::explicit_asc);
  REQUIRE(probe_calls == 0);
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

  int probe_calls = 0;
  const mediadiff::SbrProbeFn probe = [&probe_calls]() -> std::optional<SbrProbeDecodeResult> {
    ++probe_calls;
    return std::nullopt;
  };
  const SbrSignaling result = resolve_sbr_signaling(/*codec_id_is_aac=*/true, asc, probe);
  REQUIRE(result == SbrSignaling::explicit_asc);
  REQUIRE(probe_calls == 0);
}

// --- Test 3/4: a bare, ambiguous LC ASC -- the bounded probe is called
// EXACTLY ONCE, and its result distinguishes implicit_decoded from none ---

TEST_CASE("audio_config - resolve_sbr_signaling: a bare AOT_AAC_LC ASC over a genuinely SBR stream resolves to "
          "implicit_decoded via exactly one bounded probe call",
          "[unit]") {
  const auto asc = parse_audio_specific_config(bare_lc_asc_bytes());
  REQUIRE(asc.has_value());
  REQUIRE_FALSE(asc->has_explicit_sbr);

  int probe_calls = 0;
  const mediadiff::SbrProbeFn probe = [&probe_calls]() -> std::optional<SbrProbeDecodeResult> {
    ++probe_calls;
    SbrProbeDecodeResult r;
    r.declared_sample_rate_hz = 44100;
    r.decoded_sample_rate_hz = 88200;  // doubled -- genuinely implicit SBR
    r.he_profile = false;
    return r;
  };
  const SbrSignaling result = resolve_sbr_signaling(/*codec_id_is_aac=*/true, asc, probe);
  REQUIRE(result == SbrSignaling::implicit_decoded);
  REQUIRE(probe_calls == 1);
}

TEST_CASE("audio_config - resolve_sbr_signaling: a bare AOT_AAC_LC ASC over a stream with NO SBR at all resolves "
          "to none via exactly one bounded probe call",
          "[unit]") {
  const auto asc = parse_audio_specific_config(bare_lc_asc_bytes());
  REQUIRE(asc.has_value());
  REQUIRE_FALSE(asc->has_explicit_sbr);

  int probe_calls = 0;
  const mediadiff::SbrProbeFn probe = [&probe_calls]() -> std::optional<SbrProbeDecodeResult> {
    ++probe_calls;
    SbrProbeDecodeResult r;
    r.declared_sample_rate_hz = 44100;
    r.decoded_sample_rate_hz = 44100;  // NOT doubled -- plain LC, no SBR
    r.he_profile = false;
    return r;
  };
  const SbrSignaling result = resolve_sbr_signaling(/*codec_id_is_aac=*/true, asc, probe);
  REQUIRE(result == SbrSignaling::none);
  REQUIRE(probe_calls == 1);
}

TEST_CASE("audio_config - resolve_sbr_signaling: the bounded probe itself failing (std::nullopt) resolves to "
          "unknown, never a guess",
          "[unit]") {
  const auto asc = parse_audio_specific_config(bare_lc_asc_bytes());
  REQUIRE(asc.has_value());

  int probe_calls = 0;
  const mediadiff::SbrProbeFn probe = [&probe_calls]() -> std::optional<SbrProbeDecodeResult> {
    ++probe_calls;
    return std::nullopt;
  };
  const SbrSignaling result = resolve_sbr_signaling(/*codec_id_is_aac=*/true, asc, probe);
  REQUIRE(result == SbrSignaling::unknown);
  REQUIRE(probe_calls == 1);
}

// --- Test 7: a non-AAC codec resolves to none WITHOUT attempting any ASC
// parse -- codec_id_is_aac=false short-circuits before `asc` is even
// consulted, and the probe is never invoked -----------------------------

TEST_CASE("audio_config - resolve_sbr_signaling: a non-AAC codec resolves to none without any ASC parse or probe "
          "call",
          "[unit]") {
  int probe_calls = 0;
  const mediadiff::SbrProbeFn probe = [&probe_calls]() -> std::optional<SbrProbeDecodeResult> {
    ++probe_calls;
    return std::nullopt;
  };
  // Even an ASC that WOULD resolve explicit if consulted must not change
  // the outcome -- codec_id_is_aac=false gates the whole decision.
  const auto asc = parse_audio_specific_config(explicit_sbr_asc_bytes());
  const SbrSignaling result = resolve_sbr_signaling(/*codec_id_is_aac=*/false, asc, probe);
  REQUIRE(result == SbrSignaling::none);
  REQUIRE(probe_calls == 0);
}

TEST_CASE("audio_config - resolve_sbr_signaling: a missing ASC (no extradata) resolves to unknown", "[unit]") {
  int probe_calls = 0;
  const mediadiff::SbrProbeFn probe = [&probe_calls]() -> std::optional<SbrProbeDecodeResult> {
    ++probe_calls;
    return std::nullopt;
  };
  const SbrSignaling result = resolve_sbr_signaling(/*codec_id_is_aac=*/true, std::nullopt, probe);
  REQUIRE(result == SbrSignaling::unknown);
  REQUIRE(probe_calls == 0);
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

TEST_CASE("audio_config - audio_sbr_implicit.mp4: sbr_signaling is implicit_decoded via the REAL bounded decode, "
          "effective rate is the probe's own decoded rate (not a formulaic doubling of the core rate)",
          "[unit]") {
  // 06-04-PLAN.md deviation (Rule 1): empirically, avformat_find_stream_info()'s
  // own internal probing already resolves codecpar->sample_rate to the
  // SBR-doubled value for this fixture (88200, not the ASC-declared 44100) --
  // reproducible identically with and without --content, i.e. entirely
  // within the header pass. A formulaic `core_rate * 2` here would therefore
  // fabricate a false, quadrupled 176400 Hz. DemuxSession::compute_sbr_signaling()
  // instead caches the bounded probe's OWN directly-observed decoded rate
  // (implicit_probe_rate_hz_), so effective_sample_rate_hz is provably
  // correct regardless of whether codecpar already carries the doubled
  // value. For this fixture the probe's own decode and codecpar's own
  // already-resolved rate agree, so effective equals core here too.
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

// --- A non-audio stream never attempts any SBR resolution -----------------

TEST_CASE("audio_config - a video stream (video_base.mp4) resolves sbr_signaling to none, effective rate is 0",
          "[unit]") {
  auto session = DemuxSession::open(fixture("video_base.mp4"), DemuxOptions{});
  REQUIRE(session.has_value());
  const StreamInfo info = session->stream_info(0);
  REQUIRE(info.sbr_signaling == SbrSignaling::none);
  REQUIRE(info.effective_sample_rate_hz == 0);
}
