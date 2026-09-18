// 05-05-PLAN.md Task 3 (TIME-01/TIME-04, DOC-04): the DOC-04 no-others
// harness (tests/integration/timeline_findings.h) proven against the two
// crafted fixtures this plan's Task 1 generates --
// tests/fixtures/timeline_dts_backward.ts and
// tests/fixtures/timeline_pts_dupe.mp4 -- plus the byte-identical clean
// pair every other tracer in this phase reuses.
//
// 05-06-PLAN.md Task 3 (TIME-02/TIME-04, DOC-04) extends this file with the
// same harness applied to timeline.gaps and timeline.wrap_events, over
// tests/fixtures/timeline_gap.mp4, tests/fixtures/timeline_ts_wrap.ts and
// tests/fixtures/timeline_ts_nowrap.ts/_copy.ts, plus a dedicated case
// proving doc 04 section 5's own zero-false-positive acceptance criterion.
//
// 05-07-PLAN.md Task 3 (TIME-02/TIME-04, DOC-04) extends this file once
// more with timeline.discontinuities and timeline.discontinuities.flagged,
// over tests/fixtures/timeline_ts_jump.ts (an unflagged >250ms
// presentation-time jump, proven against tests/fixtures/timeline_ts_nowrap.ts)
// and tests/fixtures/timeline_ts_jump_flagged.ts (the SAME file with exactly
// one transport packet's own discontinuity_indicator bit set, proving the
// attribution split against timeline_ts_jump.ts itself).
//
// Every TEST_CASE below carries the literal prefix "timeline_structure - "
// so `ctest -R "integration\.timeline_structure"` selects exactly this
// file's cases (TEST_PREFIX "integration." makes the real ctest name
// "integration.<TEST_CASE name>"; an unmatched -R filter exits ZERO and
// prints "No tests were found", which is why the prefix matters).

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

#include <nlohmann/json.hpp>

#include "cli_harness.h"
#include "support/fixture_paths.h"
#include "timeline_findings.h"

using mediadiff::test::CliResult;
using mediadiff::test::count_non_pass;
using mediadiff::test::expect_declared_set;
using mediadiff::test::run_cli;

namespace {

namespace fs = std::filesystem;

std::string fixture(const std::string& name) { return mediadiff::test::fixture_dir() + "/" + name; }

void require_fixture(const std::string& path) {
  INFO("required fixture is missing: " << path);
  REQUIRE(fs::exists(path));
}

nlohmann::ordered_json compare_json(const std::string& baseline, const std::string& candidate,
                                     const std::string& profile) {
  require_fixture(baseline);
  require_fixture(candidate);
  const CliResult result = run_cli({"compare", baseline, candidate, "--profile", profile, "--json"});
  const nlohmann::ordered_json report = nlohmann::ordered_json::parse(result.out, nullptr, false);
  INFO("compare stdout: " << result.out << "\ncompare stderr: " << result.err);
  REQUIRE_FALSE(report.is_discarded());
  REQUIRE(report.contains("findings"));
  return report;
}

}  // namespace

