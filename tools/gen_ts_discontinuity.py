#!/usr/bin/env python3
"""Sets the MPEG-TS adaptation-field `discontinuity_indicator` bit on ONE
targeted transport packet, byte-for-byte, without re-muxing anything
(05-07-PLAN.md Task 3, TIME-02/TIME-04). No pinned ffmpeg CLI/filter can
set this bit on a chosen packet -- doc 02 section 5's own `discontinuity_
indicator` handling is a raw MPEG-TS adaptation-field flag with no
avconv-level knob -- so this writer edits the already-muxed bytes
directly, the same byte-level-patch technique `tools/gen_video_fixtures.py`
already uses for the `dvcC`/`pasp` box edits (D-01/D-02/D-03 there apply
identically here: every output enters `tests/golden/CORPUS_DIGEST.txt`
like any other corpus member, no exemption; this writer is Python 3.11,
stdlib-only; every write goes through `write_atomic`, a sibling temp file
plus `os.replace`, so a concurrent build never observes a half-written
fixture).

MPEG-TS packet layout this writer edits (ISO/IEC 13818-1 section 2.4.3.2/
2.4.3.3), assuming the common 188-byte stride every fixture in this
project's corpus actually uses (`scripts/gen_corpus.sh` never produces the
192/204-byte variants for this recipe):

  byte 0            sync_byte (0x47)
  byte 1            transport_error_indicator(1) | payload_unit_start(1)
                     | transport_priority(1) | PID high 5 bits
  byte 2            PID low 8 bits
  byte 3            transport_scrambling_control(2) |
                     adaptation_field_control(2) | continuity_counter(4)
  byte 4 (if AF)     adaptation_field_length
  byte 5 (if AF,
          length>=1) discontinuity_indicator(1) | random_access(1) |
                     ...

`adaptation_field_control` (byte 3, bits 5-4): `01` payload-only (no
adaptation field at all), `10` adaptation-only, `11` both, `00` reserved
(never legally produced by a conformant muxer -- this writer refuses
rather than guess when it encounters that value on the targeted packet).

Refuses (raises FixtureError) rather than guess whenever the edit would
change the packet's own 188-byte size, or when the target packet's own
adaptation_field_control is the reserved value -- never rewrites a packet
it did not target.
"""

import argparse
import os
import sys
import tempfile

if sys.version_info < (3, 11):
    sys.stderr.write(
        "gen_ts_discontinuity.py requires Python >= 3.11; found "
        f"{sys.version_info.major}.{sys.version_info.minor}.\n"
    )
    sys.exit(1)


class FixtureError(Exception):
    """Raised for any argument/input this writer cannot honour. Never
    leaves a partial file behind -- every write goes through write_atomic,
    which never creates the final path until the full byte string is
    already built."""


TS_PACKET_SIZE = 188
TS_SYNC_BYTE = 0x47

# adaptation_field_control values (byte 3, bits 5-4).
AF_CONTROL_RESERVED = 0x0
AF_CONTROL_PAYLOAD_ONLY = 0x1
AF_CONTROL_ADAPTATION_ONLY = 0x2
AF_CONTROL_BOTH = 0x3

DISCONTINUITY_INDICATOR_BIT = 0x80


def packet_pid(packet):
    return ((packet[1] & 0x1F) << 8) | packet[2]


