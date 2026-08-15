# meta.tool_version

## What it measures

The `mediadiff` tool-version string recorded in a fingerprint's envelope at
the time it was produced — a property of the measurement itself, not of the
media it describes.

## Why it matters

A tool-version mismatch between baseline and candidate means the two
fingerprints may not have been produced by comparable logic: check
semantics, tolerances, and even which checks exist at all can change
between releases. Surfacing the skew as its own finding — rather than
silently trusting whichever version produced the JSON — is what lets a
reviewer distinguish "the media changed" from "the tool changed."

## Accept / Tune / Silence

- **Accept**: re-run `mediadiff snapshot` on the current release to refresh
  the baseline, after confirming every other finding is also clean, so the
  stored baseline matches the tool that will keep comparing against it.
- **Tune**: none — this check has no tolerance; two tool versions either
  match exactly or they don't.
- **Silence**: set `meta.tool_version` to `ignore` in `[severity]` for CI
  legs that intentionally compare across tool versions (e.g. this
  project's own upgrade-migration tests), but leave it enabled everywhere
  else — trust never requires faith, so a silenced check's difference is
  still computed and shown under `-v`.