// --- Test 1: timeline_start_base.mp4 vs timeline_dts_backward.ts -----------
//
// timeline_dts_backward.ts is NOT a `setts`-crafted single encode (05-05-
// SUMMARY.md's Deviations section records why: a genuinely backward DTS is
// structurally impossible to write via ffmpeg's own CLI/muxer for EVERY
// container, `setts`+`-c copy` included). It is two independently-muxed,
// independently RE-ENCODED 2-second MPEG-TS segments concatenated
// byte-for-byte. That re-encode -- not a `-c copy` remux -- is the single
// root cause behind every member of this declared set: a fresh mpeg4/aac
// encode at half the duration, spliced, produces its own container
// format, its own (genuinely halved, splice-corrupted) declared duration,
// its own byte size/bitrate, its own tag set, AND the one
// timeline.dts_monotonic violation per stream this fixture exists to
// prove -- all one cause (D-02), each member below carries its own
// verified-against-the-real-binary reason.
TEST_CASE("timeline_structure - the dts_backward trigger pair declares its complete expected finding set under "
          "--profile remux, and count_non_pass equals that set's size exactly",
          "[integration]") {
  const nlohmann::ordered_json report =
      compare_json(fixture("timeline_start_base.mp4"), fixture("timeline_dts_backward.ts"), "remux");

  expect_declared_set(
      report,
      {
          // The container format itself genuinely changed (mov -> mpegts),
          // same as every other MP4-to-TS pair in this phase.
          "container.format",
          // The candidate's own PTS cadence is corrupted by the splice
          // discontinuity (segment B's PTS values are lower than segment
          // A's, so the derived mode interval and conformance ratio no
          // longer resemble a clean 25fps CFR sequence) -- a genuine,
          // observed property of splicing two independent segments, not a
          // defect this analyzer introduces.
          "video.frame_rate.measured",
          // The candidate's own audio-origin first PTS (126000 ticks @
          // 90000Hz, i.e. segment A's own mux-delay-anchored start) differs
          // from the baseline MP4's edit-list-derived origin -- the same
          // MP4-to-TS mux-delay shift the existing timeline_start_base.mp4/
          // timeline_start_shift.ts tracer pair already declares.
          "timeline.start",
          // The candidate's demuxer-declared duration reflects the
          // SPLICED, discontinuous timestamp sequence, not the true 4s of
          // encoded content -- a real, observed consequence of the splice
          // (verified against the real binary's own evidence:
          // computed_ms/container_declared_ms/stream_declared_ms all land
          // well under 4000ms on both streams), never a defect this
          // analyzer introduces.
          "timeline.duration",
          "timeline.duration",
          // Same splice-corrupted duration triple, now disagreeing with
          // itself on the audio stream specifically (container_vs_stream
          // and stream_vs_computed both flagged) -- one more legitimate
          // effect of the same root cause.
          "timeline.duration.coherence",
          // The check this task registers -- one dts[i] <= dts[i-1]
          // violation on EACH stream at the splice point (verified via
          // `mediadiff compare --json` evidence: first_violation_index 50
          // on video, 88 on audio, both with unwrapped:true since the
          // candidate is MPEG-TS).
          "timeline.dts_monotonic",
          "timeline.dts_monotonic",
          // 05-06-PLAN.md's own timeline.gaps check, registered after this
          // fixture: the SAME splice discontinuity that corrupts
          // timeline.duration/.coherence above also opens one genuine hole
          // in the candidate's AUDIO presentation timeline at the splice
          // point (verified via `mediadiff compare --json` evidence:
          // candidate gap_count 1, span {1678ms,1701ms}, baseline gap_count
          // 0 on both streams). The VIDEO stream's own gap_count stays 0 on
          // both sides -- the splice-corrupted PTS cadence is close enough
          // to the reconstructed per-packet declared duration on video not
          // to cross this check's own threshold -- so this id appears
          // exactly once here, not twice. One more legitimate effect of the
          // same root cause (D-02).
          "timeline.gaps",
          // 05-08-PLAN.md's own timeline.vfr_profile check: the SAME splice
          // discontinuity corrupts the candidate's interval-distribution
          // histogram on BOTH streams relative to the clean baseline's own
          // (near-)100% on_grid distribution -- verified via `mediadiff
          // compare --json`: both streams report their worst bin (on_grid)
          // exceeding the 2% dist tolerance. timeline.jitter itself
          // SKIPS on both streams (skipped:vfr, not a finding, so nothing
          // to declare here) -- derive_cadence's own D-05 grid-conformance
          // test classifies the spliced, discontinuous whole-file timeline
          // as VFR, the same genuine cadence corruption
          // video.frame_rate.measured already declares above. One more
          // legitimate effect of the same root cause (D-02).
          "timeline.vfr_profile",
          "timeline.vfr_profile",
          // A fresh two-segment mpeg4/aac re-encode is genuinely a
          // different byte size, stream bitrate, peak bitrate and overhead
          // ratio than the original single 4s MP4 encode -- expected for
          // any independent re-encode, not a defect this analyzer
          // introduces.
          "size.file",
          "size.stream_bitrate",
          "size.stream_bitrate",
          "size.peak_bitrate",
          "size.peak_bitrate",
          "size.overhead",
          // MP4's ftyp/handler tags (major_brand, compatible_brands,
          // minor_version, per-stream language) have no MPEG-TS equivalent
          // the demuxer surfaces the same way -- the SAME MP4-to-TS
          // container-family effect the existing tracer pair declares,
          // firing once at `global` scope and once per stream here (three
          // times total: global, video, audio).
          "meta.tags",
          "meta.tags",
          "meta.tags",
          // 05-10-PLAN.md Task 2's own K=32 checkpoint fit: the SAME
          // splice-corrupted timestamp sequence `timeline.duration.
          // coherence` above already documents (a real, structural
          // property of the two-segment splice, not per-checkpoint
          // rounding noise) gives the candidate a genuine -92ms
          // accumulated end delta against the clean MP4 baseline's own
          // 0ms -- D-07's dual gate (a delta-based test, the SAME shape
          // as every other magnitude this comparator checks) clears
          // comfortably past its 2ms epsilon, so `timeline.av_drift`
          // (the RATE) genuinely fails, not just `timeline.
          // av_drift.pattern` (which has no tolerance at all by design,
          // D-04, locked one-way, and reports the resulting
          // classification flip). One more legitimate effect of the
          // same splice root cause (D-02).
          "timeline.av_drift",
          "timeline.av_drift.pattern",
      });
}

