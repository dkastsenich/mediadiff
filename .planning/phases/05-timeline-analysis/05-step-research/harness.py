#!/usr/bin/env python3
"""05-21-PLAN.md Task 1/2: a scratch research harness that re-implements the
shipped `src/analyzers/timeline/av_sync.cpp` checkpoint construction and
`fit_drift` classification exactly, in Python, using `fractions.Fraction`
for every exact-rational step and plain Python (arbitrary-precision) `int`
everywhere else. NO product file is imported or modified -- this is a
from-scratch re-implementation, calibrated against the real binary's own
`mediadiff snapshot` evidence (see `calibrate()` below), never assumed
correct by construction.

This file introduces NO floating point anywhere in a mapping or fit (a
literal-match grep for the Python float-constructor call, applied to this
file, reports zero occurrences -- per the plan's own acceptance criterion).
`ticks_to_ms`/`priming_samples_to_ticks`/`fit_drift`'s own internal
divisions all truncate toward zero via `trunc_div`, mirroring
`core/rational.h`'s `detail::checked_div` (plain C++ `/`, which truncates
toward zero, never floors) -- Python's own `//` floors, so every division
in this file that must match the shipped truncation convention goes
through `trunc_div`, never a bare `//`.

Subcommands:
  calibrate  -- re-derive timeline.av_drift / timeline.av_drift.pattern for
                the 8 calibration fixtures and assert exact equality
                against `mediadiff snapshot`'s own real evidence.
  evaluate   -- run every candidate design (D0 shipped, D1 segment-
                proportional, D2 media-clock, each under span:declared and
                span:observed) over the fixed panel and print a table.

Nothing under tests/fixtures/ or src/ is read for anything other than
input; nothing is written there. Recipe-built media referenced by
`evaluate()` are read from a caller-supplied directory (05-21 Task 2's
recipes.py writes there, always a `tempfile.mkdtemp()` result).
"""

from __future__ import annotations

import argparse
import bisect
import json
import os
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field
from fractions import Fraction
from typing import Dict, List, Optional, Tuple

# --------------------------------------------------------------------------
# doc 04 section 3 / analyzers.h's own named constants, transcribed
# verbatim -- never a bare literal at a use site in this file either.
# --------------------------------------------------------------------------

INT64_MIN = -(1 << 63)

K_DRIFT_CHECKPOINT_COUNT = 32  # kDriftCheckpointCount
K_DRIFT_EPSILON_MS = 2  # kDriftEpsilonMs
K_DRIFT_STEP_RESIDUAL_MULTIPLE = 3  # kDriftStepResidualMultiple
K_DRIFT_RATE_EPSILON_NUM_MS_PER_MIN = 1  # kDriftRateEpsilonNumMsPerMin
K_DRIFT_RATE_EPSILON_DEN_MS_PER_MIN = 5  # kDriftRateEpsilonDenMsPerMin
K_MAX_DRIFT_DENOMINATOR = 1_000_000_000  # kMaxDriftDenominator
K_NOMINAL_DURATION_CAP_MULTIPLIER = 2  # kNominalDurationCapMultiplier

K_TS_PTS_WRAP_MODULUS = 1 << 33  # kTsPtsWrapModulus
K_TS_PTS_WRAP_HALF_RANGE = 1 << 32  # kTsPtsWrapHalfRange

CALIBRATION_FIXTURES = [
    "timeline_start_base.mp4",
    "timeline_start_base_copy.mp4",
    "timeline_avoffset_video_shift.mp4",
    "timeline_drift_base.mp4",
    "timeline_drift_linear.mp4",
    "timeline_drift_step.mp4",
    "timeline_start_shift.ts",
    "timeline_ntsc_remux.mkv",
]

# doc 04 section 5 / 05-21-PLAN.md Task 2's fixed panel. See recipes.py for
# the V1-V4 builders; these names match the directory recipes.py writes.
PANEL_NO_REGRESSION_PAIRS = [
    ("timeline_start_base.mp4", "timeline_start_base_copy.mp4"),
    ("timeline_start_base.mp4", "timeline_avoffset_video_shift.mp4"),
    ("timeline_drift_base.mp4", "timeline_drift_linear.mp4"),
    ("timeline_start_base.mp4", "timeline_drift_step.mp4"),
]

PANEL_FALSE_POSITIVE_GUARDS = [
    ("timeline_ts_nowrap.ts", "timeline_ts_jump.ts"),
    ("timeline_ts_nowrap.ts", "timeline_ts_jump_flagged.ts"),
    ("timeline_start_base.mp4", "timeline_start_shift.ts"),
    ("timeline_start_base.mp4", "timeline_avoffset_unknown.ts"),
    ("timeline_ntsc_base.mp4", "timeline_ntsc_remux.mkv"),
]


def trunc_div(a: int, b: int) -> int:
    """`a / b` truncated toward zero -- Python's own `//` floors, which
    disagrees with C++'s `/` (and this project's own `detail::checked_div`)
    for exactly the negative-operand case. Every division in this file that
    must match the shipped truncation convention goes through this
    function, never a bare `//` or `int(Fraction(...))` (whose rounding
    direction is easy to get backwards for a negative operand)."""
    q = a // b
    if (a % b != 0) and ((a < 0) != (b < 0)):
        q += 1
    return q


# --------------------------------------------------------------------------
# ffprobe-backed packet/stream loading. Python 3 standard library only
# (subprocess + json) -- no third-party package, matching the plan's own
# constraint.
# --------------------------------------------------------------------------


def repo_root() -> str:
    try:
        out = subprocess.run(
            ["git", "rev-parse", "--show-toplevel"], capture_output=True, text=True, check=True
        )
        return out.stdout.strip()
    except Exception:
        return os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..", ".."))


def mediadiff_bin() -> str:
    return os.path.join(repo_root(), "build", "x64-linux", "mediadiff")


def ffprobe_json(extra_args: List[str], path: str) -> dict:
    cmd = ["ffprobe", "-v", "error", "-of", "json"] + extra_args + [path]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"ffprobe failed for {path}: {result.stderr}")
    return json.loads(result.stdout) if result.stdout.strip() else {}


@dataclass
class FileData:
    streams: Dict[int, dict]
    packets: Dict[int, List[dict]]  # idx -> list of {"pts","dts","duration"} in read order
    first_packet_skip_samples: Dict[int, Optional[int]]
    is_ts: bool


def _parse_int_field(value) -> int:
    if value is None or value == "N/A" or value == "unknown":
        return INT64_MIN
    return int(value)