def set_discontinuity_indicator(packet):
    """Returns a NEW 188-byte bytes object, identical to `packet` except
    that the adaptation field's discontinuity_indicator bit is now set --
    inserting an adaptation field (consuming trailing payload bytes to
    keep the packet exactly 188 bytes) only if the packet does not already
    carry one. Raises FixtureError rather than silently accepting a
    malformed/unhandleable input; never returns a result of any size other
    than exactly 188 bytes."""
    if len(packet) != TS_PACKET_SIZE:
        raise FixtureError(f"packet is {len(packet)} bytes, expected exactly {TS_PACKET_SIZE}")
    if packet[0] != TS_SYNC_BYTE:
        raise FixtureError(f"packet does not start with the TS sync byte 0x{TS_SYNC_BYTE:02x} (got 0x{packet[0]:02x})")

    out = bytearray(packet)
    af_control = (out[3] >> 4) & 0x3

    if af_control == AF_CONTROL_RESERVED:
        raise FixtureError("adaptation_field_control is the reserved value 00 -- refusing to guess")

    if af_control in (AF_CONTROL_ADAPTATION_ONLY, AF_CONTROL_BOTH):
        af_len = out[4]
        if af_len >= 1:
            # The flags byte already exists (index 5) -- overwrite its
            # discontinuity_indicator bit IN PLACE. No size change, every
            # other field (including every other flag bit and the PCR/
            # OPCR/splice fields that may follow) is byte-identical.
            out[5] |= DISCONTINUITY_INDICATOR_BIT
            return bytes(out)
        if af_control == AF_CONTROL_BOTH:
            # af_length == 0 with payload present: grow the adaptation
            # field by exactly one byte (the flags byte), consuming one
            # trailing payload byte to keep the total size at 188.
            out[4] = 1
            out.insert(5, DISCONTINUITY_INDICATOR_BIT)
            del out[TS_PACKET_SIZE:]
            return bytes(out)
        # AF_CONTROL_ADAPTATION_ONLY with af_length == 0: no payload
        # bytes exist anywhere in this packet to consume from (ISO
        # 13818-1's own adaptation-only convention pads af_length to 183
        # to fill the packet; a genuine af_length==0 here leaves nothing
        # this writer can shrink to make room) -- refuse rather than
        # guess.
        raise FixtureError(
            "adaptation-field-only packet with adaptation_field_length==0 has no payload byte to consume from "
            "-- refusing to guess"
        )

    # AF_CONTROL_PAYLOAD_ONLY: no adaptation field exists at all. Insert
    # adaptation_field_length=1 plus the flags byte (discontinuity_
    # indicator set, every other flag bit 0), consuming exactly 2 bytes
    # from the trailing payload to keep the packet at 188 bytes.
    # adaptation_field_control moves from '01' to '11' (bits 5-4 of byte
    # 3); every other bit of byte 3 (scrambling, continuity_counter) is
    # left untouched.
    out[3] = (out[3] & 0xCF) | (AF_CONTROL_BOTH << 4)
    out[4:4] = bytes([1, DISCONTINUITY_INDICATOR_BIT])
    del out[TS_PACKET_SIZE:]
    return bytes(out)