// --- Test 2: timeline_start_base.mp4 vs timeline_pts_dupe.mp4 --------------
//
// timeline_pts_dupe.mp4 IS the plan's own `setts`-crafted single encode:
// identical testsrc2/sine source, identical mpeg4/aac encode, with exactly
// frame N=50's own PTS rewritten to the next packet's PTS value. Every
// other property of the encode (container, duration, size, tags, DTS
// sequence) is untouched, so this pair's declared set is exactly the one
// check this fixture exists to isolate.
TEST_CASE("timeline_structure - the pts_dupe trigger pair declares its complete expected finding set under "
          "--profile remux, and count_non_pass equals that set's size exactly",
          "[integration]") {
  const nlohmann::ordered_json report =
      compare_json(fixture("timeline_start_base.mp4"), fixture("timeline_pts_dupe.mp4"), "remux");

  // One cause (the setts PTS rewrite), one effect: exactly one duplicate
  // PTS pair on the video stream (verified via `mediadiff compare --json`
  // evidence: first_duplicate_value 26112, duplicate_indices [50, 51]).
  // DTS is left untouched by this recipe and stays strictly monotonic, so
  // timeline.dts_monotonic does NOT appear here.
  //
  // 05-08-PLAN.md's own timeline.vfr_profile check: the single duplicated
  // PTS collapses one interval to zero ticks, which is enough to push the
  // VIDEO stream's own worst bin (on_grid) past the 2% dist tolerance
  // (verified via `mediadiff compare --json`); the AUDIO stream, untouched
  // by the rewrite, stays `pass`. timeline.jitter itself SKIPS on the video
  // stream (skipped:vfr, not a finding) since the single collapsed
  // interval is enough for derive_cadence's own D-05 grid-conformance test
  // to reclassify the whole-file cadence as VFR -- one more legitimate
  // effect of the same one-PTS-rewrite root cause (D-02).
  expect_declared_set(report, {
                                   "timeline.pts_unique",
                                   "timeline.vfr_profile",
                               });
}