def first_packet_skip_samples(path: str, stream_index: int) -> Optional[int]:
    """The stream's own FIRST packet in READ order, inspected for
    AV_PKT_DATA_SKIP_SAMPLES -- mirrors packet_scan.cpp's own "first
    accepted packet, and only that packet" rule exactly (D-09)."""
    data = ffprobe_json(
        ["-select_streams", str(stream_index), "-read_intervals", "%+#1", "-show_packets"], path
    )
    packets = data.get("packets", [])
    if not packets:
        return None
    for side in packets[0].get("side_data_list", []):
        if side.get("side_data_type") == "Skip Samples":
            return int(side.get("skip_samples", 0))
    return None


def load_file(path: str) -> FileData:
    fmt = ffprobe_json(["-show_entries", "format=format_name"], path)
    format_name = fmt.get("format", {}).get("format_name", "")
    is_ts = format_name.split(",")[0] == "mpegts"

    streams_json = ffprobe_json(
        ["-show_entries", "stream=index,codec_type,time_base,duration_ts,sample_rate"], path
    ).get("streams", [])
    streams: Dict[int, dict] = {}
    for s in streams_json:
        idx = int(s["index"])
        tb_str = s.get("time_base", "1/1")
        tb_num_str, tb_den_str = tb_str.split("/")
        streams[idx] = {
            "codec_type": s.get("codec_type"),
            "tb": (int(tb_num_str), int(tb_den_str)),
            "declared_duration_ticks": int(s["duration_ts"]) if "duration_ts" in s else None,
            "sample_rate": int(s["sample_rate"]) if "sample_rate" in s else None,
        }

    packets: Dict[int, List[dict]] = {idx: [] for idx in streams}
    pkt_json = ffprobe_json(
        ["-show_entries", "packet=stream_index,pts,dts,duration"], path
    ).get("packets", [])
    for p in pkt_json:
        idx = int(p["stream_index"])
        packets.setdefault(idx, []).append(
            {
                "pts": _parse_int_field(p.get("pts")),
                "dts": _parse_int_field(p.get("dts")),
                "duration": int(p.get("duration", 0) or 0),
            }
        )

    skip_samples: Dict[int, Optional[int]] = {}
    for idx, info in streams.items():
        if info["codec_type"] != "audio":
            continue
        skip_samples[idx] = first_packet_skip_samples(path, idx)

    return FileData(streams=streams, packets=packets, first_packet_skip_samples=skip_samples, is_ts=is_ts)


# --------------------------------------------------------------------------
# doc 04 section 1.2: the 33-bit MPEG-TS unwrap, transcribed from
# unwrap.cpp's apply_wrap_step/unwrap_ts_timestamps exactly (the asymmetric
# rule: only a backward jump past -half-range advances the running offset;
# a forward jump past +half-range is a genuine discontinuity, left alone).
# --------------------------------------------------------------------------


def unwrap_ts_timestamps(raw_in_read_order: List[int]) -> Tuple[List[int], int]:
    unwrapped: List[int] = []
    offset = 0
    prev: Optional[int] = None
    wrap_events = 0
    for raw in raw_in_read_order:
        if prev is not None:
            delta = raw - prev
            if delta < -K_TS_PTS_WRAP_HALF_RANGE:
                offset += K_TS_PTS_WRAP_MODULUS
                wrap_events += 1
            # Forward guard (delta > +half-range): deliberately NOT adjusted
            # -- doc 04's own asymmetry, a genuine backward discontinuity
            # must never be erased as if it were a wrap.
        unwrapped.append(raw + offset)
        prev = raw
    return unwrapped, wrap_events


def make_timeline_packet_views(
    packets_by_stream: Dict[int, List[dict]], is_ts: bool
) -> Dict[int, List[dict]]:
    """unwrap.cpp's make_timeline_packet_views: per-stream PTS/DTS unwrap
    (TS only), then the cross-stream 2^33 epoch alignment (TS only). A
    non-TS input's packets pass through completely unchanged (zero-copy in
    the shipped code; a shallow copy here, since Python has no borrow
    concept -- the caller never mutates the source dict entries)."""
    if not is_ts:
        return {idx: [dict(p) for p in pkts] for idx, pkts in packets_by_stream.items()}

    views: Dict[int, List[dict]] = {}
    for idx, pkts in packets_by_stream.items():
        owned = [dict(p) for p in pkts]
        for axis in ("pts", "dts"):
            indices = [i for i, p in enumerate(owned) if p[axis] != INT64_MIN]
            raw = [owned[i][axis] for i in indices]
            if not raw:
                continue
            unwrapped, _wrap_events = unwrap_ts_timestamps(raw)
            for i, val in zip(indices, unwrapped):
                owned[i][axis] = val
        views[idx] = owned

    # Cross-stream epoch rule: each stream's own FIRST RAW (pre-unwrap) PTS
    # in read order.
    first_raw_pts: Dict[int, int] = {}
    for idx, pkts in packets_by_stream.items():
        for p in pkts:
            if p["pts"] != INT64_MIN:
                first_raw_pts[idx] = p["pts"]
                break
    if not first_raw_pts:
        return views

    min_pts = min(first_raw_pts.values())
    max_pts = max(first_raw_pts.values())
    spread = max_pts - min_pts
    if spread <= K_TS_PTS_WRAP_HALF_RANGE:
        return views

    for idx in views:
        raw0 = first_raw_pts.get(idx)
        if raw0 is None or raw0 >= K_TS_PTS_WRAP_HALF_RANGE:
            continue
        for p in views[idx]:
            if p["pts"] != INT64_MIN:
                p["pts"] += K_TS_PTS_WRAP_MODULUS
            if p["dts"] != INT64_MIN:
                p["dts"] += K_TS_PTS_WRAP_MODULUS
    return views


# --------------------------------------------------------------------------
# Shared pure helpers -- av_sync.cpp's own detail:: functions, transcribed.
# --------------------------------------------------------------------------


def ticks_to_ms(ticks: int, tb: Tuple[int, int]) -> int:
    tb_num, tb_den = tb
    return trunc_div(ticks * 1000 * tb_num, tb_den)


def first_presented_pts(packets: List[dict]) -> Optional[int]:
    valid = [p["pts"] for p in packets if p["pts"] != INT64_MIN]
    if not valid:
        return None
    return min(valid)


@dataclass
class PtsSpan:
    pts: List[int] = field(default_factory=list)
    durations: List[int] = field(default_factory=list)
    span_ticks: int = 0
    has_span: bool = False
    nominal_duration_ticks: int = 0


