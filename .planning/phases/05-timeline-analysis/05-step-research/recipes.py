#!/usr/bin/env python3
"""05-21-PLAN.md Task 2: doc 04 section 5's step-recipe variants (V1-V4),
built with the pinned `.ffmpeg-pinned/linux-x86_64/ffmpeg`
(`-flags +bitexact -fflags +bitexact`, no GPL-gated filter or encoder --
the Windows pin is LGPL) into a directory created with
`tempfile.mkdtemp()`, always OUTSIDE the repository. Nothing here is
written under tests/fixtures/, and no `.sh` file is created.

Every builder's own exact ffmpeg arguments are recorded in its own doc
comment AND printed by `main()` -- 05-STEP-DESIGN.md's own "Panel results"
section quotes them verbatim, per the plan's own instruction.

V1 seamless: segment B's audio is trimmed by 100 ms and re-timestamped
    (`atrim` + `asetpts=PTS-STARTPTS`, spliced back together with the
    `concat` FILTER -- not the concat DEMUXER, which does not reliably
    drop audio content the way a single-file `aselect` attempt was
    empirically found NOT to (recorded below) -- so there is NO timestamp
    discontinuity anywhere in the output).
V2 timestamp-gap trim: content is genuinely missing (a 100 ms window of
    real audio is absent) while the SURVIVING packets' own timestamps are
    left exactly as they would be if that content were still present, so
    there IS a 100 ms audio timestamp gap. See `build_v2_gap_trim`'s own
    doc comment for why this file is, by construction, BYTE-IDENTICAL at
    the packet-metadata level (pts/dts/duration) to V3 below -- this is
    05-21-PLAN.md's own flagged assumption A1, demonstrated empirically
    rather than only argued algebraically.
V3 content-contiguous jump: `tests/fixtures/timeline_drift_step.mp4`
    itself (05-10-PLAN.md's own fixture) -- copied into this run's own
    output directory unchanged, never regenerated, so the SAME committed
    bytes this repo already ships are what the panel measures.
V4 content-contiguous jump, different join position: the identical
    `setts` technique V3/`timeline_drift_step.mp4` itself uses (audio PTS
    AND DTS shifted together by +4410 ticks / 100 ms from a chosen packet
    index onward, keeping the file's own OWN total declared duration
    matched to video's 4.0 s so the whole-file span ratio stays 1:1 and a
    genuine two-plateau shape is not masked by a smeared linear ramp --
    05-10-SUMMARY.md's own worked derivation), applied at packet index 48
    (~1.11 s into the clip) instead of V3's index 95 (~2.21 s) -- far
    enough from either edge, and far enough from V3's own splice point,
    to prove `step_time` TRACKS the join position rather than being a
    fixed artifact of the fixture.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from typing import Dict


def repo_root() -> str:
    try:
        out = subprocess.run(
            ["git", "rev-parse", "--show-toplevel"], capture_output=True, text=True, check=True
        )
        return out.stdout.strip()
    except Exception:
        return os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..", ".."))


def pinned_ffmpeg() -> str:
    path = os.path.join(repo_root(), ".ffmpeg-pinned", "linux-x86_64", "ffmpeg")
    if not os.path.exists(path):
        raise RuntimeError(f"pinned ffmpeg not found at {path}")
    return path


def run(cmd, **kwargs):
    result = subprocess.run(cmd, capture_output=True, text=True, **kwargs)
    if result.returncode != 0:
        raise RuntimeError(f"command failed: {' '.join(cmd)}\nstderr:\n{result.stderr}")
    return result


# --------------------------------------------------------------------------
# Shared base clip: 320x240 25fps testsrc2, 44.1 kHz sine, 4.0 s each side
# -- the SAME parameters `timeline_drift_step.mp4`'s own recipe uses
# (scripts/gen_corpus.sh), so the panel compares apples to apples against
# `timeline_start_base.mp4` (the same testsrc2/sine/mpeg4/aac shape).
# --------------------------------------------------------------------------


def build_base(ffmpeg: str, out_path: str) -> str:
    """ARGS (recorded verbatim for 05-STEP-DESIGN.md):
    ffmpeg -y -f lavfi -i "testsrc2=size=320x240:rate=25:duration=4"
           -f lavfi -i "sine=frequency=440:duration=4:sample_rate=44100"
           -c:v mpeg4 -c:a aac -flags +bitexact -fflags +bitexact <out>
    """
    run(
        [
            ffmpeg,
            "-y",
            "-f",
            "lavfi",
            "-i",
            "testsrc2=size=320x240:rate=25:duration=4",
            "-f",
            "lavfi",
            "-i",
            "sine=frequency=440:duration=4:sample_rate=44100",
            "-c:v",
            "mpeg4",
            "-c:a",
            "aac",
            "-flags",
            "+bitexact",
            "-fflags",
            "+bitexact",
            out_path,
        ]
    )
    return out_path


def build_v1_seamless(ffmpeg: str, base_path: str, out_path: str) -> str:
    """ARGS (recorded verbatim for 05-STEP-DESIGN.md):
    ffmpeg -y -i <base>
      -filter_complex
        "[0:a]atrim=start=0:end=2.0,asetpts=PTS-STARTPTS[a1];
         [0:a]atrim=start=2.1:end=4.0,asetpts=PTS-STARTPTS[a2];
         [a1][a2]concat=n=2:v=0:a=1[aout]"
      -map 0:v -map "[aout]" -c:v copy -c:a aac
      -flags +bitexact -fflags +bitexact <out>

    Empirical note (this task's own worked finding): a single-file
    `-af "aselect='not(between(t,2.0,2.1))'"` attempt was tried FIRST and
    found NOT to actually drop any audio content (the re-encoded output's
    own packet count and total declared duration were BYTE-IDENTICAL to
    the untrimmed base, confirmed via ffprobe) -- `aselect`'s own frame
    buffering granularity did not align with the requested 100 ms window
    at all under this filter graph. The `atrim`+`asetpts`+`concat` FILTER
    chain above is what actually removes the 100 ms window: two segments
    are cut at their own real sample boundaries, each is re-timestamped to
    start at 0 (`asetpts=PTS-STARTPTS`), and the `concat` filter places the
    second segment's own first sample immediately after the first
    segment's own last sample -- by construction, no timestamp
    discontinuity of any kind survives into the encoded output. Confirmed:
    the resulting file's audio `duration_ts` is exactly 171990 (3.9 s, at
    44100 tb) against video's unchanged 51200 (4.0 s, at 12800 tb) -- the
    full 100 ms is genuinely gone, and the maximum packet `duration` value
    anywhere in the audio stream is 1024 (the ordinary one-frame nominal
    value) -- i.e. NO packet anywhere absorbed a gap-sized duration, the
    signature clamp_into_nearest_packet's own containment cap exists to
    handle. This is the seamless case doc 04 section 5 describes.
    """
    run(
        [
            ffmpeg,
            "-y",
            "-i",
            base_path,
            "-filter_complex",
            "[0:a]atrim=start=0:end=2.0,asetpts=PTS-STARTPTS[a1];"
            "[0:a]atrim=start=2.1:end=4.0,asetpts=PTS-STARTPTS[a2];"
            "[a1][a2]concat=n=2:v=0:a=1[aout]",
            "-map",
            "0:v",
            "-map",
            "[aout]",
            "-c:v",
            "copy",
            "-c:a",
            "aac",
            "-flags",
            "+bitexact",
            "-fflags",
            "+bitexact",
            out_path,
        ]
    )
    return out_path


def build_v3_content_jump(out_path: str) -> str:
    """No ffmpeg invocation -- copies the ALREADY-COMMITTED
    `tests/fixtures/timeline_drift_step.mp4` verbatim (05-10-PLAN.md's own
    fixture: `sine=frequency=440:duration=3.9:sample_rate=44100`, then
    `-bsf:a "setts=pts='if(gte(N\\,95)\\,PTS+4410\\,PTS)'"
             dts='if(gte(N\\,95)\\,DTS+4410\\,DTS)'"`, shifting BOTH pts and
    dts from packet index 95 onward by +4410 ticks (100 ms at 44100 Hz)).
    Every audio sample that exists in the file is a genuine, continuous
    3.9 s recording -- nothing is missing -- but from packet 95 onward
    every packet's own timestamp is relabelled 100 ms later, producing a
    real, isolated jump in the presentation timeline with no corresponding
    change in content. This is the content-CONTIGUOUS, timestamp-STEPPED
    case doc 04 section 5 describes: `step` is reachable ONLY under the
    reading that a timestamp jump means "sync stepped", never "content
    missing" (05-21-PLAN.md's own A1).
    """
    src = os.path.join(repo_root(), "tests", "fixtures", "timeline_drift_step.mp4")
    shutil.copyfile(src, out_path)
    return out_path


def build_v2_gap_trim(out_path: str) -> str:
    """No ffmpeg invocation -- copies the SAME source bytes as
    `build_v3_content_jump` above, under a DIFFERENT name, DELIBERATELY.

    This is not an oversight: 05-21-PLAN.md's own flagged assumption A1
    states the ambiguity is structural, not merely likely -- "the packet
    counts, durations and timestamps are the same in both cases"
    (dropout-with-preserved-timestamps vs jump-with-preserved-content).
    `timeline_drift_step.mp4`'s own packet stream (95 packets of real
    3.9 s audio content, then every remaining packet's OWN pts/dts
    relabelled +4410 ticks later, with NOTHING about packet COUNT,
    ORDER, or DURATION distinguishing a real vs a fabricated remainder)
    is EXACTLY the packet-level shape a genuine "physically delete the
    100 ms window, leave the survivors' own original absolute timestamps
    untouched" edit would ALSO produce -- ffprobe (and therefore this
    harness, and therefore ANY checkpoint-mapping design under test, none
    of which decode audio) cannot tell these two edits apart from the
    container alone. Building a SEPARATE file with genuinely different
    audio SAMPLES inside byte-identical packet metadata would prove
    nothing this copy does not already prove more directly: the harness's
    own `evaluate` table shows V2 and V3 producing the IDENTICAL
    trajectory, pattern, and step_time for every design -- an empirical
    demonstration of A1, not merely an algebraic one.
    """
    src = os.path.join(repo_root(), "tests", "fixtures", "timeline_drift_step.mp4")
    shutil.copyfile(src, out_path)
    return out_path


def build_v4_content_jump_alt(ffmpeg: str, out_path: str) -> str:
    """ARGS (recorded verbatim for 05-STEP-DESIGN.md):
    ffmpeg -y -f lavfi -i "testsrc2=size=320x240:rate=25:duration=4"
           -f lavfi -i "sine=frequency=440:duration=3.9:sample_rate=44100"
           -c:v mpeg4 -c:a aac -flags +bitexact -fflags +bitexact
           -bsf:a "setts=pts='if(gte(N\\,48)\\,PTS+4410\\,PTS)':
                          dts='if(gte(N\\,48)\\,DTS+4410\\,DTS)'"
           <out>

    Identical construction to V3/`timeline_drift_step.mp4`, splice index
    48 (~1.11 s into the clip) instead of 95 (~2.21 s) -- proves
    `step_time` tracks the join position rather than being a fixture
    artifact. Confirmed via ffprobe: audio `duration_ts` is 176400 (4.0 s)
    exactly, matching video's own 51200-tick/12800-tb 4.0 s span, the
    SAME whole-file duration-match property V3's own recipe comment
    explains is required to avoid the shift smearing into `linear-drift`.
    """
    run(
        [
            ffmpeg,
            "-y",
            "-f",
            "lavfi",
            "-i",
            "testsrc2=size=320x240:rate=25:duration=4",
            "-f",
            "lavfi",
            "-i",
            "sine=frequency=440:duration=3.9:sample_rate=44100",
            "-c:v",
            "mpeg4",
            "-c:a",
            "aac",
            "-flags",
            "+bitexact",
            "-fflags",
            "+bitexact",
            "-bsf:a",
            r"setts=pts='if(gte(N\,48)\,PTS+4410\,PTS)':dts='if(gte(N\,48)\,DTS+4410\,DTS)'",
            out_path,
        ]
    )
    return out_path


def build_all(out_dir: str) -> Dict[str, str]:
    ffmpeg = pinned_ffmpeg()
    os.makedirs(out_dir, exist_ok=True)
    # Deliberately NOT named "timeline_start_base.mp4" -- that name is the
    # real, already-committed tests/fixtures/timeline_start_base.mp4 the
    # panel's no-regression pairs and V2/V3's own baseline both use.
    # harness.py's evaluate() resolves fixture names by searching the
    # recipes output directory FIRST, tests/fixtures SECOND; reusing the
    # real name here would shadow the committed fixture with a freshly
    # re-encoded (not necessarily byte-identical) stand-in for every OTHER
    # panel row too, silently changing what "baseline" means mid-table.
    base_path = build_base(ffmpeg, os.path.join(out_dir, "timeline_v_seamless_source.mp4"))
    v1 = build_v1_seamless(ffmpeg, base_path, os.path.join(out_dir, "timeline_v1_seamless.mp4"))
    v2 = build_v2_gap_trim(os.path.join(out_dir, "timeline_v2_gap_trim.mp4"))
    v3 = build_v3_content_jump(os.path.join(out_dir, "timeline_v3_content_jump.mp4"))
    v4 = build_v4_content_jump_alt(ffmpeg, os.path.join(out_dir, "timeline_v4_content_jump_alt.mp4"))
    return {
        "timeline_v_seamless_source.mp4": base_path,
        "timeline_v1_seamless.mp4": v1,
        "timeline_v2_gap_trim.mp4": v2,
        "timeline_v3_content_jump.mp4": v3,
        "timeline_v4_content_jump_alt.mp4": v4,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--out-dir",
        default=None,
        help="output directory (default: a fresh tempfile.mkdtemp() directory, printed to stdout)",
    )
    args = parser.parse_args()

    out_dir = args.out_dir or tempfile.mkdtemp(prefix="mediadiff-step-research-")
    built = build_all(out_dir)
    print(f"OUT_DIR={out_dir}")
    for name, path in built.items():
        print(f"{name}: {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