// --- Test 3: timeline_start_base.mp4 vs timeline_gap.mp4 -------------------
//
// timeline_gap.mp4 is a `setts`-crafted single encode: identical
// testsrc2/sine source and mpeg4/aac encode, with every video packet's own
// PTS from N=50 onward shifted forward by exactly 3 codec-timebase frames
// (DTS left untouched -- see this plan's own scripts/gen_corpus.sh recipe
// comment for why a combined PTS+DTS shift self-heals against MP4's own
// stts-derived declared duration and never triggers a gap). One cause (the
// PTS-only shift), several legitimately moved facts (D-02): the shift
// pushes every subsequent packet's PTS 1536 ticks later, so the track's
// own overall duration, its own MP4 edit-list segment_duration, and its
// own measured-cadence conformance ratio all genuinely change alongside
// the one presentation-order hole this fixture exists to prove. DTS is
// never touched, so timeline.dts_monotonic and timeline.pts_unique stay
// clean and do not appear here.
TEST_CASE(
    "timeline_structure - the gap trigger pair declares its complete expected finding set under --profile remux, "
    "and count_non_pass equals that set's size exactly",
    "[integration]") {
  const nlohmann::ordered_json report =
      compare_json(fixture("timeline_start_base.mp4"), fixture("timeline_gap.mp4"), "remux");

  expect_declared_set(
      report,
      {
          // The MP4 edit-list's own `trim` entry segment_duration is
          // computed from the track's own (now shift-lengthened) media
          // duration -- verified via evidence: baseline
          // segment_duration=176400, candidate segment_duration=181692.
          "container.mp4.edit_list",
          // The shifted packets' own PTS cadence no longer conforms to a
          // clean 25fps CFR sequence past the shift point (verified via
          // evidence: candidate conforming_timestamps drops from 100 to 2,
          // class flips from cfr to vfr) -- a direct, expected consequence
          // of moving PTS values without moving the frame count.
          "video.frame_rate.measured",
          // The track's own overall duration grows by exactly the 120ms
          // the 3-frame/1536-tick shift adds (verified via evidence:
          // baseline computed_ms 4000, candidate computed_ms 4120,
          // beyond the fixed 40ms fail threshold).
          "timeline.duration",
          // The candidate's demuxer-declared duration values (container/
          // stream, both still 4000ms -- their own bookkeeping never saw
          // the shift) now disagree with the shift-lengthened computed
          // duration (4120ms) -- container_vs_computed and
          // stream_vs_computed both flagged, at info severity per this
          // check's own state semantic.
          "timeline.duration.coherence",
          // The check this task registers -- one genuine hole in the
          // video presentation timeline at the shift point (verified via
          // evidence: candidate gap_count 1, span {1960ms,2120ms}).
          "timeline.gaps",
          // 05-08-PLAN.md's own timeline.vfr_profile check: the same
          // 3-frame shift pushes the VIDEO stream's own worst bin (longer)
          // past the 2% dist tolerance (verified via `mediadiff compare
          // --json`); the AUDIO stream, untouched by the shift, stays
          // `pass`. timeline.jitter itself SKIPS on the video stream
          // (skipped:vfr, not a finding) since derive_cadence's own D-05
          // grid-conformance test reclassifies the shift-corrupted
          // whole-file cadence as VFR, the same reclassification
          // video.frame_rate.measured's own evidence already declares
          // above. One more legitimate effect of the same root cause
          // (D-02).
          "timeline.vfr_profile",
      });
}