def sorted_pts_with_span(packets: List[dict]) -> PtsSpan:
    entries = [(p["pts"], p["duration"]) for p in packets if p["pts"] != INT64_MIN]
    entries.sort(key=lambda e: e[0])

    result = PtsSpan()
    for i, (pts, dur) in enumerate(entries):
        eff = dur
        if eff <= 0:
            neighbor: Optional[int] = None
            if i + 1 < len(entries):
                neighbor = i + 1
            elif i > 0:
                neighbor = i - 1
            if neighbor is not None:
                interval = (
                    entries[neighbor][0] - entries[i][0]
                    if neighbor > i
                    else entries[i][0] - entries[neighbor][0]
                )
                eff = interval if interval > 0 else 0
            else:
                eff = 0
        result.pts.append(pts)
        result.durations.append(eff)

    positive = sorted(d for d in result.durations if d > 0)
    if positive:
        result.nominal_duration_ticks = positive[len(positive) // 2]

    if len(entries) < 2:
        return result

    last_pts = entries[-1][0]
    last_eff = result.durations[-1]
    last_end = last_pts + last_eff
    span = last_end - entries[0][0]
    if span > 0:
        result.span_ticks = span
        result.has_span = True
    return result


def nearest_tick(sorted_pts: List[int], target: int) -> int:
    it = bisect.bisect_left(sorted_pts, target)
    if it == 0:
        return sorted_pts[0]
    if it == len(sorted_pts):
        return sorted_pts[-1]
    upper = sorted_pts[it]
    lower = sorted_pts[it - 1]
    upper_delta = upper - target
    lower_delta = target - lower
    return upper if upper_delta < lower_delta else lower


def clamp_into_nearest_packet(
    sorted_pts: List[int], durations: List[int], nominal_duration_ticks: int, target: int
) -> int:
    upper_index = bisect.bisect_left(sorted_pts, target)
    has_lower = False
    lower_index = 0
    if upper_index < len(sorted_pts) and sorted_pts[upper_index] == target:
        has_lower = True
        lower_index = upper_index
    elif upper_index > 0:
        has_lower = True
        lower_index = upper_index - 1

    if has_lower:
        effective_duration = durations[lower_index]
        if nominal_duration_ticks > 0:
            cap = nominal_duration_ticks * K_NOMINAL_DURATION_CAP_MULTIPLIER
            if cap > 0 and cap < effective_duration:
                effective_duration = cap
        end_tick = sorted_pts[lower_index] + effective_duration
        if target < end_tick:
            return target  # Contained -- sample-accurate, zero clamp.

    has_upper = upper_index < len(sorted_pts)
    lower_end = sorted_pts[lower_index] + durations[lower_index] if has_lower else 0
    upper_start = sorted_pts[upper_index] if has_upper else 0
    if not has_lower:
        return upper_start
    if not has_upper:
        return lower_end
    lower_delta = target - lower_end
    upper_delta = upper_start - target
    return upper_start if upper_delta < lower_delta else lower_end


def index_proportional_raw_ticks(
    sorted_pts: List[int], delta_from_video_start_ticks: int, video_span_ticks: int
) -> int:
    last_index = len(sorted_pts) - 1
    numerator = delta_from_video_start_ticks * last_index
    rounded_numerator = numerator + video_span_ticks // 2
    index = trunc_div(rounded_numerator, video_span_ticks)
    if index < 0:
        index = 0
    elif index > last_index:
        index = last_index
    return sorted_pts[index]


def priming_samples_to_ticks(samples: int, sample_rate: Optional[int], tb: Tuple[int, int]) -> Optional[int]:
    tb_num, tb_den = tb
    if samples < 0 or sample_rate is None or sample_rate <= 0 or tb_num <= 0 or tb_den <= 0:
        return None
    numerator = samples * tb_den
    denominator = sample_rate * tb_num
    half_denominator = denominator // 2
    rounded_numerator = numerator + half_denominator
    return trunc_div(rounded_numerator, denominator)


def resolve_priming(first_packet_skip: Optional[int], initial_padding: int) -> Tuple[str, int]:
    """D-09's own precedence: packet-level skip_samples first,
    codecpar->initial_padding as fallback."""
    if first_packet_skip is not None and first_packet_skip > 0:
        return "skip_samples", first_packet_skip
    if initial_padding > 0:
        return "initial_padding", initial_padding
    return "unknown", 0


# --------------------------------------------------------------------------
# fit_drift -- doc 04 section 3 steps 3-4, transcribed from av_sync.cpp's
# own fit_line/residual_ms/fit_drift exactly. Uses fractions.Fraction for
# the least-squares slope (Fraction's own GCD reduction is canonical --
# same reduced numerator/denominator pair the C++ Int128Accum::
# try_reduce_ratio produces, since both reduce the identical rational
# number to lowest terms with a positive denominator). Every subsequent
# division (residuals, means) truncates toward zero via trunc_div, exactly
# matching detail::checked_div.
# --------------------------------------------------------------------------


@dataclass
class DriftFit:
    rate_num: int
    rate_den: int
    end_delta_ms: int
    residual_max_ms: int
    pattern: str
    step_time_ms: Optional[int]


def fit_line(x: List[int], y: List[int]) -> Optional[Tuple[int, int]]:
    count = len(x)
    n_acc = 0
    d_acc = 0
    for i in range(count):
        for j in range(i + 1, count):
            dx = x[j] - x[i]
            dy = y[j] - y[i]
            d_acc += dx * dx
            n_acc += dx * dy
    if d_acc == 0:
        return None
    frac = Fraction(n_acc, d_acc)  # canonical GCD-reduced, positive denominator
    raw_num, raw_den = frac.numerator, frac.denominator
    if raw_den > K_MAX_DRIFT_DENOMINATOR:
        return None
    return raw_num, raw_den


def fit_drift(checkpoints: List[Tuple[int, int]]) -> Optional[DriftFit]:
    """`checkpoints`: list of (t_v_ms, offset_ms) -- already millisecond-
    scale integers, mirroring the shipped call site's own
    `fit_drift(checkpoints, Rational{1, 1000})` (the checkpoint values ARE
    already expressed in a 1/1000 s timebase by construction, so this
    function's own internal tb-scaling collapses to an identity -- verified
    algebraically against av_sync.cpp's residual_ms, see this file's
    05-STEP-DESIGN.md Calibration section)."""
    count = len(checkpoints)
    if count < 2:
        return None

    x0 = checkpoints[0][0]
    x = [c[0] - x0 for c in checkpoints]
    y = [c[1] for c in checkpoints]

    line = fit_line(x, y)
    if line is None:
        return None
    raw_num, raw_den = line

    rate_num = raw_num * 60000  # ms/min, NOT re-reduced (matches the shipped code exactly)
    rate_den = raw_den

    sum_x = sum(x)
    sum_y = sum(y)

    residuals: List[int] = []
    for i in range(count):
        a_k = count * y[i] - sum_y
        b_k = sum_x - count * x[i]
        numer = a_k * raw_den + raw_num * b_k
        denom = count * raw_den
        residuals.append(trunc_div(numer, denom))
    residual_max = max(abs(r) for r in residuals)

    end_delta_ms = y[-1] - y[0]

    if residual_max < K_DRIFT_EPSILON_MS:
        abs_rate_num = abs(rate_num)
        lhs = abs_rate_num * K_DRIFT_RATE_EPSILON_DEN_MS_PER_MIN
        rhs = K_DRIFT_RATE_EPSILON_NUM_MS_PER_MIN * rate_den
        pattern = "constant-offset" if lhs < rhs else "linear-drift"
        return DriftFit(rate_num, rate_den, end_delta_ms, residual_max, pattern, None)

    offset_ms = y
    step_index = 0
    largest_jump = -1
    for i in range(1, count):
        jump = abs(offset_ms[i] - offset_ms[i - 1])
        if jump > largest_jump:
            largest_jump = jump
            step_index = i

    step_threshold = K_DRIFT_STEP_RESIDUAL_MULTIPLE * K_DRIFT_EPSILON_MS
    is_step = largest_jump > step_threshold
    if is_step:
        sum_before = sum(offset_ms[:step_index])
        sum_after = sum(offset_ms[step_index:])
        mean_before = trunc_div(sum_before, step_index)
        mean_after = trunc_div(sum_after, count - step_index)
        for i in range(step_index):
            if abs(offset_ms[i] - mean_before) > K_DRIFT_EPSILON_MS:
                is_step = False
                break
        if is_step:
            for i in range(step_index, count):
                if abs(offset_ms[i] - mean_after) > K_DRIFT_EPSILON_MS:
                    is_step = False
                    break

    if is_step:
        step_time_ms = checkpoints[step_index][0]
        return DriftFit(rate_num, rate_den, end_delta_ms, residual_max, "step", step_time_ms)
    return DriftFit(rate_num, rate_den, end_delta_ms, residual_max, "irregular", None)


# --------------------------------------------------------------------------
# shipped_trajectory -- av_sync.cpp's own checkpoint CONSTRUCTION (doc 04
# section 3 steps 1-2), transcribed exactly. Returns None when the shipped
# analyzer itself would skip (no video/audio stream, no span, etc.) -- the
# caller maps that to a documented "skip" row, never a fabricated result.
# --------------------------------------------------------------------------


@dataclass
class DriftResult:
    trajectory: List[Tuple[int, int, int]]  # (k, t_v_ms, offset_ms)
    fit: DriftFit
    raw_offset_ms: int
    adjusted_offset_ms: int
    comparison_basis: str


def compute_shipped_drift(fd: FileData, video_idx: int, audio_idx: int) -> Optional[DriftResult]:
    views = make_timeline_packet_views(fd.packets, fd.is_ts)
    video_packets = views[video_idx]
    video_tb = fd.streams[video_idx]["tb"]
    video_first_pts_ticks = first_presented_pts(video_packets)
    if video_first_pts_ticks is None:
        return None
    video_first_pts_ms = ticks_to_ms(video_first_pts_ticks, video_tb)

    video_pts_span = sorted_pts_with_span(video_packets)
    video_declared = fd.streams[video_idx]["declared_duration_ticks"]
    if video_declared is not None and video_declared > 0:
        video_span_ticks = video_declared
    elif video_pts_span.has_span:
        video_span_ticks = video_pts_span.span_ticks
    else:
        return None

    audio_packets = views[audio_idx]
    audio_tb = fd.streams[audio_idx]["tb"]
    audio_first_pts_ticks = first_presented_pts(audio_packets)
    if audio_first_pts_ticks is None:
        return None

    skip_samples = fd.first_packet_skip_samples.get(audio_idx)
    priming_source, priming_samples = resolve_priming(skip_samples, 0)
    priming_known = priming_source != "unknown"
    sample_rate = fd.streams[audio_idx]["sample_rate"]

    priming_ticks: Optional[int] = None
    if priming_known and sample_rate is not None:
        priming_ticks = priming_samples_to_ticks(priming_samples, sample_rate, audio_tb)
    priming_adjusted = priming_ticks is not None

    adjusted_audio_ticks = audio_first_pts_ticks + (priming_ticks or 0)
    audio_raw_ms = ticks_to_ms(audio_first_pts_ticks, audio_tb)
    audio_adjusted_ms = ticks_to_ms(adjusted_audio_ticks, audio_tb)
    raw_offset_ms = audio_raw_ms - video_first_pts_ms
    adjusted_offset_ms = audio_adjusted_ms - video_first_pts_ms
    comparison_basis = "adjusted" if priming_adjusted else "raw"

    audio_pts_span = sorted_pts_with_span(audio_packets)
    audio_declared = fd.streams[audio_idx]["declared_duration_ticks"]
    if audio_declared is not None and audio_declared > 0:
        audio_span_ticks = audio_declared
    elif audio_pts_span.has_span:
        audio_span_ticks = audio_pts_span.span_ticks
    else:
        return None

    audio_start_ticks = adjusted_audio_ticks if priming_adjusted else audio_first_pts_ticks
    video_start_ticks = video_pts_span.pts[0]
    priming_shift = priming_ticks if priming_adjusted else 0

    checkpoints: List[Tuple[int, int]] = []
    trajectory: List[Tuple[int, int, int]] = []
    for k in range(K_DRIFT_CHECKPOINT_COUNT):
        video_delta_ticks = trunc_div(k * video_span_ticks, K_DRIFT_CHECKPOINT_COUNT - 1)
        target_v_ticks = video_start_ticks + video_delta_ticks
        t_v_ticks = nearest_tick(video_pts_span.pts, target_v_ticks)
        delta_from_video_start_ticks = t_v_ticks - video_start_ticks

        audio_delta_ticks = trunc_div(delta_from_video_start_ticks * audio_span_ticks, video_span_ticks)
        target_a_ticks = audio_start_ticks + audio_delta_ticks
        raw_target_a_ticks = target_a_ticks - priming_shift
        raw_t_a_ticks = clamp_into_nearest_packet(
            audio_pts_span.pts, audio_pts_span.durations, audio_pts_span.nominal_duration_ticks, raw_target_a_ticks
        )

        if len(audio_pts_span.pts) >= 2 and audio_pts_span.nominal_duration_ticks > 0:
            index_based_raw_ticks = index_proportional_raw_ticks(
                audio_pts_span.pts, delta_from_video_start_ticks, video_span_ticks
            )
            divergence = raw_t_a_ticks - index_based_raw_ticks
            divergence_cap = audio_pts_span.nominal_duration_ticks * K_NOMINAL_DURATION_CAP_MULTIPLIER
            if abs(divergence) > divergence_cap:
                raw_t_a_ticks = index_based_raw_ticks

        t_a_ticks = raw_t_a_ticks + priming_shift
        t_v_ms = ticks_to_ms(t_v_ticks, video_tb)
        t_a_ms = ticks_to_ms(t_a_ticks, audio_tb)
        offset_ms = t_a_ms - t_v_ms
        checkpoints.append((t_v_ms, offset_ms))
        trajectory.append((k, t_v_ms, offset_ms))

    fit = fit_drift(checkpoints)
    if fit is None:
        return None
    return DriftResult(trajectory, fit, raw_offset_ms, adjusted_offset_ms, comparison_basis)


def primary_video_and_audio(fd: FileData) -> Tuple[Optional[int], List[int]]:
    video_indices = sorted(i for i, s in fd.streams.items() if s["codec_type"] == "video")
    audio_indices = sorted(i for i, s in fd.streams.items() if s["codec_type"] == "audio")
    video_idx = video_indices[0] if video_indices else None
    return video_idx, audio_indices


# --------------------------------------------------------------------------
# calibrate -- re-derive the shipped result for each calibration fixture,
# compare exact-field equality against `mediadiff snapshot`'s own real
# evidence. See 05-STEP-DESIGN.md's Calibration section for the results.
# --------------------------------------------------------------------------


def get_shipped_evidence(path: str) -> Tuple[Optional[dict], Optional[dict]]:
    tmp = tempfile.mkdtemp(prefix="mediadiff-calib-")
    try:
        out_path = os.path.join(tmp, "out.snap.json")
        result = subprocess.run(
            [mediadiff_bin(), "snapshot", path, "--out", out_path], capture_output=True, text=True
        )
        if result.returncode != 0:
            raise RuntimeError(f"mediadiff snapshot failed for {path}: {result.stderr}")
        with open(out_path) as f:
            snap = json.load(f)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    measurements = snap.get("measurements", [])
    drift = next((m for m in measurements if m.get("id") == "timeline.av_drift"), None)
    pattern = next((m for m in measurements if m.get("id") == "timeline.av_drift.pattern"), None)
    return drift, pattern


def compare_result(harness: DriftResult, drift_m: Optional[dict], pattern_m: Optional[dict]) -> Tuple[bool, str]:
    if drift_m is None or pattern_m is None:
        return False, "shipped measurement missing (skip?)"

    shipped_traj = drift_m.get("evidence", {}).get("trajectory", [])
    if len(shipped_traj) != len(harness.trajectory):
        return False, f"trajectory length: shipped={len(shipped_traj)} harness={len(harness.trajectory)}"
    for i, (sc, (hk, ht, ho)) in enumerate(zip(shipped_traj, harness.trajectory)):
        if sc["k"] != hk or sc["t_v_ms"] != ht or sc["offset_ms"] != ho:
            return False, f"checkpoint {i}: shipped={sc} harness=(k={hk},t_v_ms={ht},offset_ms={ho})"

    shipped_end_delta = drift_m["evidence"]["end_delta_ms"]
    if shipped_end_delta != harness.fit.end_delta_ms:
        return False, f"end_delta_ms: shipped={shipped_end_delta} harness={harness.fit.end_delta_ms}"

    shipped_residual_max = drift_m["evidence"]["residual_max_ms"]
    if shipped_residual_max != harness.fit.residual_max_ms:
        return False, f"residual_max_ms: shipped={shipped_residual_max} harness={harness.fit.residual_max_ms}"

    shipped_step = drift_m["evidence"].get("step_time_ms")
    if shipped_step != harness.fit.step_time_ms:
        return False, f"step_time_ms: shipped={shipped_step} harness={harness.fit.step_time_ms}"

    shipped_pattern = pattern_m["value"]
    if shipped_pattern != harness.fit.pattern:
        return False, f"pattern: shipped={shipped_pattern} harness={harness.fit.pattern}"

    return True, ""


def calibrate() -> int:
    fixtures_dir = os.path.join(repo_root(), "tests", "fixtures")
    all_pass = True
    pass_count = 0
    for name in CALIBRATION_FIXTURES:
        path = os.path.join(fixtures_dir, name)
        try:
            fd = load_file(path)
            video_idx, audio_indices = primary_video_and_audio(fd)
            if video_idx is None or not audio_indices:
                print(f"FAIL: {name} -- no video/audio stream")
                all_pass = False
                continue
            audio_idx = audio_indices[0]
            harness = compute_shipped_drift(fd, video_idx, audio_idx)
            if harness is None:
                print(f"FAIL: {name} -- harness produced no drift result (would skip)")
                all_pass = False
                continue
            drift_m, pattern_m = get_shipped_evidence(path)
            ok, detail = compare_result(harness, drift_m, pattern_m)
        except Exception as exc:  # noqa: BLE001 -- calibration must report, never silently pass
            ok = False
            detail = f"EXCEPTION: {exc}"
        if ok:
            pass_count += 1
            print(f"PASS: {name}")
        else:
            all_pass = False
            print(f"FAIL: {name} -- {detail}")

    print(f"\n{pass_count}/{len(CALIBRATION_FIXTURES)} PASS")
    return 0 if all_pass and pass_count == len(CALIBRATION_FIXTURES) else 1


# --------------------------------------------------------------------------
# Candidate mappings (D1, D2) -- Task 2. Each is a pure function from a
# FileData to a per-audio-stream DriftResult, sharing the unchanged
# fit_drift above. See 05-STEP-DESIGN.md "Candidate designs" for the design
# rationale.
# --------------------------------------------------------------------------


def detect_discontinuities(pts_span: PtsSpan, tb: Tuple[int, int]) -> List[int]:
    """Indices (into pts_span.pts, ascending) where a genuine timestamp
    discontinuity is detected: the gap to the NEXT entry exceeds
    kNominalDurationCapMultiplier times this stream's own nominal (median)
    packet duration -- the SAME fixed, stream-derived threshold
    clamp_into_nearest_packet's own containment cap already uses (D-08: a
    detection constant derived from the stream's own data, never an
    arbitrary configurable knob). Returns the index of the LAST packet
    before each detected gap (the split point: segment N ends here,
    segment N+1 starts at the next index)."""
    if pts_span.nominal_duration_ticks <= 0:
        return []
    threshold = pts_span.nominal_duration_ticks * K_NOMINAL_DURATION_CAP_MULTIPLIER
    splits = []
    for i in range(len(pts_span.pts) - 1):
        end_of_i = pts_span.pts[i] + pts_span.durations[i]
        gap = pts_span.pts[i + 1] - end_of_i
        if gap > threshold:
            splits.append(i)
    return splits


def segment_ranges(pts_span: PtsSpan, splits: List[int]) -> List[Tuple[int, int]]:
    """[start_index, end_index] (inclusive) pairs for each contiguous
    segment, given `splits` (indices ending a segment, from
    detect_discontinuities)."""
    ranges = []
    start = 0
    for s in splits:
        ranges.append((start, s))
        start = s + 1
    ranges.append((start, len(pts_span.pts) - 1))
    return ranges


def segment_for_checkpoint_fraction(
    ranges: List[Tuple[int, int]], pts_span: PtsSpan, fraction_num: int, fraction_den: int
) -> int:
    """Which segment (by position in `ranges`) the video-timeline fraction
    `fraction_num/fraction_den` falls into, mapped by SPAN proportion (each
    segment claims a share of the whole span proportional to its own
    [start,end] extent) -- returns the segment's own list index."""
    total_span = pts_span.pts[ranges[-1][1]] + pts_span.durations[ranges[-1][1]] - pts_span.pts[ranges[0][0]]
    if total_span <= 0 or len(ranges) == 1:
        return 0
    target_offset_from_start = Fraction(fraction_num, fraction_den) * total_span
    cursor = 0
    for i, (s, e) in enumerate(ranges):
        seg_span = pts_span.pts[e] + pts_span.durations[e] - pts_span.pts[s]
        if i == len(ranges) - 1:
            return i
        if target_offset_from_start < cursor + seg_span:
            return i
        cursor += seg_span
    return len(ranges) - 1


def compute_candidate_drift(
    fd: FileData,
    video_idx: int,
    audio_idx: int,
    design: str,
    span_source: str,
) -> Optional[DriftResult]:
    """design: "D0" (shipped, unchanged), "D1" (segment-proportional),
    "D2" (media-clock). span_source: "declared" (shipped preference) or
    "observed" (first-to-last packet extent, A2)."""
    if design == "D0":
        if span_source == "declared":
            return compute_shipped_drift(fd, video_idx, audio_idx)
        return _compute_drift_with_span_source(fd, video_idx, audio_idx, design="D0", span_source=span_source)

    return _compute_drift_with_span_source(fd, video_idx, audio_idx, design=design, span_source=span_source)


def _observed_span(pts_span: PtsSpan) -> Optional[int]:
    return pts_span.span_ticks if pts_span.has_span else None


def _resolve_span(
    declared_ticks: Optional[int], pts_span: PtsSpan, span_source: str
) -> Optional[int]:
    if span_source == "declared":
        if declared_ticks is not None and declared_ticks > 0:
            return declared_ticks
        return _observed_span(pts_span)
    # span:observed -- always prefer the packet-derived extent; fall back to
    # declared only when the container carries no per-stream declared
    # duration AND no usable packet span either (never happens in practice,
    # kept for parity with D0's own "no span at all" skip path).
    observed = _observed_span(pts_span)
    if observed is not None:
        return observed
    if declared_ticks is not None and declared_ticks > 0:
        return declared_ticks
    return None


def _compute_drift_with_span_source(
    fd: FileData, video_idx: int, audio_idx: int, design: str, span_source: str
) -> Optional[DriftResult]:
    views = make_timeline_packet_views(fd.packets, fd.is_ts)
    video_packets = views[video_idx]
    video_tb = fd.streams[video_idx]["tb"]
    video_first_pts_ticks = first_presented_pts(video_packets)
    if video_first_pts_ticks is None:
        return None
    video_first_pts_ms = ticks_to_ms(video_first_pts_ticks, video_tb)

    video_pts_span = sorted_pts_with_span(video_packets)
    video_span_ticks = _resolve_span(fd.streams[video_idx]["declared_duration_ticks"], video_pts_span, span_source)
    if video_span_ticks is None:
        return None

    audio_packets = views[audio_idx]
    audio_tb = fd.streams[audio_idx]["tb"]
    audio_first_pts_ticks = first_presented_pts(audio_packets)
    if audio_first_pts_ticks is None:
        return None

    skip_samples = fd.first_packet_skip_samples.get(audio_idx)
    priming_source, priming_samples = resolve_priming(skip_samples, 0)
    priming_known = priming_source != "unknown"
    sample_rate = fd.streams[audio_idx]["sample_rate"]
    priming_ticks: Optional[int] = None
    if priming_known and sample_rate is not None:
        priming_ticks = priming_samples_to_ticks(priming_samples, sample_rate, audio_tb)
    priming_adjusted = priming_ticks is not None
    priming_shift = priming_ticks if priming_adjusted else 0

    adjusted_audio_ticks = audio_first_pts_ticks + (priming_ticks or 0)
    audio_raw_ms = ticks_to_ms(audio_first_pts_ticks, audio_tb)
    audio_adjusted_ms = ticks_to_ms(adjusted_audio_ticks, audio_tb)
    raw_offset_ms = audio_raw_ms - video_first_pts_ms
    adjusted_offset_ms = audio_adjusted_ms - video_first_pts_ms
    comparison_basis = "adjusted" if priming_adjusted else "raw"

    audio_pts_span = sorted_pts_with_span(audio_packets)
    audio_span_ticks = _resolve_span(fd.streams[audio_idx]["declared_duration_ticks"], audio_pts_span, span_source)
    if audio_span_ticks is None:
        return None

    audio_start_ticks = adjusted_audio_ticks if priming_adjusted else audio_first_pts_ticks
    video_start_ticks = video_pts_span.pts[0]

    if design == "D0":
        checkpoints = _checkpoints_whole_file(
            video_pts_span, audio_pts_span, video_span_ticks, audio_span_ticks, video_start_ticks,
            audio_start_ticks, priming_shift, video_tb, audio_tb,
        )
    elif design == "D1":
        checkpoints = _checkpoints_segment_proportional(
            video_pts_span, audio_pts_span, video_start_ticks, audio_start_ticks, priming_shift, video_tb, audio_tb,
        )
    elif design == "D2":
        checkpoints = _checkpoints_media_clock(
            video_pts_span, audio_pts_span, video_start_ticks, audio_start_ticks, priming_shift, video_tb, audio_tb,
        )
    else:
        raise ValueError(f"unknown design {design}")

    if checkpoints is None:
        return None

    fit = fit_drift([(t_v_ms, off_ms) for (_k, t_v_ms, off_ms) in checkpoints])
    if fit is None:
        return None
    return DriftResult(checkpoints, fit, raw_offset_ms, adjusted_offset_ms, comparison_basis)


def _checkpoints_whole_file(
    video_pts_span: PtsSpan, audio_pts_span: PtsSpan, video_span_ticks: int, audio_span_ticks: int,
    video_start_ticks: int, audio_start_ticks: int, priming_shift: int, video_tb, audio_tb,
) -> Optional[List[Tuple[int, int, int]]]:
    """D0: the SHIPPED single whole-file affine map (05-10's own
    construction), re-derived here (rather than calling
    compute_shipped_drift) so it can be run under BOTH span:declared and
    span:observed -- the ONLY difference from compute_shipped_drift is
    which span the caller resolved."""
    trajectory: List[Tuple[int, int, int]] = []
    for k in range(K_DRIFT_CHECKPOINT_COUNT):
        video_delta_ticks = trunc_div(k * video_span_ticks, K_DRIFT_CHECKPOINT_COUNT - 1)
        target_v_ticks = video_start_ticks + video_delta_ticks
        t_v_ticks = nearest_tick(video_pts_span.pts, target_v_ticks)
        delta_from_video_start_ticks = t_v_ticks - video_start_ticks

        audio_delta_ticks = trunc_div(delta_from_video_start_ticks * audio_span_ticks, video_span_ticks)
        target_a_ticks = audio_start_ticks + audio_delta_ticks
        raw_target_a_ticks = target_a_ticks - priming_shift
        raw_t_a_ticks = clamp_into_nearest_packet(
            audio_pts_span.pts, audio_pts_span.durations, audio_pts_span.nominal_duration_ticks, raw_target_a_ticks
        )
        if len(audio_pts_span.pts) >= 2 and audio_pts_span.nominal_duration_ticks > 0:
            index_based = index_proportional_raw_ticks(audio_pts_span.pts, delta_from_video_start_ticks, video_span_ticks)
            divergence = raw_t_a_ticks - index_based
            cap = audio_pts_span.nominal_duration_ticks * K_NOMINAL_DURATION_CAP_MULTIPLIER
            if abs(divergence) > cap:
                raw_t_a_ticks = index_based
        t_a_ticks = raw_t_a_ticks + priming_shift
        t_v_ms = ticks_to_ms(t_v_ticks, video_tb)
        t_a_ms = ticks_to_ms(t_a_ticks, audio_tb)
        trajectory.append((k, t_v_ms, t_a_ms - t_v_ms))
    return trajectory


def _checkpoints_segment_proportional(
    video_pts_span: PtsSpan, audio_pts_span: PtsSpan, video_start_ticks: int, audio_start_ticks: int,
    priming_shift: int, video_tb, audio_tb,
) -> Optional[List[Tuple[int, int, int]]]:
    """D1: detect per-stream timestamp discontinuities (own threshold,
    §detect_discontinuities), split VIDEO and AUDIO into their own
    segments, and map k/K -> a video segment (by whole-file span
    proportion) -> the CORRESPONDING audio segment by ordinal position
    (segment i of N on each side, assuming both sides split into the same
    number of segments -- the "corresponding segments" A1 flags as this
    design's own assumption: a timestamp discontinuity means CONTENT is
    genuinely missing/resumed on BOTH streams at once). Within a segment,
    maps time-proportionally exactly like D0's whole-file map, but scoped
    to that segment's own [start,end] extent -- never the whole file's."""
    video_splits = detect_discontinuities(video_pts_span, video_tb)
    audio_splits = detect_discontinuities(audio_pts_span, audio_tb)
    video_ranges = segment_ranges(video_pts_span, video_splits)
    audio_ranges = segment_ranges(audio_pts_span, audio_splits)

    if len(video_ranges) != len(audio_ranges):
        # A1's own assumption failed for this file (the two streams'
        # discontinuity counts disagree) -- design does not apply; caller
        # treats this as "no result" (a documented limitation, not a
        # fabricated trajectory).
        return None

    trajectory: List[Tuple[int, int, int]] = []
    for k in range(K_DRIFT_CHECKPOINT_COUNT):
        target_v_ticks = video_start_ticks + trunc_div(
            k * (video_pts_span.pts[-1] + video_pts_span.durations[-1] - video_start_ticks), K_DRIFT_CHECKPOINT_COUNT - 1
        )
        t_v_ticks = nearest_tick(video_pts_span.pts, target_v_ticks)
        seg_idx = segment_for_checkpoint_fraction(video_ranges, video_pts_span, k, K_DRIFT_CHECKPOINT_COUNT - 1)

        v_s, v_e = video_ranges[seg_idx]
        a_s, a_e = audio_ranges[seg_idx]
        v_seg_start = video_pts_span.pts[v_s]
        v_seg_end = video_pts_span.pts[v_e] + video_pts_span.durations[v_e]
        v_seg_span = v_seg_end - v_seg_start
        a_seg_start_raw = audio_pts_span.pts[a_s]
        a_seg_end_raw = audio_pts_span.pts[a_e] + audio_pts_span.durations[a_e]
        a_seg_span = a_seg_end_raw - a_seg_start_raw

        delta_in_segment = t_v_ticks - v_seg_start
        if v_seg_span <= 0:
            audio_delta = 0
        else:
            audio_delta = trunc_div(delta_in_segment * a_seg_span, v_seg_span) if a_seg_span > 0 else 0
        a_seg_start_adjusted = a_seg_start_raw + priming_shift if a_s == 0 else a_seg_start_raw
        target_a_raw = a_seg_start_raw + audio_delta
        raw_t_a_ticks = clamp_into_nearest_packet(
            audio_pts_span.pts[a_s : a_e + 1],
            audio_pts_span.durations[a_s : a_e + 1],
            audio_pts_span.nominal_duration_ticks,
            target_a_raw,
        )
        t_a_ticks = raw_t_a_ticks + (priming_shift if a_s == 0 else 0)

        t_v_ms = ticks_to_ms(t_v_ticks, video_tb)
        t_a_ms = ticks_to_ms(t_a_ticks, audio_tb)
        trajectory.append((k, t_v_ms, t_a_ms - t_v_ms))
    return trajectory


def _checkpoints_media_clock(
    video_pts_span: PtsSpan, audio_pts_span: PtsSpan, video_start_ticks: int, audio_start_ticks: int,
    priming_shift: int, video_tb, audio_tb,
) -> Optional[List[Tuple[int, int, int]]]:
    """D2: like D1, but the AUDIO clock has every detected discontinuity's
    own GAP DURATION removed before the proportional map (a "media clock"
    that only advances when audio content is actually present), then
    offsets are measured on the REAL (gap-containing) timestamps. This is
    what makes a genuine sync STEP (content-contiguous, timestamps jump)
    distinguishable in principle from a dropout (content missing,
    timestamps preserved) IF the caller also knows which reading is meant
    -- D2 assumes "sync stepped" (A1), identically to D1, but represents
    the audio segments in a DEDUPLICATED clock so segment boundaries align
    to real elapsed media time rather than raw segment index count."""
    video_splits = detect_discontinuities(video_pts_span, video_tb)
    audio_splits = detect_discontinuities(audio_pts_span, audio_tb)
    video_ranges = segment_ranges(video_pts_span, video_splits)
    audio_ranges = segment_ranges(audio_pts_span, audio_splits)
    if len(video_ranges) != len(audio_ranges):
        return None

    # Media-clock audio segment starts: cumulative real (gap-closed) audio
    # duration up to the start of each segment.
    media_clock_starts = []
    cumulative = 0
    for (a_s, a_e) in audio_ranges:
        media_clock_starts.append(cumulative)
        seg_span = audio_pts_span.pts[a_e] + audio_pts_span.durations[a_e] - audio_pts_span.pts[a_s]
        cumulative += max(seg_span, 0)
    media_clock_total = cumulative

    trajectory: List[Tuple[int, int, int]] = []
    for k in range(K_DRIFT_CHECKPOINT_COUNT):
        target_v_ticks = video_start_ticks + trunc_div(
            k * (video_pts_span.pts[-1] + video_pts_span.durations[-1] - video_start_ticks), K_DRIFT_CHECKPOINT_COUNT - 1
        )
        t_v_ticks = nearest_tick(video_pts_span.pts, target_v_ticks)
        seg_idx = segment_for_checkpoint_fraction(video_ranges, video_pts_span, k, K_DRIFT_CHECKPOINT_COUNT - 1)

        v_s, v_e = video_ranges[seg_idx]
        a_s, a_e = audio_ranges[seg_idx]
        v_seg_start = video_pts_span.pts[v_s]
        v_seg_end = video_pts_span.pts[v_e] + video_pts_span.durations[v_e]
        v_seg_span = v_seg_end - v_seg_start
        a_seg_start_raw = audio_pts_span.pts[a_s]
        a_seg_end_raw = audio_pts_span.pts[a_e] + audio_pts_span.durations[a_e]
        a_seg_span = a_seg_end_raw - a_seg_start_raw

        delta_in_segment = t_v_ticks - v_seg_start
        audio_delta = trunc_div(delta_in_segment * a_seg_span, v_seg_span) if v_seg_span > 0 and a_seg_span > 0 else 0
        target_a_raw = a_seg_start_raw + audio_delta
        raw_t_a_ticks = clamp_into_nearest_packet(
            audio_pts_span.pts[a_s : a_e + 1],
            audio_pts_span.durations[a_s : a_e + 1],
            audio_pts_span.nominal_duration_ticks,
            target_a_raw,
        )
        t_a_ticks = raw_t_a_ticks + (priming_shift if a_s == 0 else 0)

        t_v_ms = ticks_to_ms(t_v_ticks, video_tb)
        t_a_ms = ticks_to_ms(t_a_ticks, audio_tb)
        trajectory.append((k, t_v_ms, t_a_ms - t_v_ms))
    return trajectory


# --------------------------------------------------------------------------
# evaluate -- run every design over the fixed panel, print a table.
# --------------------------------------------------------------------------

DESIGNS = ["D0", "D1", "D2"]
SPAN_SOURCES = ["declared", "observed"]


def evaluate(fixtures_dir: Optional[str]) -> int:
    fixtures_dirs = [os.path.join(repo_root(), "tests", "fixtures")]
    if fixtures_dir:
        fixtures_dirs.insert(0, fixtures_dir)

    def find_fixture(name: str) -> Optional[str]:
        for d in fixtures_dirs:
            candidate = os.path.join(d, name)
            if os.path.exists(candidate):
                return candidate
        return None

    all_pairs = PANEL_NO_REGRESSION_PAIRS + PANEL_FALSE_POSITIVE_GUARDS
    header = f"{'pair':60s} {'design':10s} {'span':10s} {'pattern':16s} {'rate(ms/min)':16s} {'end_delta_ms':13s} {'residual_max_ms':16s} {'step_time_ms':13s}"
    print(header)
    print("-" * len(header))

    had_error = False
    for baseline_name, candidate_name in all_pairs:
        baseline_path = find_fixture(baseline_name)
        candidate_path = find_fixture(candidate_name)
        if baseline_path is None or candidate_path is None:
            print(f"{baseline_name} vs {candidate_name}: SKIP (fixture not found; run recipes.py first)")
            continue
        for fname, fpath in ((baseline_name, baseline_path), (candidate_name, candidate_path)):
            try:
                fd = load_file(fpath)
                video_idx, audio_indices = primary_video_and_audio(fd)
                if video_idx is None or not audio_indices:
                    print(f"{fname:60s} -- no video/audio stream")
                    continue
                audio_idx = audio_indices[0]
                for design in DESIGNS:
                    for span in SPAN_SOURCES:
                        if design == "D0" and span == "declared":
                            result = compute_shipped_drift(fd, video_idx, audio_idx)
                        else:
                            result = compute_candidate_drift(fd, video_idx, audio_idx, design, span)
                        if result is None:
                            print(f"{fname:60s} {design:10s} {span:10s} {'skip':16s}")
                            continue
                        rate = Fraction(result.fit.rate_num, result.fit.rate_den)
                        print(
                            f"{fname:60s} {design:10s} {span:10s} {result.fit.pattern:16s} "
                            f"{str(rate):16s} {result.fit.end_delta_ms:<13d} {result.fit.residual_max_ms:<16d} "
                            f"{str(result.fit.step_time_ms):13s}"
                        )
            except Exception as exc:  # noqa: BLE001
                had_error = True
                print(f"{fname:60s} -- ERROR: {exc}")

    return 1 if had_error else 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("calibrate")
    evaluate_parser = sub.add_parser("evaluate")
    evaluate_parser.add_argument(
        "--fixtures-dir", default=None, help="directory containing recipes.py's V1-V4 outputs (default: tests/fixtures only)"
    )
    args = parser.parse_args()

    if args.command == "calibrate":
        return calibrate()
    if args.command == "evaluate":
        return evaluate(args.fixtures_dir)
    parser.print_help()
    return 1


if __name__ == "__main__":
    sys.exit(main())