def find_target_packet_offset(data, pid, after_offset):
    """Scans plain 188-byte-stride TS packets starting at the first
    stride-aligned offset >= `after_offset`, returning the byte offset of
    the FIRST packet on `pid`. Returns None if none is found before EOF.
    Refuses (raises FixtureError) rather than proceed if the file's own
    length is not an exact multiple of 188 bytes -- a non-stride-aligned
    file is not one this writer's caller-named-byte-offset targeting
    contract can honour safely."""
    if len(data) % TS_PACKET_SIZE != 0:
        raise FixtureError(
            f"input is {len(data)} bytes, not an exact multiple of {TS_PACKET_SIZE} -- refusing to guess a stride"
        )
    start = ((after_offset + TS_PACKET_SIZE - 1) // TS_PACKET_SIZE) * TS_PACKET_SIZE
    offset = start
    while offset + TS_PACKET_SIZE <= len(data):
        packet = data[offset : offset + TS_PACKET_SIZE]
        if packet[0] == TS_SYNC_BYTE and packet_pid(packet) == pid:
            return offset
        offset += TS_PACKET_SIZE
    return None


def apply_discontinuity_edit(data, pid, after_offset):
    """Returns a NEW bytes object: `data` with exactly ONE targeted
    packet's discontinuity_indicator bit set. Never rewrites a packet it
    did not target -- every byte outside the one targeted 188-byte window
    is passed through unchanged."""
    offset = find_target_packet_offset(data, pid, after_offset)
    if offset is None:
        raise FixtureError(
            f"no transport packet on PID {pid} found at or after byte offset {after_offset} -- refusing to guess"
        )
    edited_packet = set_discontinuity_indicator(data[offset : offset + TS_PACKET_SIZE])
    out = bytearray(data)
    out[offset : offset + TS_PACKET_SIZE] = edited_packet
    return bytes(out), offset


# ---------------------------------------------------------------------------
# Atomic file I/O (D-03, mirrors tools/gen_video_fixtures.py's own write_atomic).
# ---------------------------------------------------------------------------


def write_atomic(path, data):
    directory = os.path.dirname(path) or "."
    if not os.path.isdir(directory):
        raise FixtureError(f"output directory '{directory}' does not exist for '{path}'")
    tmp_path = os.path.join(directory, f".{os.path.basename(path)}.tmp{os.getpid()}")
    try:
        with open(tmp_path, "wb") as f:
            f.write(data)
        os.replace(tmp_path, path)
    except OSError as exc:
        try:
            os.remove(tmp_path)
        except OSError:
            pass
        raise FixtureError(f"failed writing '{path}': {exc}") from exc


def read_file(path, *, what):
    if not os.path.isfile(path):
        raise FixtureError(f"{what} '{path}' does not exist")
    with open(path, "rb") as f:
        return f.read()


# ---------------------------------------------------------------------------
# Self-test (D-03's "every gate self-tests and refuses to pass vacuously"
# convention, matching check_corpus.sh/lint_bash4_builtins.sh/
# assert_corpus_digest.sh and tools/gen_video_fixtures.py's own --selftest).
# ---------------------------------------------------------------------------


def _synthetic_payload_only_packet(pid=0x100, cc=3):
    """A minimal, well-formed 188-byte TS packet: payload-only (no
    adaptation field), an arbitrary but deterministic 184-byte payload
    body so byte-identity assertions have real content to compare."""
    byte1 = ((pid >> 8) & 0x1F) | 0x40  # payload_unit_start_indicator set, arbitrary
    byte2 = pid & 0xFF
    byte3 = 0x10 | (cc & 0x0F)  # scrambling=00, af_control='01' (payload only), cc
    header = bytes([TS_SYNC_BYTE, byte1, byte2, byte3])
    payload = bytes((i * 7 + 11) % 256 for i in range(184))
    packet = header + payload
    assert len(packet) == TS_PACKET_SIZE
    return packet


def _synthetic_packet_with_adaptation_field(pid=0x101, cc=5, af_len=7):
    """A well-formed TS packet that already carries an adaptation field
    (af_control='11') of `af_len` bytes (>=1, so the flags byte at index 5
    exists), discontinuity_indicator initially CLEAR, random_access_
    indicator initially SET (an arbitrary, checkable flag this writer must
    leave untouched)."""
    byte1 = ((pid >> 8) & 0x1F) | 0x40
    byte2 = pid & 0xFF
    byte3 = 0x30 | (cc & 0x0F)  # af_control='11' (both)
    flags_byte = 0x40  # random_access_indicator set, discontinuity_indicator clear
    af_body = bytes([af_len, flags_byte]) + bytes((af_len - 1))  # af_len-1 filler bytes after the flags byte
    payload_len = TS_PACKET_SIZE - 4 - 1 - af_len
    payload = bytes((i * 13 + 3) % 256 for i in range(payload_len))
    packet = bytes([TS_SYNC_BYTE, byte1, byte2, byte3]) + af_body + payload
    assert len(packet) == TS_PACKET_SIZE
    return packet


def selftest():
    failures = []
    assertions_run = 0

    def check(condition, message):
        nonlocal assertions_run
        assertions_run += 1
        if not condition:
            failures.append(message)

    # --- Case 1: payload-only packet gains a fresh adaptation field. ------
    original = _synthetic_payload_only_packet(pid=0x100, cc=9)
    edited = set_discontinuity_indicator(original)
    check(len(edited) == TS_PACKET_SIZE, f"case 1: edited packet is {len(edited)} bytes, expected {TS_PACKET_SIZE}")
    check(edited[0] == TS_SYNC_BYTE, "case 1: sync byte was not preserved")
    check(edited[1] == original[1] and edited[2] == original[2], "case 1: PID bytes were not preserved")
    check((edited[3] & 0x30) >> 4 == AF_CONTROL_BOTH, "case 1: adaptation_field_control was not set to '11'")
    check((edited[3] & 0xCF) == (original[3] & 0xCF), "case 1: scrambling/continuity_counter bits were disturbed")
    check(edited[4] == 1, "case 1: adaptation_field_length was not set to 1")
    check(edited[5] == DISCONTINUITY_INDICATOR_BIT, "case 1: flags byte is not exactly discontinuity_indicator alone")
    check(
        edited[6:TS_PACKET_SIZE] == original[4 : TS_PACKET_SIZE - 2],
        "case 1: surviving payload bytes were not preserved verbatim (byte-identical outside the targeted field)",
    )
    check(
        set_discontinuity_indicator(original) == edited,
        "case 1: two invocations with identical input produced different bytes (determinism)",
    )

    # --- Case 2: packet already carrying an adaptation field -- ONLY the
    #     flags byte's own bit changes, nothing else. ----------------------
    original2 = _synthetic_packet_with_adaptation_field(pid=0x101, cc=2, af_len=7)
    edited2 = set_discontinuity_indicator(original2)
    check(len(edited2) == TS_PACKET_SIZE, f"case 2: edited packet is {len(edited2)} bytes, expected {TS_PACKET_SIZE}")
    check(edited2[:5] == original2[:5], "case 2: bytes before the flags byte were disturbed")
    check(edited2[5] == (original2[5] | DISCONTINUITY_INDICATOR_BIT), "case 2: discontinuity_indicator bit not set")
    check(
        (edited2[5] & ~DISCONTINUITY_INDICATOR_BIT) == (original2[5] & ~DISCONTINUITY_INDICATOR_BIT),
        "case 2: an UNRELATED flag bit in the flags byte was disturbed (e.g. random_access_indicator)",
    )
    check(edited2[6:] == original2[6:], "case 2: bytes after the flags byte were disturbed")

    # --- Case 3: a packet already carrying the flag SET is idempotent. ----
    already_flagged = bytearray(original2)
    already_flagged[5] |= DISCONTINUITY_INDICATOR_BIT
    idempotent = set_discontinuity_indicator(bytes(already_flagged))
    check(idempotent == bytes(already_flagged), "case 3: re-applying to an already-flagged packet changed bytes")

    # --- Case 4: the reserved adaptation_field_control value refuses. -----
    reserved = bytearray(_synthetic_payload_only_packet())
    reserved[3] = reserved[3] & 0xCF  # af_control -> '00', reserved
    raised = False
    try:
        set_discontinuity_indicator(bytes(reserved))
    except FixtureError:
        raised = True
    check(raised, "case 4: the reserved adaptation_field_control value did not refuse")
    assertions_run += 1

    # --- Case 5: find_target_packet_offset / apply_discontinuity_edit over
    #     a small synthetic multi-packet stream, proving the join. --------
    stream = (
        _synthetic_payload_only_packet(pid=0x200, cc=0)
        + _synthetic_payload_only_packet(pid=0x100, cc=1)
        + _synthetic_payload_only_packet(pid=0x100, cc=2)
    )
    edited_stream, found_offset = apply_discontinuity_edit(stream, pid=0x100, after_offset=0)
    check(found_offset == TS_PACKET_SIZE, f"case 5: expected the first PID-0x100 packet at offset 188, got {found_offset}")
    check(len(edited_stream) == len(stream), "case 5: whole-stream length changed")
    check(edited_stream[:TS_PACKET_SIZE] == stream[:TS_PACKET_SIZE], "case 5: an untargeted packet was rewritten")
    check(
        edited_stream[2 * TS_PACKET_SIZE :] == stream[2 * TS_PACKET_SIZE :],
        "case 5: an untargeted packet (after the target) was rewritten",
    )
    edited_target = edited_stream[TS_PACKET_SIZE : 2 * TS_PACKET_SIZE]
    check((edited_target[3] & 0x30) >> 4 == AF_CONTROL_BOTH, "case 5: targeted packet's own edit did not apply")

    # --- Case 6: no matching PID at/after the offset refuses cleanly. -----
    raised6 = False
    try:
        apply_discontinuity_edit(stream, pid=0x999, after_offset=0)
    except FixtureError:
        raised6 = True
    check(raised6, "case 6: a nonexistent target PID did not refuse")
    assertions_run += 1

    # --- Zero-assertions guard: this selftest must never pass vacuously. --
    if assertions_run == 0:
        failures.append("selftest ran ZERO assertions -- refusing to report a vacuous pass")

    if failures:
        sys.stderr.write(f"gen_ts_discontinuity.py --selftest: FAILED ({len(failures)} of {assertions_run} checks)\n")
        for f in sorted(failures):
            sys.stderr.write(f"  {f}\n")
        sys.exit(1)
    print(f"gen_ts_discontinuity.py --selftest: OK ({assertions_run} assertions run)")


# ---------------------------------------------------------------------------
# CLI.
# ---------------------------------------------------------------------------


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--input", help="input MPEG-TS file (plain 188-byte stride)")
    parser.add_argument("--output", help="output path for the edited copy")
    parser.add_argument("--pid", type=lambda s: int(s, 0), help="target PID (decimal or 0x-prefixed hex)")
    parser.add_argument(
        "--after-offset",
        type=lambda s: int(s, 0),
        default=0,
        help="byte offset to search from (inclusive); the FIRST matching-PID packet at or after this offset is edited",
    )
    parser.add_argument("--selftest", action="store_true", help="run this writer's own known-good/known-bad controls")
    args = parser.parse_args()

    if args.selftest:
        selftest()
        return

    missing = [name for name, value in (("--input", args.input), ("--output", args.output), ("--pid", args.pid)) if value is None]
    if missing:
        sys.stderr.write(f"gen_ts_discontinuity.py: missing required argument(s): {', '.join(missing)}\n")
        sys.exit(1)

    try:
        data = read_file(args.input, what="--input")
        edited, offset = apply_discontinuity_edit(data, args.pid, args.after_offset)
        write_atomic(args.output, edited)
        print(f"gen_ts_discontinuity.py: set discontinuity_indicator on the packet at byte offset {offset} in '{args.output}'")
    except FixtureError as exc:
        sys.stderr.write(f"gen_ts_discontinuity.py: {exc}\n")
        sys.exit(1)


if __name__ == "__main__":
    main()