// --- Test 4: timeline_ts_nowrap.ts vs timeline_ts_wrap.ts ------------------
//
// timeline_ts_wrap.ts is a fresh direct testsrc2/sine/mpeg4/aac encode
// straight to MPEG-TS with `-output_ts_offset 95440.34` (an output-side
// option, applied at encode time, not a `-c copy` remux) producing a
// genuine on-the-wire 33-bit PTS/DTS wrap partway through the file.
// timeline_ts_nowrap.ts is the identical recipe with no offset -- the
// `state` semantic's own asymmetry (one side flagged, one not) is what
// makes this pair a trigger.
//
// Deliberately NOT an expect_declared_set/whole-report assertion here
// (contrast Test 3's gap trigger pair above): this task's own Rule 1/2
// deviation (demux_session.{h,cpp}'s correct_ts_overflow=0, needed so
// unwrap_ts_timestamps ever sees a genuine wrap at all -- see this plan's
// own commit message) has the side effect of exposing that
// timeline.start, timeline.duration, timeline.duration.coherence,
// video.frame_rate.measured and size.stream_bitrate all read RAW,
// un-unwrapped PTS/DTS axis values directly (never through the shared
// unwrap this plan's own two checks use), producing nonsensical evidence
// on ANY genuinely-wrapping TS file (one observed instance: size.
// stream_bitrate's corrupted rationals used to overflow the tolerance
// comparator's int64_t cross-multiplication and report `status: error`;
// since debug session test-898-ci-nonreproducible made that comparator
// exact, the same corrupted inputs yield an equally meaningless `fail`
// instead -- the defect is the un-unwrapped INPUT, WINDOWS.md #26, not
// the comparison). That is a REAL, pre-existing correctness gap
// this plan's own fix newly makes reachable -- not something a real user
// would want silently folded into an "expected" declared set (FALSE
// POSITIVES ARE P0). It is out of THIS plan's declared scope (05-06-
// PLAN.md's Task 2 is timeline_monotonic_analyzer() only; the affected
// checks live in three different analyzer files/groups) and is recorded
// as a follow-up gap in this plan's own SUMMARY.md rather than papered
// over here. This test therefore asserts ONLY the one finding this
// fixture pair exists to prove, the same scope discipline
// doc03_coverage's own statuses_for() helper already applies.
TEST_CASE("timeline_structure - the wrap trigger pair's timeline.wrap_events finding is the state-semantic "
          "non-pass case on both streams under --profile remux",
          "[integration]") {
  const nlohmann::ordered_json report =
      compare_json(fixture("timeline_ts_nowrap.ts"), fixture("timeline_ts_wrap.ts"), "remux");

  int wrap_events_non_pass = 0;
  for (const auto& finding : report.at("findings")) {
    if (finding.at("id").get<std::string>() != "timeline.wrap_events") {
      continue;
    }
    INFO("timeline.wrap_events finding: " << finding.dump(2));
    REQUIRE(finding.at("baseline").get<std::string>() == "no_wrap");
    REQUIRE(finding.at("candidate").get<std::string>() == "ts_33bit_wrap");
    REQUIRE(finding.at("status").get<std::string>() != "pass");
    REQUIRE(finding.at("status").get<std::string>() != "skipped");
    ++wrap_events_non_pass;
  }
  // Fires once per stream (video, audio): the offset wraps BOTH streams'
  // own 90kHz PES timestamps at roughly the same point in the file.
  REQUIRE(wrap_events_non_pass == 2);
}

// --- Test 5: the wrap fixture's own byte-identical clean pair --------------
//
// timeline_ts_nowrap.ts vs its own byte-identical copy -- deliberately a
// SEPARATE clean pair from timeline_start_base.mp4/_copy.mp4 (reused by
// every other tracer in this phase) because timeline.wrap_events'
// `not_applicable_container` skip on non-TS inputs would make an MP4 clean
// pair prove nothing about this check specifically. Both sides here are
// TS and both report the unflagged `no_wrap` value -- the state semantic's
// own `pass` requirement (neither side flagged, never "both agree").
//
// One pre-existing, non-wrap-related fact rides along (D-02): a freshly
// TS-muxed AAC audio stream's own container-declared duration (4023ms)
// disagrees with its own stream-declared/computed durations (3877ms/
// 4040ms) -- the SAME class of MPEG-TS audio-duration-bookkeeping
// disagreement test_timeline_start_duration.cpp already declares for
// timeline_start_shift.ts (05-04-PLAN.md's own precedent), here on BOTH
// sides of a byte-identical pair since it is a property of the encode
// itself, not of the wrap. Verified via evidence: both baseline and
// candidate flag "container_vs_stream", `status: info`
// (timeline.duration.coherence's own info severity), never gating.
TEST_CASE(
    "timeline_structure - the wrap fixture's own byte-identical clean pair declares only the pre-existing "
    "TS-audio-duration artifact, and count_non_pass equals that set's size exactly",
    "[integration]") {
  const nlohmann::ordered_json report =
      compare_json(fixture("timeline_ts_nowrap.ts"), fixture("timeline_ts_nowrap_copy.ts"), "remux");
  expect_declared_set(report, {
                                   "timeline.duration.coherence",
                               });
}

// --- Test 6: doc 04 section 5's own acceptance criterion -------------------
//
// doc 04 section 5 requires that a genuine mid-file 33-bit wrap produces
// ZERO false gaps and ZERO false timeline.dts_monotonic violations. Proven
// here by comparing timeline_ts_wrap.ts against ITSELF: both
// timeline.dts_monotonic and timeline.gaps must report `pass` with a
// `gap_count` of exactly 0 on both streams, while timeline.wrap_events
// still correctly reports the wrap as a non-pass, state-semantic "both
// values are flagged" finding (proving the wrap was genuinely detected,
// not silently absorbed) -- exactly the report this plan's own commit
// message transcript records against the real binary.
TEST_CASE(
    "timeline_structure - a genuine mid-file 33-bit wrap compared against itself produces zero false gaps and "
    "zero false dts_monotonic violations (doc 04 section 5)",
    "[integration]") {
  const nlohmann::ordered_json report =
      compare_json(fixture("timeline_ts_wrap.ts"), fixture("timeline_ts_wrap.ts"), "remux");

  bool saw_dts_monotonic = false;
  bool saw_gaps = false;
  bool saw_wrap_events = false;
  for (const auto& finding : report.at("findings")) {
    const std::string id = finding.at("id").get<std::string>();
    const std::string status = finding.at("status").get<std::string>();
    if (id == "timeline.dts_monotonic") {
      saw_dts_monotonic = true;
      INFO("timeline.dts_monotonic finding: " << finding.dump(2));
      REQUIRE(status == "pass");
    } else if (id == "timeline.gaps") {
      saw_gaps = true;
      INFO("timeline.gaps finding: " << finding.dump(2));
      REQUIRE(status == "pass");
      REQUIRE(finding.at("evidence").at("baseline").at("gap_count").get<int>() == 0);
      REQUIRE(finding.at("evidence").at("candidate").at("gap_count").get<int>() == 0);
    } else if (id == "timeline.wrap_events") {
      saw_wrap_events = true;
      INFO("timeline.wrap_events finding: " << finding.dump(2));
      REQUIRE(status != "pass");
      REQUIRE(status != "skipped");
      REQUIRE(finding.at("baseline").get<std::string>() == "ts_33bit_wrap");
      REQUIRE(finding.at("candidate").get<std::string>() == "ts_33bit_wrap");
    }
  }
  REQUIRE(saw_dts_monotonic);
  REQUIRE(saw_gaps);
  REQUIRE(saw_wrap_events);
}

// --- Test 7: the byte-identical clean pair's empty declared set ------------
//
// Reuses the same clean pair every tracer in this phase declares
// (timeline_start_base.mp4 vs its own byte-identical copy) -- included
// directly in this file too so a reader auditing test_timeline_structure.cpp
// alone can see both this plan's own checks report a clean bill of health
// on a genuinely unchanged file, without having to cross-reference
// test_timeline_start_duration.cpp.
TEST_CASE("timeline_structure - the byte-identical clean pair declares the empty set and count_non_pass is zero",
          "[integration]") {
  const nlohmann::ordered_json report =
      compare_json(fixture("timeline_start_base.mp4"), fixture("timeline_start_base_copy.mp4"), "remux");
  expect_declared_set(report, {});
  REQUIRE(count_non_pass(report) == 0);
}

// --- Test 8: timeline_ts_nowrap.ts vs timeline_ts_jump.ts ------------------
//
// timeline_ts_jump.ts is built by the SAME two-independently-muxed-TS-
// segments-concatenated-via-`cat` technique as Test 1's timeline_dts_
// backward.ts (each segment its own fresh mpeg4/aac re-encode), except
// segment B carries a global `-output_ts_offset 5.0` instead of dts_
// backward.ts's own `-itsoffset` on its inputs -- a genuine FORWARD
// presentation-time jump (verified via `ffprobe -show_packets`: video PTS
// jumps from 3.383222s to 6.400000s at the splice, ~3.02s, comfortably past
// the 250ms threshold) rather than a backward DTS violation. (The offset
// was 3.0 -- a ~1.02s jump -- until debug session test-898-ci-
// nonreproducible: that left this pair's video size.stream_bitrate delta
// straddling the 3% warn line across hosts; see the size.* entries
// below and scripts/gen_corpus.sh's own recipe comment.) One root
// cause (D-02, the same "independently re-encoded, spliced segments" cause
// Test 1 already documents at length), several legitimately moved facts:
TEST_CASE(
    "timeline_structure - the unflagged jump trigger pair declares its complete expected finding set under "
    "--profile remux, and count_non_pass equals that set's size exactly",
    "[integration]") {
  const nlohmann::ordered_json report =
      compare_json(fixture("timeline_ts_nowrap.ts"), fixture("timeline_ts_jump.ts"), "remux");

  expect_declared_set(
      report,
      {
          // Each segment's own TS mux restarts its continuity_counter
          // sequence at 0 independently -- a genuine, UNEXPLAINED CC break
          // at the splice on every PID this two-segment technique
          // produces (verified via evidence: per_pid_errors {17:1, 256:1,
          // 257:1}, 3 total). Neither segment's own muxer ever sets
          // discontinuity_indicator (this fixture is the deliberately
          // UNFLAGGED half of the pair Test 9 below proves the split on),
          // so every one of these three breaks is unexplained.
          "container.ts.cc_errors",
          // The splice-corrupted PTS cadence no longer conforms to a
          // clean 25fps CFR sequence past the splice point -- the same
          // class of effect Test 1's dts_backward pair already declares
          // for its own splice.
          "video.frame_rate.measured",
          // The candidate's demuxer-declared duration reflects the
          // SPLICED, discontinuous timestamp sequence on both streams --
          // the same effect Test 1 documents at length for its own
          // two-segment splice.
          "timeline.duration",
          "timeline.duration",
          // The audio stream's own duration triple disagrees with itself
          // under the same splice-corrupted sequence -- one more
          // legitimate effect of the same root cause.
          "timeline.duration.coherence",
          // 05-06-PLAN.md's own timeline.gaps check: the splice opens a
          // genuine hole in BOTH streams' own presentation timelines this
          // time (unlike Test 1's dts_backward pair, where only audio
          // crossed the gap threshold) -- the jump is large enough
          // (~3.02s) to cross timeline.gaps' own declared-duration-based
          // threshold on video too, not just timeline.discontinuities'
          // fixed 250ms detection threshold below.
          "timeline.gaps",
          "timeline.gaps",
          // The check this task registers -- the one >250ms unexplained
          // presentation jump this fixture exists to prove, on both
          // streams (verified via evidence: jump_count 0 -> 1 on each).
          "timeline.discontinuities",
          "timeline.discontinuities",
          // 05-08-PLAN.md's own timeline.vfr_profile check: the ~3.02s
          // splice jump pushes BOTH streams' own worst bin (longer) past
          // the 2% dist tolerance (verified via `mediadiff compare
          // --json`). timeline.jitter itself SKIPS on both streams
          // (skipped:vfr, not a finding) since derive_cadence's own D-05
          // grid-conformance test reclassifies the splice-corrupted
          // whole-file cadence as VFR on both -- the same reclassification
          // video.frame_rate.measured's own evidence already declares
          // above for video. One more legitimate effect of the same root
          // cause (D-02).
          "timeline.vfr_profile",
          "timeline.vfr_profile",
          // A fresh two-segment mpeg4/aac re-encode is genuinely a
          // different byte size, stream bitrate and overhead ratio than
          // the original single-segment TS encode -- expected for any
          // independent re-encode, the same effect Test 1 documents.
          // size.stream_bitrate fires on BOTH streams, and on every host:
          // the ~3.02s gap lengthens the candidate's measured span, so the
          // video delta is -26.6% .. -27.1% and the audio delta ~-42%
          // across encoder thread counts 1/3/5/9 with SIMD on or off
          // (debug session test-898-ci-nonreproducible) -- far past the
          // 10% fail line. With the former ~1.02s gap the video delta sat
          // at +2.4% .. +3.2%, straddling the 3% warn line, so this entry
          // held on some hosts and not others.
          "size.file",
          "size.stream_bitrate",
          "size.stream_bitrate",
          "size.overhead",
          // 05-10-PLAN.md Task 2's own K=32 checkpoint fit: the ~3.02s
          // splice jump this fixture exists to prove is exactly the
          // "single large step" doc 04 section 3 fits a rate against --
          // the accumulated end delta CHANGE (evidence: end_delta_ms
          // 10 -> 36) clears D-07's 2ms epsilon by a wide margin, so
          // (unlike Test 1's dts_backward pair, where the effect stays
          // sub-epsilon on the RATE) the dual gate does NOT suppress
          // this one: `timeline.av_drift` genuinely fails. Both sides
          // classify as `linear-drift` (verified via evidence), so
          // `timeline.av_drift.pattern` itself stays `pass` and is
          // deliberately absent here. One more legitimate effect of the
          // same splice root cause (D-02).
          "timeline.av_drift",
      });
}

// --- Test 9: timeline_ts_jump.ts vs timeline_ts_jump_flagged.ts ------------
//
// timeline_ts_jump_flagged.ts is BYTE-IDENTICAL to timeline_ts_jump.ts
// except for the one transport packet the splice lands on --
// tools/gen_ts_discontinuity.py sets that packet's own discontinuity_
// indicator bit (verified via `cmp -l`: exactly one byte differs, the
// packet's own pre-existing adaptation-field flags byte, 0x50 -> 0xd0).
// This is the pair that proves the attribution SPLIT itself: the SAME
// underlying jump moves from `timeline.discontinuities` (unexplained,
// gating) to `timeline.discontinuities.flagged` (explained by container
// structure, info) on exactly the one stream (video) whose PID (0x100)
// the flagged packet belongs to -- the audio stream's own jump (a
// SEPARATE, unattributed jump on PID 0x101) is untouched by this edit and
// stays out of both checks' non-pass sets here (still present in both
// files identically, so it contributes nothing to this DIFF).
TEST_CASE(
    "timeline_structure - the flagged/unflagged split pair declares its complete expected finding set under "
    "--profile remux, and count_non_pass equals that set's size exactly",
    "[integration]") {
  const nlohmann::ordered_json report =
      compare_json(fixture("timeline_ts_jump.ts"), fixture("timeline_ts_jump_flagged.ts"), "remux");

  expect_declared_set(
      report,
      {
          // Flagging the video PID's own packet EXPLAINS one of the three
          // CC breaks Test 8 above declares (verified via evidence:
          // per_pid_errors baseline {17:1, 256:1, 257:1} -> candidate
          // {17:1, 257:1} -- PID 256's own entry disappears), leaving 2,
          // still a genuine mismatch against the baseline's 3.
          "container.ts.cc_errors",
          // The one-byte edit is ALSO a genuine, newly-EXPLAINED CC
          // discontinuity from container.ts.cc_discontinuities' own point
          // of view (verified via evidence: candidate count 0 -> 1) -- the
          // complementary half of the same single root cause as the line
          // above (D-02: one edit, two check ids, opposite direction).
          "container.ts.cc_discontinuities",
          // A pre-existing, unrelated-to-this-edit TS audio-duration-
          // bookkeeping artifact (verified via evidence: computed_ms 5020,
          // container_declared_ms 5000, stream_declared_ms 4904,
          // IDENTICAL on both baseline and candidate) -- the same class of
          // per-encode artifact Test 5 above already declares for its own
          // byte-identical clean pair, riding along here because both
          // fixtures share the same underlying AAC audio encode.
          "timeline.duration.coherence",
          // THE split, half one: the jump this fixture's own baseline
          // carries as unexplained now has a container-structure
          // explanation in the candidate, so it VANISHES from
          // timeline.discontinuities (verified via evidence: jump_count
          // 1 -> 0, message "-1 removed span(s)") -- a removed span is
          // always `info` under this check's own span semantic (never
          // gating), even though the check's own severity is `fail`.
          "timeline.discontinuities",
          // THE split, half two: the SAME jump now appears in
          // timeline.discontinuities.flagged instead (verified via
          // evidence: jump_count 0 -> 1, message "+1 introduced span(s)",
          // identical start/end ms to the removed span above) -- this
          // check's own severity is `info` by design, so an introduced
          // span here never gates the merge on its own either. This pair
          // is the whole reason this check id exists.
          "timeline.discontinuities.flagged",
          // timeline.av_drift is deliberately ABSENT: the one-byte edit
          // does not touch either stream's packet timing, so both sides'
          // least-squares rates are IDENTICAL and the delta is exactly
          // zero -> `pass`. (Until debug session test-898-ci-
          // nonreproducible this set listed timeline.av_drift, because
          // src/compare/tol.cpp cross-multiplied those identical wide
          // rationals in int64_t, overflowed, and reported `error` -- a
          // declared set must never enshrine an `error`. The comparator is
          // exact now, core/exact_int.h.)
      });
}
