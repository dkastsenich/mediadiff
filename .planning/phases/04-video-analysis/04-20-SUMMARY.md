---
phase: 04-video-analysis
plan: 20
subsystem: testing
tags: [ci, corpus-digest, designated-leg, github-actions, gh, draft-pr]

# Dependency graph
requires:
  - phase: 04-video-analysis
    provides: "04-13 restored main's designated-leg hashes and added the provisional ledger; 04-14 through 04-19 closed every other Phase 4 gap so the branch was worth publishing"
provides:
  - "Branch gsd/phase-04-video-analysis published to origin at 196b52a after an explicit human confirmation obtained immediately beforehand"
  - "Draft PR #5 against main opened after a second, separate explicit human confirmation"
  - "Verbatim x64-linux corpus digest listing (138 fixtures + CORPUS_DIGEST_SUMMARY) from CI run 34776142545, recorded below for 04-21 to transcribe"
  - "Cross-check verdict: every pre-existing fixture hash on the designated leg equals main's (78/78 compared; the excluded Opus pair is equal too)"
  - "FINDING outside this plan's scope: the x64-windows-static-md leg now fails at corpus generation because the pin reader does not tolerate CRLF python3 output"
affects: [04-21, corpus-digest, ci]

# Actuals (#2632)
actuals:
  tokens: 6000
  tasks: 3
  commits: 1

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Blocking-human checkpoints executed in the orchestrator's main context (execute-plan Pattern C): each outward-facing action was preceded by its own confirmation and no subagent ever held the ability to push or open the PR"

key-files:
  created:
    - .planning/phases/04-video-analysis/04-20-SUMMARY.md
  modified: []

key-decisions:
  - "Human Decision 4 route A taken: publish now, capture the designated leg's listing, let 04-21 transcribe it"
  - "The plan ran inline in the orchestrator (a checkpoint:decision routes to Pattern C), so the three gate="blocking-human" tasks were presented to the human directly and answered A / push / open the draft PR"
  - "The Windows leg's new corpus-generation failure is recorded as a FINDING and left unfixed here: this plan changes no source file and the pin reader is 04-21-adjacent CI plumbing, not a digest line"

patterns-established: []

requirements-completed: [BUILD-05, BUILD-08]

# Coverage metadata (#1602)
coverage:
  - id: D1
    description: "gsd/phase-04-video-analysis pushed to origin after an explicit human confirmation; the remote ref equals local HEAD"
    requirement: BUILD-05
    verification:
      - kind: other
        ref: "git ls-remote --heads origin gsd/phase-04-video-analysis -> 196b52a68b05b0880c7c8b6335b345ea378f8219, equal to git rev-parse HEAD"
        status: pass
    human_judgment: false
  - id: D2
    description: "Draft PR #5 from gsd/phase-04-video-analysis to main opened after a second explicit human confirmation"
    requirement: BUILD-05
    verification:
      - kind: other
        ref: "gh pr view --json isDraft,baseRefName,headRefName -> isDraft true, baseRefName main, headRefName gsd/phase-04-video-analysis"
        status: pass
    human_judgment: false
  - id: D3
    description: "The x64-linux leg's corpus digest listing captured verbatim from run 34776142545 (job 103774491730), with its CORPUS_DIGEST_SUMMARY recomputed over the captured lines and found equal"
    requirement: BUILD-08
    verification:
      - kind: other
        ref: "sha256sum over the 138 captured '<sha256>  <name>' lines == acb4cd4f328023f355895ef69068a9bd5a52d1196a663974ca961d1ee92a4372 (the captured CORPUS_DIGEST_SUMMARY)"
        status: pass
    human_judgment: false
  - id: D4
    description: "Cross-check of every pre-existing fixture in git show 8caf1f1:tests/golden/CORPUS_DIGEST.txt against the captured listing: clean"
    requirement: BUILD-08
    verification:
      - kind: other
        ref: "78/78 non-excluded pre-existing fixtures equal, 0 findings, 0 missing; mkv_opus_a.webm and mkv_opus_b.webm (excluded) also equal"
        status: pass
    human_judgment: false

# Metrics
duration: 5h 42m
completed: 2026-09-13
status: complete
---

# Phase 4 Plan 20: Designated-Leg Digest Capture Summary

**Branch published and draft PR #5 opened behind three separate human confirmations; the x64-linux leg's real corpus digest listing (138 fixtures) captured verbatim from CI run 34776142545, with every pre-existing fixture hash equal to main's and 44 of the 58 provisional Phase 4 hashes differing from the workstation values that 04-21 must now replace.**

## Performance

- **Duration:** 5h 42m wall clock, almost all of it waiting on the three human confirmations
- **Started:** 2026-09-13T13:19:49Z (Wave 5 opened after 04-19's last commit 196b52a)
- **Completed:** 2026-09-13T19:02:54Z
- **Tasks:** 3 (all `checkpoint:*` with `gate="blocking-human"`)
- **Files modified:** 0 source files; this SUMMARY plus the planning metadata files

## Accomplishments

- `gsd/phase-04-video-analysis` pushed to `origin` at `196b52a` (104 commits ahead of `main` = `origin/main` = `8caf1f1`), after the human said "push".
- Draft PR #5 (https://github.com/dkastsenich/mediadiff/pull/5) opened against `main`, after the human said "open the draft PR". It triggered CI run 34776142545, the only run this branch has (the workflow triggers only on `push` to `main` and `pull_request` against `main`, so the push itself started nothing).
- The x64-linux leg reached "Report corpus digest (cross-platform byte-identity evidence)" (step 11, success) and then failed at "Assert the corpus digest matches the committed pin (D-GAP-01)" (step 12) exactly as the plan predicted, before vcpkg bootstrap. Its listing is recorded verbatim below.
- Cross-check against `main`: clean. Every pre-existing fixture's designated-leg hash equals `main`'s.

## Task Commits

This plan creates no task commits by design: its three tasks are outward-facing actions and a capture, not file changes. The only commit is the plan-metadata commit (`docs(04-20): …`, this SUMMARY plus STATE.md/ROADMAP.md).

## Human confirmations (verbatim, in order)

| Task | Question put to the human | Verbatim answer | When |
|---|---|---|---|
| 1 (decision) | "Say A, B or C. Nothing leaves this machine until you answer" | `A` | before the push confirmation below |
| 2 (human-action) | "Push `gsd/phase-04-video-analysis` to the remote, yes or no?" | `push` | push executed 2026-09-13T17:34:11Z |
| 3 (human-action) | "Open a DRAFT pull request from `gsd/phase-04-video-analysis` to `main`, yes or no?" | `open the draft PR` | PR created 2026-09-13T18:56:05Z |

No commit, push or pull request happened before the corresponding answer. `workflow.auto_advance` was active for the phase and was not applied to any of these three checkpoints.

## Task 2: pre-push state and verification

Shown to the human before asking:

- `git status --porcelain`: no tracked changes; only the pre-existing untracked `.planning/milestone.lock` and `.planning/state.json`, which are in no commit.
- `git log --oneline 8caf1f1..HEAD`: 104 commits (34 gap-closure commits for 04-13 through 04-19, 70 earlier Phase 4 commits).
- `ctest --preset x64-linux --output-on-failure`: `100% tests passed, 0 tests failed out of 771` (the by-design skips still skipped).
- `bash scripts/lint_corpus_digest_provenance.sh`: exit 0, all four clauses OK (80/80 pre-existing lines present verbatim).
- `bash scripts/lint_pragma_scope.sh`: exit 0, self-test fired, 7 files clean.
- `bash scripts/assert_corpus_digest.sh`: exit 1, EXPECTED on this workstation — that failure is 04-13's restore working (the committed file carries CI-runner hashes; this machine's ffmpeg encodes different bytes), not a defect.

Verification after `git push -u origin gsd/phase-04-video-analysis`:

```text
local HEAD : 196b52a68b05b0880c7c8b6335b345ea378f8219
remote ref : 196b52a68b05b0880c7c8b6335b345ea378f8219	refs/heads/gsd/phase-04-video-analysis
upstream   : origin/gsd/phase-04-video-analysis
```

## Task 3: draft PR and run verification

```text
gh pr view --json number,url,isDraft,baseRefName,headRefName,state,headRefOid
{"baseRefName":"main","headRefName":"gsd/phase-04-video-analysis","headRefOid":"196b52a68b05b0880c7c8b6335b345ea378f8219","isDraft":true,"number":5,"state":"OPEN","url":"https://github.com/dkastsenich/mediadiff/pull/5"}
```

Run: 34776142545 (event `pull_request`, head sha `196b52a68b05b0880c7c8b6335b345ea378f8219`, https://github.com/dkastsenich/mediadiff/actions/runs/34776142545). Job `build (x64-linux)` = 103774491730. Step outcomes on that job: 1-2 success, 8 "Install the pinned ffmpeg build" success, 9 "Generate media fixture corpus" success, 10 "Verify the fixture corpus is complete" success, 11 "Report corpus digest" success, 12 "Assert the corpus digest" **failure**, 13-26 skipped (vcpkg bootstrap, Configure, Build, Test never ran on this leg, as the plan said they would not).

Logs were pulled with `gh api --allow-escape-sequences repos/dkastsenich/mediadiff/actions/jobs/103774491730/logs` (the `gh run view --log` form refuses while other legs are still running) and ANSI escapes stripped; timestamps stripped from the lines below. Nothing else was altered.

## Captured x64-linux listing (VERBATIM — 04-21 transcribes from this block)

Source: run 34776142545, job 103774491730 `build (x64-linux)`, step 11 "Report corpus digest (cross-platform byte-identity evidence)", commit `196b52a68b05b0880c7c8b6335b345ea378f8219`. 138 fixture lines + 1 summary line. Integrity: `sha256sum` over the 138 fixture lines (with their trailing newlines, as `scripts/corpus_digest.sh` computes it) = `acb4cd4f328023f355895ef69068a9bd5a52d1196a663974ca961d1ee92a4372` = the captured `CORPUS_DIGEST_SUMMARY`, so the capture is complete and untruncated.

```text
2bf9b1aa7b2549e12c5fa2ed4d4c48c5d3e95f32e147e645cd18e70d650ea0e4  .topo_chapters.ffmeta
bc6ea3690b3cd541353646370ba6906c65881498f000b4c50899151d68647775  .topo_subs.srt
f8cb3c45419790666076b37f02e150b2c06c6eab906afb5fb4513f5df8d6c38c  .video_ilace_mixed_raw.m2v
7e6554161b7e3eaef6cf476fc221b016a47954c9676cbcd3239a83aea85e6691  .video_ilace_seg_a.m2v
19663658555feacbe389a8b2238ca284498199ab68133039e7157a414e12c6e2  .video_ilace_seg_b.m2v
789e37c2685f72e387c40f208847d2eed78cd4e2e0ec93a47bb19f1edbe3a4b2  idem_a.mp4
789e37c2685f72e387c40f208847d2eed78cd4e2e0ec93a47bb19f1edbe3a4b2  idem_b.mp4
3e2a48423fe552751dafb33a9225920a139a5e5ca40b3743834963c7815cc254  lang_absent.mp4
6eff884e1c34c4153aa1f4d9bdbd167d0674941308f9291f547d7c8147907b76  lang_eng.mp4
86fab80f1705bb1b77f9c4b644a52991735007145ed7e3a8c8a6ff4d7e16f000  lang_fra.mp4
3e2a48423fe552751dafb33a9225920a139a5e5ca40b3743834963c7815cc254  lang_und.mp4
373e176e61edfb437034021ef58494770460cfc893bb9eaaa2ef468db5f211af  mkv_cues_end.mkv
4ac579046a6d13efdd8e236f7cf2c728ed9778a9a54ba44d0ca6378a6799227b  mkv_cues_front.mkv
4ac579046a6d13efdd8e236f7cf2c728ed9778a9a54ba44d0ca6378a6799227b  mkv_cues_front_copy.mkv
e75572498363f354d3897a1381fdbaf518d2a3da4c031aba3ec9a7ec7721b44c  mkv_noduration.mkv
c679a443bb35787c9ed1dccf5de1dba5f435e0ab96835f3b991f8f81944e7f6a  mkv_noopus.mkv
3658af3f7708a083f6828503819cca9c0e03e262567cfe5cb9d2d499eb595a06  mkv_opus_a.webm
afdf14186aeb866fc9e39675c0ecf554723b22f0d8b659c4f0543de0ecb071aa  mkv_opus_b.webm
373e176e61edfb437034021ef58494770460cfc893bb9eaaa2ef468db5f211af  mkv_tscale_a.mkv
0717b7976bb99a7302f83a291b6cf6d98f3eeb2906e877f581a43ee22f258c97  mkv_tscale_b.mkv
7d1c880370b06b226b65eadadfeaf67a195855e3cd82bf4dfb85de32d0082953  mp4_editdelay.mp4
7fd8f437bb089183a5cae2fef4d3962a291b5aaac160ce4e4f94d47e952a8aa4  mp4_edittrim.mp4
fe75c2dfcd6c3870bb7620fbee07ca793e97ffdb71db70e3527f554ef2e62090  mp4_faststart.mp4
fe75c2dfcd6c3870bb7620fbee07ca793e97ffdb71db70e3527f554ef2e62090  mp4_faststart_copy.mp4
c97c8bae9602899b2df04ae025789529e1c71a7d5ba149b979432ba11e369f06  mp4_fragmented.mp4
49b5ebb5801c7431032f6e5991b0caa5cd8514c3e99833b67c86711b89976b6f  mp4_fragmented_close.mp4
8ad87376b63fa22f123e38d0afa1249e3b6a0299b7a88b668c5f25b99cadedd9  mp4_fragmented_far.mp4
3e2a48423fe552751dafb33a9225920a139a5e5ca40b3743834963c7815cc254  mp4_nofaststart.mp4
3e2a48423fe552751dafb33a9225920a139a5e5ca40b3743834963c7815cc254  mp4_ts_a.mp4
40bda3b8de6f0bf52f6fbd14a062723ae595e481ee8fe1be38b64e56d06f21f9  mp4_ts_b.mp4
d78a1184111b57f0034dadf69eb513676910e4828ef935c5ca84fcdf9f329334  size_bitrate_a.mp4
3c8a49b95937972d552aac68703ed441b93ea533a7c927d4f0943544db791ff3  size_bitrate_b.mp4
3c8a49b95937972d552aac68703ed441b93ea533a7c927d4f0943544db791ff3  size_crf20.mp4
3c8a49b95937972d552aac68703ed441b93ea533a7c927d4f0943544db791ff3  size_crf20_copy.mp4
eb65ae0b2608698417a85604a67cf1579c683dacd805a0fc94bf749ac52d2131  size_crf23.mp4
6099498d22dff79f6fbc1bc23f277442cbf7faf22e460f9c4563ae8b63d192cf  size_muxrate_a.ts
7ef7ad7f53247f7ef03ce2196782d8b147354f8c8d1234a6b60a4125b4969551  size_muxrate_b.ts
df39b7b9c0a27ef2439c1d03d5cb163a65a2366a5474f5f3fe4f31226ffbc06c  size_near_a.mp4
ed780ca3310d4f4ba0e8ad62cc5ef30484fba4b838efa120086f2842fc7b2507  size_near_b.mp4
d22ccb8d2dd3d4f08ea89764c2c107517a790b29348d16d8ed4da4cc274c36ba  size_partial.mp4
3714f151c584afa5aa51dd7bc342b3163473900e36a940bd41f3287412a15e9f  size_peak_singlepass.mp4
cdb3dc56635dfe6dc9b0cfbccc37f4785772dd4fe2d6b29de2f85c913a745a60  size_peak_vbv.mp4
38ee5f962281b7b7d39df9e565eb89dff0903946fb6a9a08fc650cc8b4d130c0  size_short.mp4
dfa051eb70b95324fab7fbeb3ce882822f6a7c60bc9672f3e0d946f152b045d4  tags_esc_a.mp4
d8d9d8b9cb776292bd8a9a0bf2aafe69ea6b4f1aaa1cba8142fd7082c4bd2f8e  tags_esc_b.mp4
5205d37505a7656f0b205b9b2c7e62c82f8703aa774e6aaab1a805ad15a200de  tags_stream_title_a.mp4
c0ab42b97a7137bfab2c8161713b481265188dd079f9fd6c94a40c527ae04599  tags_stream_title_b.mp4
15d253460a7e1c6145fdc377aa93ed2c12040667b4fd259f7e75c0c719af8589  tags_title_a.mp4
7a42122780b4c8a73456112e753ece3e786edd170cbcc00897f6c0b93bad468b  tags_title_b.mp4
87f62b0f74c45d86e5d9d8bd57c6a4a740020e2409801141c94d2d86c3891132  tags_volatile_a.mp4
9d66186a9d81a83471163d6596012f795c66bf2cbbcedac1ef8a3dbfcfd7a1b2  tags_volatile_b.mp4
cde5f98e17c2747950ead30349e2a96f83ce57318b43e3672c1a830840ca0cd2  topo_chapters.mkv
373e176e61edfb437034021ef58494770460cfc893bb9eaaa2ef468db5f211af  topo_nochapters.mkv
3e2a48423fe552751dafb33a9225920a139a5e5ca40b3743834963c7815cc254  topo_nosubs.mp4
3e2a48423fe552751dafb33a9225920a139a5e5ca40b3743834963c7815cc254  topo_notmcd.mp4
8ae251f4e07cb0d003cb34baf5c3723ef9472966b577c8825fcce52c1638e119  topo_order_a.mp4
a821734bdd1cef0df33c927de8259f48fce3c581ffe9417c2772ddb39b8aabab  topo_order_b.mp4
c6b14560e3fb9ad46f8319f794c91c034fe0605c73cae84347028db1fcad0f23  topo_subs.mp4
c6b14560e3fb9ad46f8319f794c91c034fe0605c73cae84347028db1fcad0f23  topo_subs_copy.mp4
41eda409dc08446e171f95018dead3fb09c60bbe1a5c4d8f44ace491cac59f26  topo_tmcd.mp4
ece0d8dbfc7b9c79cd97eebe56c54f996dcafb211921a74113fbbd31ea5092dd  topo_ts.ts
3e2a48423fe552751dafb33a9225920a139a5e5ca40b3743834963c7815cc254  topo_type_order_a.mp4
4589ae4e5d0ff2808558e8d9154eb422f5d04065772598538fe1a09e223962e3  topo_type_order_b.mp4
373e176e61edfb437034021ef58494770460cfc893bb9eaaa2ef468db5f211af  tracer_a.mkv
3e2a48423fe552751dafb33a9225920a139a5e5ca40b3743834963c7815cc254  tracer_a.mp4
3e2a48423fe552751dafb33a9225920a139a5e5ca40b3743834963c7815cc254  tracer_a_copy.mp4
e353dc9c179ac03137f75ee80833b4b7fd528822204f9cb73e28a963b6f66af2  tracer_empty.mp4
3b9aa88c834c96c0c99acee4be6340d92b2bdf0c5c2d38c4232895c1b7912678  ts_192.ts
cdb8d023dc06ed23725e4074ad42478d99c7663f810156c4a7b8b79405e73615  ts_204.ts
1d34a7b8c81f7a3ddcca83e22b7a75bf8eb1ed44075c18ae998eaf922543a741  ts_ccgap.ts
3d30b4e1d84fe658c94dc6895d45b7fad1711600bb1410622ce2c2a1ff46865c  ts_discontinuity.ts
2d5002e79231c9e1bae47d8fba7730ad50b1a6b423e7eec91c32b88761c4ea01  ts_multiprogram.ts
6c313054b1792ab9c000956d747ab85bfb98efa2fe3267b7ca118b867bfd2c92  ts_multiprogram_renumbered.ts
222512d37611bb864abcd2d8da85781a7ef1f1fc95abad39bed8dfdbb252b871  ts_multiprogram_reordered.ts
6099498d22dff79f6fbc1bc23f277442cbf7faf22e460f9c4563ae8b63d192cf  ts_nullratio_a.ts
7ef7ad7f53247f7ef03ce2196782d8b147354f8c8d1234a6b60a4125b4969551  ts_nullratio_b.ts
3d8d09b39270c789746c5073a1d44499c1ef588ea7ae19ac951c0219393acbd7  ts_pcr_close_a.ts
b4d115d5cb8132c0bf2355362a294747154f670df454537b4776792c3c3f2740  ts_pcr_close_b.ts
3d8d09b39270c789746c5073a1d44499c1ef588ea7ae19ac951c0219393acbd7  ts_pcr_far_a.ts
b81e422ccad6b499462bc304b37814b46f344a22f4fb2d399775dd2a06f73653  ts_pcr_far_b.ts
ece0d8dbfc7b9c79cd97eebe56c54f996dcafb211921a74113fbbd31ea5092dd  ts_single.ts
ece0d8dbfc7b9c79cd97eebe56c54f996dcafb211921a74113fbbd31ea5092dd  ts_single_copy.ts
a6e7eedd4ed6e7c15c29900b7c8916132ad1f59595dc9282b30369a51b084176  ts_single_pcr.ts
25a8db74fedcc8aca1c2f56c6f9cd0129ac45eac537d2325f9cd6c015c0d336b  video_base.mp4
25a8db74fedcc8aca1c2f56c6f9cd0129ac45eac537d2325f9cd6c015c0d336b  video_base_copy.mp4
f634d3e5331ad447b416a8e6e0fad3f040f64b9f5048e217c8614de8ce9230a9  video_bf3.mp4
f4215e5ac30c487a5097070e5113ddb24e2c51ad096dd00ece3c6d59bd56a186  video_chroma_center.mkv
d4d9856cb400c18faa222817182390223df087ba411ba7511d2ad42fba2eb0ab  video_chroma_left.mkv
64317df5b8ee6f492ad7b07e0abdc3460a39f4801d40ccef858020bf9bf3ff1f  video_codec_mpeg2.mp4
ecca2ca384869220b223398bc4604da52d4dffdf06c8415d165e685044b14e0d  video_color_bt601.mp4
eeb8f1a36ccba789d61ba253aaa221456f4aab6ba6fd14c557689bcdca9d5faf  video_color_bt709.mp4
eeb8f1a36ccba789d61ba253aaa221456f4aab6ba6fd14c557689bcdca9d5faf  video_color_bt709_copy.mp4
9165b44f5b044bd086dc6fed996b14e7624da97a386e360811a861be5dcf11e6  video_color_unspec.mp4
781897e5573e4cd0ce32ce74b98f7fa00ee9683bde2c2ed560f12400004bdf8c  video_dovi_a.mp4
781897e5573e4cd0ce32ce74b98f7fa00ee9683bde2c2ed560f12400004bdf8c  video_dovi_a_copy.mp4
fd44aafd11201e8234686c3f5a2712ec9f4c217ae7b8c90ac5610c8f83a8eed5  video_dovi_b.mp4
455960e0a375ea4c803bc45af5cd26ed69cb68db3fe5ea70073d2392c078358e  video_fps_30.mp4
bdba25ceb13bd761162ec6396083e4dc4d33c67d65891fdddbf527964304c426  video_frames_50.mp4
25a8db74fedcc8aca1c2f56c6f9cd0129ac45eac537d2325f9cd6c015c0d336b  video_gop_g48.mp4
25a8db74fedcc8aca1c2f56c6f9cd0129ac45eac537d2325f9cd6c015c0d336b  video_gop_g48_copy.mp4
3927cf91cf9d8337143651eadc5d36d99b803a68bbff185bae59469d81010a95  video_gop_g96.mp4
d71bace33d6e6e2cd71cecd35ee68b3b854a9eb99719cdeba712cb5d251bffc7  video_h264_closed.h264
d71bace33d6e6e2cd71cecd35ee68b3b854a9eb99719cdeba712cb5d251bffc7  video_h264_closed_copy.h264
652ec88d1cf14429853cc0b18688fee92e3e99752ffc3fd42da7f4a7fdd58b17  video_h264_idr48.h264
6330a5d9e78a6fa6e222ca5d6d8b7d10e6f5b348cb83d660deb4ba88e801f7d7  video_h264_open.h264
d71bace33d6e6e2cd71cecd35ee68b3b854a9eb99719cdeba712cb5d251bffc7  video_h264_refs1.h264
cde9424b79d8e4d8a59fe0f4cd9749e87491db634e098500c03fc4a536a869ca  video_h264_refs4.h264
288ccc292faa4bdc90a503281d5a25599adac879a180646a33d9e4a456580810  video_hdr_a.mp4
288ccc292faa4bdc90a503281d5a25599adac879a180646a33d9e4a456580810  video_hdr_a_copy.mp4
0b338c352948ebc595693b58190df8279507b49e66b37e8d48e4b5d239c5d9a9  video_hdr_cll_b.mp4
21e55f8f574cb18e8d625d0a13e4a7c0efe569ed3a379a288401645a94563580  video_hdr_coherent.mp4
21e55f8f574cb18e8d625d0a13e4a7c0efe569ed3a379a288401645a94563580  video_hdr_coherent_copy.mp4
22539f519c85eb7f7fd5c4e13f94453ab61817add5775aee9e9ed2fb30c7c217  video_hdr_hlg_nomdcv.mp4
f85f8f5f158a0c777656dd1dc65c8d959f1a79b2eff0dec473b9e4f0883e5e7c  video_hdr_lum_b.mp4
50dd67016b4df30ba6606f9aa28046467640354f9a6de3882a4fd4de2c4e0a4f  video_hdr_none.mp4
5a88998ebadcfc4c5d7b822b4836883d80ec36a971e9370753e8a774a8ac9eb3  video_hdr_pq_nomdcv.mp4
6a14f1c42e9b4357b2b95649f9720396051db0c90dacf7c4aca735521b3f5217  video_hdr_prim_b.mp4
a6ac9db285e4181c0bc17084443e68fcfafc0a1dc8e9ba7a399a988b3da0693f  video_hdr_sdr_mdcv.mp4
a6ac9db285e4181c0bc17084443e68fcfafc0a1dc8e9ba7a399a988b3da0693f  video_hdr_sdr_mdcv_copy.mp4
8fd28ad9dcac9d316b82220805a5abbdf18e1a858d3a20e36b7f3fd85495fb62  video_hevc_cra.hevc
e78bee74a332b2ade9f025b19c74f3bb82f3036fea9817121db191cc9edde4e8  video_hevc_idr.hevc
cfebdd8e9c3d2cd2130e5e5f10679b9424843dc941206aeb1fda31322476cdc8  video_ilace_bff.mp4
618c3c61a29828798458564355f130476379efad51d56a8bfd39ffe3ac825534  video_ilace_mixed.mp4
c04e6c5114ef85d8af519c1b1666e1826fd56c131004ff7054698bfea73b5299  video_ilace_tff.mp4
c04e6c5114ef85d8af519c1b1666e1826fd56c131004ff7054698bfea73b5299  video_ilace_tff_copy.mp4
ece4fa578e5d2a6712ab936a239d39b8633c01673abd9990b711e278a9c15e14  video_noparser.mkv
ece4fa578e5d2a6712ab936a239d39b8633c01673abd9990b711e278a9c15e14  video_noparser_copy.mkv
129fb3d4d6c06045c27cb1246b5e71f4b37cc92b050e808bddcbb659dd2009ce  video_prof_a.mp4
a32c898d4078d8f29e4d2d73655142ec38efa5ec56c2adbb38a1f73794007945  video_prof_b.mp4
e88ef9e1bb4e4247fb929ef3d7b86e9de0aaa1f3fe82a677ba6a6dd64608ad0f  video_range_pc.mp4
da97d5e8102c64e1de69ba68340d25f20e83ab2b87cc060a1c8060bcdc0e8309  video_res_640.mp4
937da2d337e5907f006e2838545aea548b1957a9731221b11f9e5b2652f78be5  video_sar_4_3.mp4
ea59cd76cc7c46ee9aa9aa3ed7ee5f7edaa9a3e71a6f0e2178a95300d56c56cd  video_sar_conflict.mp4
44d0bbd13079141a064c5aed8537a5fa75b48e5c83cdd39ec561a25b341dbbbf  video_vfr.mp4
f9d92aff10ff030a4f1bf742d188f53bedcade5a8087a407bb71a2df941c4508  video_yuv420p_pc.mp4
f700341b108d43bdb335c8a2ed1e8ca0918f3abe6c1c3d4e54d12358e06cc587  video_yuv420p_pc_tagged.mp4
548d74b3321922fa70aec56821baaea53e5e5988638a73498eb613c4d12516b1  video_yuv420p_tv.mp4
f9d92aff10ff030a4f1bf742d188f53bedcade5a8087a407bb71a2df941c4508  video_yuvj420p.mp4
CORPUS_DIGEST_SUMMARY=acb4cd4f328023f355895ef69068a9bd5a52d1196a663974ca961d1ee92a4372
```

## Digest-assertion failure output (step 12, `build (x64-linux)`)

Terminal lines of the step:

```text
##[error]tests/golden/CORPUS_DIGEST.txt no longer matches on the 136 non-excluded line(s) (diff printed above). This is a hard failure under D-GAP-01 -- CI never rewrites this file.
##[error]Process completed with exit code 1.
```

The diff the step printed has 44 `-` lines (committed, workstation-derived) and 44 `+` lines (computed on the designated leg); every one of the 44 names is in `tests/golden/CORPUS_DIGEST_PROVISIONAL.txt`. Full step output, verbatim apart from stripped timestamps:

<details>
<summary>Step 12 output (133 lines)</summary>

```text
##[group]Run DESIGNATED_LEG="x64-linux"
DESIGNATED_LEG="x64-linux"
if [ "x64-linux" = "$DESIGNATED_LEG" ]; then
  bash scripts/assert_corpus_digest.sh
else
  echo "This leg (x64-linux) is NOT the designated leg ($DESIGNATED_LEG) for tests/golden/CORPUS_DIGEST.txt (03-19: the pinned ffmpeg builds do not produce byte-identical fixtures across platforms -- see 03-19-SUMMARY.md). This leg's corpus is deliberately NOT asserted against the committed digest."
fi
shell: /usr/bin/bash --noprofile --norc -e -o pipefail {0}
env:
  pythonLocation: /opt/hostedtoolcache/Python/3.11.16/x64
  PKG_CONFIG_PATH: /opt/hostedtoolcache/Python/3.11.16/x64/lib/pkgconfig
  Python_ROOT_DIR: /opt/hostedtoolcache/Python/3.11.16/x64
  Python2_ROOT_DIR: /opt/hostedtoolcache/Python/3.11.16/x64
  Python3_ROOT_DIR: /opt/hostedtoolcache/Python/3.11.16/x64
  LD_LIBRARY_PATH: /opt/hostedtoolcache/Python/3.11.16/x64/lib
  MEDIADIFF_FFMPEG: /home/runner/work/mediadiff/mediadiff/.ffmpeg-pinned/linux-x86_64/ffmpeg
##[endgroup]
assert_corpus_digest.sh: self-test OK -- known-good (Opus+summary-only diff) passed, non-vacuity control (non-Opus diff) failed, count-guard control (excluded count != 3) failed.
--- /tmp/tmp.jzk3qf5nxg	2026-09-13 18:56:45.928472982 +0000
+++ /tmp/tmp.vf8SUZr7pG	2026-09-13 18:56:45.929291810 +0000
@@ -1,8 +1,8 @@
 2bf9b1aa7b2549e12c5fa2ed4d4c48c5d3e95f32e147e645cd18e70d650ea0e4  .topo_chapters.ffmeta
 bc6ea3690b3cd541353646370ba6906c65881498f000b4c50899151d68647775  .topo_subs.srt
-cf0c7a0adc3cea714e9447414e80e3b37162f8e9f104cbed1da469486cb26247  .video_ilace_mixed_raw.m2v
-23d782f1a6e30b034e11c99a71d462760e6335f51ca8bb43e714460e3343052f  .video_ilace_seg_a.m2v
-8c9c18005e3f700da3d1f47c383032deeb2fbcffef91f0c6a806ea64e5407bfb  .video_ilace_seg_b.m2v
+f8cb3c45419790666076b37f02e150b2c06c6eab906afb5fb4513f5df8d6c38c  .video_ilace_mixed_raw.m2v
+7e6554161b7e3eaef6cf476fc221b016a47954c9676cbcd3239a83aea85e6691  .video_ilace_seg_a.m2v
+19663658555feacbe389a8b2238ca284498199ab68133039e7157a414e12c6e2  .video_ilace_seg_b.m2v
 789e37c2685f72e387c40f208847d2eed78cd4e2e0ec93a47bb19f1edbe3a4b2  idem_a.mp4
 789e37c2685f72e387c40f208847d2eed78cd4e2e0ec93a47bb19f1edbe3a4b2  idem_b.mp4
 3e2a48423fe552751dafb33a9225920a139a5e5ca40b3743834963c7815cc254  lang_absent.mp4
@@ -79,57 +79,57 @@
 ece0d8dbfc7b9c79cd97eebe56c54f996dcafb211921a74113fbbd31ea5092dd  ts_single.ts
 ece0d8dbfc7b9c79cd97eebe56c54f996dcafb211921a74113fbbd31ea5092dd  ts_single_copy.ts
 a6e7eedd4ed6e7c15c29900b7c8916132ad1f59595dc9282b30369a51b084176  ts_single_pcr.ts
-4e69daa047343c6698e51bf5a1a2a8b3b86f4e947f2edda3ebef409d26c7d523  video_base.mp4
-4e69daa047343c6698e51bf5a1a2a8b3b86f4e947f2edda3ebef409d26c7d523  video_base_copy.mp4
-bea82ca8486e0ae74040bdeee673bc9bc7b6e4f264e7808e60851c814e2bb0e9  video_bf3.mp4
-bbd5304c6d01ae609246b69168997cfc76971b5afdb6ac2eb307b38c338bf82f  video_chroma_center.mkv
-f03de98d58e142f60b4e722b8d63f2bb04e8832f744722342bbfa5010bd5fc98  video_chroma_left.mkv
-7f6f844a66247765edf1720c1fe6cc04adbfad001107b0fa1c8aa9bdb275fe5c  video_codec_mpeg2.mp4
-642113161409de1840e57294b690ca52c80be2e0215f664e1d377470db43b195  video_color_bt601.mp4
-c96bbcc36fdadecbd0dbe35d5963d0678fec8f221a105850db43cc22fe6d3dc7  video_color_bt709.mp4
-c96bbcc36fdadecbd0dbe35d5963d0678fec8f221a105850db43cc22fe6d3dc7  video_color_bt709_copy.mp4
-4757c9957b0615d785c7dcbfb7c5234f00b688807f8b0cc32564a2db70bd50b4  video_color_unspec.mp4
-fd11e5c156268461c01194c060566c1a9d9329da53476f8c99721e97919eb66f  video_dovi_a.mp4
-fd11e5c156268461c01194c060566c1a9d9329da53476f8c99721e97919eb66f  video_dovi_a_copy.mp4
-a088661a22c8300f9266a121f9061f756f9637b605b4595c71fb2b1a9dd44d86  video_dovi_b.mp4
-fb68f8e97ddf25ebc4aaefd8607cd1abe180dbd81cbbc513144f7c5bc25c43c8  video_fps_30.mp4
-5ebbb766f5d1b86d006bc27bd7db77c6cc39b31809104980d9b1965f2395f9d3  video_frames_50.mp4
-4e69daa047343c6698e51bf5a1a2a8b3b86f4e947f2edda3ebef409d26c7d523  video_gop_g48.mp4
-4e69daa047343c6698e51bf5a1a2a8b3b86f4e947f2edda3ebef409d26c7d523  video_gop_g48_copy.mp4
-be6008e65881d6d4d773c3ba0896b8c9bf1a51439f49b57c41b7b7f8759079db  video_gop_g96.mp4
+25a8db74fedcc8aca1c2f56c6f9cd0129ac45eac537d2325f9cd6c015c0d336b  video_base.mp4
+25a8db74fedcc8aca1c2f56c6f9cd0129ac45eac537d2325f9cd6c015c0d336b  video_base_copy.mp4
+f634d3e5331ad447b416a8e6e0fad3f040f64b9f5048e217c8614de8ce9230a9  video_bf3.mp4
+f4215e5ac30c487a5097070e5113ddb24e2c51ad096dd00ece3c6d59bd56a186  video_chroma_center.mkv
+d4d9856cb400c18faa222817182390223df087ba411ba7511d2ad42fba2eb0ab  video_chroma_left.mkv
+64317df5b8ee6f492ad7b07e0abdc3460a39f4801d40ccef858020bf9bf3ff1f  video_codec_mpeg2.mp4
+ecca2ca384869220b223398bc4604da52d4dffdf06c8415d165e685044b14e0d  video_color_bt601.mp4
+eeb8f1a36ccba789d61ba253aaa221456f4aab6ba6fd14c557689bcdca9d5faf  video_color_bt709.mp4
+eeb8f1a36ccba789d61ba253aaa221456f4aab6ba6fd14c557689bcdca9d5faf  video_color_bt709_copy.mp4
+9165b44f5b044bd086dc6fed996b14e7624da97a386e360811a861be5dcf11e6  video_color_unspec.mp4
+781897e5573e4cd0ce32ce74b98f7fa00ee9683bde2c2ed560f12400004bdf8c  video_dovi_a.mp4
+781897e5573e4cd0ce32ce74b98f7fa00ee9683bde2c2ed560f12400004bdf8c  video_dovi_a_copy.mp4
+fd44aafd11201e8234686c3f5a2712ec9f4c217ae7b8c90ac5610c8f83a8eed5  video_dovi_b.mp4
+455960e0a375ea4c803bc45af5cd26ed69cb68db3fe5ea70073d2392c078358e  video_fps_30.mp4
+bdba25ceb13bd761162ec6396083e4dc4d33c67d65891fdddbf527964304c426  video_frames_50.mp4
+25a8db74fedcc8aca1c2f56c6f9cd0129ac45eac537d2325f9cd6c015c0d336b  video_gop_g48.mp4
+25a8db74fedcc8aca1c2f56c6f9cd0129ac45eac537d2325f9cd6c015c0d336b  video_gop_g48_copy.mp4
+3927cf91cf9d8337143651eadc5d36d99b803a68bbff185bae59469d81010a95  video_gop_g96.mp4
 d71bace33d6e6e2cd71cecd35ee68b3b854a9eb99719cdeba712cb5d251bffc7  video_h264_closed.h264
 d71bace33d6e6e2cd71cecd35ee68b3b854a9eb99719cdeba712cb5d251bffc7  video_h264_closed_copy.h264
 652ec88d1cf14429853cc0b18688fee92e3e99752ffc3fd42da7f4a7fdd58b17  video_h264_idr48.h264
 6330a5d9e78a6fa6e222ca5d6d8b7d10e6f5b348cb83d660deb4ba88e801f7d7  video_h264_open.h264
 d71bace33d6e6e2cd71cecd35ee68b3b854a9eb99719cdeba712cb5d251bffc7  video_h264_refs1.h264
 cde9424b79d8e4d8a59fe0f4cd9749e87491db634e098500c03fc4a536a869ca  video_h264_refs4.h264
-cc885588900532ed41c1d0c7bebaaf9df6468c385d15e2377f9103915e98da71  video_hdr_a.mp4
-cc885588900532ed41c1d0c7bebaaf9df6468c385d15e2377f9103915e98da71  video_hdr_a_copy.mp4
-5f1149d45742b525b5a46a524b3460866df780e091630ebbff928ce877a77aef  video_hdr_cll_b.mp4
-58adc1c3e1eb41a2dce384db4ec23015e34f40b60219219859508d918ec2ed0b  video_hdr_coherent.mp4
-58adc1c3e1eb41a2dce384db4ec23015e34f40b60219219859508d918ec2ed0b  video_hdr_coherent_copy.mp4
-0d0c312fd1508e3fc5be46a9240405ef03bc59bb759f01dd1aeaa82843bed7b7  video_hdr_hlg_nomdcv.mp4
-2114ae1877b000c1bef2648ff4caae3939a95a6f948e49d65a957c6965d03561  video_hdr_lum_b.mp4
-4f6bd658d2e7f5db28ad2442efd1a19d1969fce53f29c5bf7ae84825069e8e62  video_hdr_none.mp4
-bd9e74aa604d28530a54fcec2b946cac321e38ebaaf64fb458f43f1162c5ce97  video_hdr_pq_nomdcv.mp4
-726f545db61a2c31baf49a862f747328b92c9fa678bfae9adc0f8ed507d11ebd  video_hdr_prim_b.mp4
-994a2a83830e5c82559829929709494f6e36dddb389e704a4d0c7b182556e3be  video_hdr_sdr_mdcv.mp4
-994a2a83830e5c82559829929709494f6e36dddb389e704a4d0c7b182556e3be  video_hdr_sdr_mdcv_copy.mp4
+288ccc292faa4bdc90a503281d5a25599adac879a180646a33d9e4a456580810  video_hdr_a.mp4
+288ccc292faa4bdc90a503281d5a25599adac879a180646a33d9e4a456580810  video_hdr_a_copy.mp4
+0b338c352948ebc595693b58190df8279507b49e66b37e8d48e4b5d239c5d9a9  video_hdr_cll_b.mp4
+21e55f8f574cb18e8d625d0a13e4a7c0efe569ed3a379a288401645a94563580  video_hdr_coherent.mp4
+21e55f8f574cb18e8d625d0a13e4a7c0efe569ed3a379a288401645a94563580  video_hdr_coherent_copy.mp4
+22539f519c85eb7f7fd5c4e13f94453ab61817add5775aee9e9ed2fb30c7c217  video_hdr_hlg_nomdcv.mp4
+f85f8f5f158a0c777656dd1dc65c8d959f1a79b2eff0dec473b9e4f0883e5e7c  video_hdr_lum_b.mp4
+50dd67016b4df30ba6606f9aa28046467640354f9a6de3882a4fd4de2c4e0a4f  video_hdr_none.mp4
+5a88998ebadcfc4c5d7b822b4836883d80ec36a971e9370753e8a774a8ac9eb3  video_hdr_pq_nomdcv.mp4
+6a14f1c42e9b4357b2b95649f9720396051db0c90dacf7c4aca735521b3f5217  video_hdr_prim_b.mp4
+a6ac9db285e4181c0bc17084443e68fcfafc0a1dc8e9ba7a399a988b3da0693f  video_hdr_sdr_mdcv.mp4
+a6ac9db285e4181c0bc17084443e68fcfafc0a1dc8e9ba7a399a988b3da0693f  video_hdr_sdr_mdcv_copy.mp4
 8fd28ad9dcac9d316b82220805a5abbdf18e1a858d3a20e36b7f3fd85495fb62  video_hevc_cra.hevc
 e78bee74a332b2ade9f025b19c74f3bb82f3036fea9817121db191cc9edde4e8  video_hevc_idr.hevc
-e538b59b18209e21dcc3f9f473a87a461e23b9dc7045c2744bc088babcac4d35  video_ilace_bff.mp4
-e5c76ac60bc244718ef388736f4b10221e2cfc25e5d437d83824834dd13f3f1d  video_ilace_mixed.mp4
-bc3740d9b7911dad97e1744d707ac4d41476a3589f4806762ee6a1d1283a898e  video_ilace_tff.mp4
-bc3740d9b7911dad97e1744d707ac4d41476a3589f4806762ee6a1d1283a898e  video_ilace_tff_copy.mp4
+cfebdd8e9c3d2cd2130e5e5f10679b9424843dc941206aeb1fda31322476cdc8  video_ilace_bff.mp4
+618c3c61a29828798458564355f130476379efad51d56a8bfd39ffe3ac825534  video_ilace_mixed.mp4
+c04e6c5114ef85d8af519c1b1666e1826fd56c131004ff7054698bfea73b5299  video_ilace_tff.mp4
+c04e6c5114ef85d8af519c1b1666e1826fd56c131004ff7054698bfea73b5299  video_ilace_tff_copy.mp4
 ece4fa578e5d2a6712ab936a239d39b8633c01673abd9990b711e278a9c15e14  video_noparser.mkv
 ece4fa578e5d2a6712ab936a239d39b8633c01673abd9990b711e278a9c15e14  video_noparser_copy.mkv
-29f1646bc1a23d41901e14da9007a41b5176f55d64258f067f6a490c01360e2b  video_prof_a.mp4
-2fb32b2f88fde08aaae8a5e3ba9be60d937dc5dbf36877aa0b983145e2525592  video_prof_b.mp4
-32dc595aba402d09fe4754c0031d4bb80ad3299f3f586edfba0506cad4c78c76  video_range_pc.mp4
-1d328d80dbb13f4db8ea0e92ffc33e167af0966d546f0fc95d70d19b888db916  video_res_640.mp4
-5adb0fc7c9833361720c35e68a632ce6c7d99dd435994e819cdf4add9ecb2302  video_sar_4_3.mp4
-178330121b76e69e7a767b5810a83b0874d84a44f6b74c057e88fb71e8a9f010  video_sar_conflict.mp4
-fb49a8d08c4d9f50875f461da0c14630c3475f812562389c9b4197f08f0b5037  video_vfr.mp4
+129fb3d4d6c06045c27cb1246b5e71f4b37cc92b050e808bddcbb659dd2009ce  video_prof_a.mp4
+a32c898d4078d8f29e4d2d73655142ec38efa5ec56c2adbb38a1f73794007945  video_prof_b.mp4
+e88ef9e1bb4e4247fb929ef3d7b86e9de0aaa1f3fe82a677ba6a6dd64608ad0f  video_range_pc.mp4
+da97d5e8102c64e1de69ba68340d25f20e83ab2b87cc060a1c8060bcdc0e8309  video_res_640.mp4
+937da2d337e5907f006e2838545aea548b1957a9731221b11f9e5b2652f78be5  video_sar_4_3.mp4
+ea59cd76cc7c46ee9aa9aa3ed7ee5f7edaa9a3e71a6f0e2178a95300d56c56cd  video_sar_conflict.mp4
+44d0bbd13079141a064c5aed8537a5fa75b48e5c83cdd39ec561a25b341dbbbf  video_vfr.mp4
 f9d92aff10ff030a4f1bf742d188f53bedcade5a8087a407bb71a2df941c4508  video_yuv420p_pc.mp4
 f700341b108d43bdb335c8a2ed1e8ca0918f3abe6c1c3d4e54d12358e06cc587  video_yuv420p_pc_tagged.mp4
 548d74b3321922fa70aec56821baaea53e5e5988638a73498eb613c4d12516b1  video_yuv420p_tv.mp4
##[error]tests/golden/CORPUS_DIGEST.txt no longer matches on the 136 non-excluded line(s) (diff printed above). This is a hard failure under D-GAP-01 -- CI never rewrites this file.
##[error]Process completed with exit code 1.
```

</details>

## Cross-check verdict (Task 3 step 7)

Compared every fixture name in `git show 8caf1f1:tests/golden/CORPUS_DIGEST.txt` against the captured listing, excluding `mkv_opus_a.webm`, `mkv_opus_b.webm` and the `CORPUS_DIGEST_SUMMARY=` line as `scripts/assert_corpus_digest.sh` does:

| Population | Result |
|---|---|
| Pre-existing, non-excluded (78) | 78 equal, 0 divergent, 0 missing from the CI listing |
| Excluded Opus pair (2) | both equal to `main` as well (informational; not a finding either way) |
| Phase 4 provisional entries (58) | 44 differ from the committed workstation hashes, 14 are byte-identical (stream-copy remuxes and other host-independent recipes) |
| Non-provisional, non-excluded, differing | 0 |

**Verdict: CLEAN.** No pre-existing recipe's output changed during Phase 4. 04-21 may transcribe the 44 differing provisional lines; the 14 identical ones need no hash change.

## Findings outside this plan's scope

1. **NEW on this branch — `build (x64-windows-static-md)` fails at step 9 "Generate media fixture corpus (BUILD-08 / D-08)".** `scripts/gen_corpus.sh` reports `ffmpeg release-identity mismatch` with `pin expects: UNKNOWN (pin unreadable: unexpected output from the pin reader while reading /d/a/mediadiff/mediadiff/scripts/ffmpeg_pin.json)`, although step 8 (`install_pinned_ffmpeg.sh`) resolved and verified the very same pinned binary one step earlier. The pin reader in `scripts/resolve_pinned_ffmpeg.sh` runs a `python3` heredoc and matches its first output line against the literal `OK`; on the Windows runner python3 emits CRLF line endings, so the line is `OK\r`, the `case` falls through to `*)`, and the gate fails closed. The script has no CR stripping. This gate arrived with `31d285a feat(quick-260910-vvp)` on this branch; `main`'s last run (34353206899 at `8caf1f1`) passed the Windows leg. Not fixed here (this plan changes no source file); it needs its own GSD task before any run of this branch can go green on Windows.
2. **Pre-existing on `main`:** `build (x64-osx)` and `build (arm64-linux)` also fail on `main`'s latest run 34353206899. On this run they failed at
   "Build" (x64-osx) and "Register vcpkg NuGet feed (read-write, trusted runs only)" (arm64-linux) respectively. Not caused by Phase 4; recorded so 04-21's "CI goes green" expectation is read against the two legs that can actually go green (x64-linux and x64-windows-static-md after the fix above, plus arm64-osx and lint, which passed on this run).
3. `build (arm64-osx)` and `lint (ENG-16 boundary)` passed on this run.

## Decisions Made

- Route A of Human Decision 4, chosen by the human at Task 1.
- Executed inline in the orchestrator rather than by a spawned executor: execute-plan's routing sends a plan containing a `checkpoint:decision` to Pattern C (main context), and doing so meant no subagent ever had the standing to push or open the PR before a human answered.
- The Windows regression is a finding, not a deviation: fixing it would violate this plan's "capture only" scope and the plan's prohibition on source changes.

## Deviations from Plan

None in substance — every step of every task ran as written, in order, behind its confirmation. Two notes for the record:

- Task 2 step 1 asks for an empty `git status --porcelain`; it showed the two pre-existing untracked GSD runtime files named above and nothing else. They are in no commit and the push carried only committed history.
- Task 3 step 4 names `gh run view <run-id> --log`; that command refuses while sibling legs are still running, so the job log was fetched through the jobs-logs API instead (documented in the Task 3 section). Same content, same leg.

## Issues Encountered

- `gh api …/jobs/<id>/logs` refuses output containing terminal escape sequences unless `--allow-escape-sequences` is passed; passed it and stripped ANSI codes with `sed` before extracting the steps.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- 04-21 has everything it needs: the verbatim listing above, the run id, the commit sha, and a clean cross-check. Its transcription touches exactly the 44 differing provisional lines and must leave the 80 pre-existing lines byte-identical (`scripts/lint_corpus_digest_provenance.sh` clause 4 enforces this).
- Before 04-21's push, decide whether to fix the Windows pin-reader CRLF regression first (a `/gsd-quick` task on `scripts/resolve_pinned_ffmpeg.sh`), so the second run can go green on both the designated leg and Windows in one shot. Without it, the next run stays red on Windows regardless of the digest.
- x64-osx and arm64-linux are red on `main` already; a fully green matrix is not attainable from this branch alone.

---
*Phase: 04-video-analysis*
*Completed: 2026-09-13*

## Self-Check: PASSED

- SUMMARY exists on disk and its verbatim block contains 138 `<sha256>  <name>` lines + 1 `CORPUS_DIGEST_SUMMARY=` line; the summary recomputed over the block equals the captured value.
- `git ls-remote --heads origin gsd/phase-04-video-analysis` == local HEAD (`196b52a…`).
- `gh pr view --json isDraft,baseRefName,headRefName` == `true` / `main` / `gsd/phase-04-video-analysis`.
- Cross-check: 78/78 pre-existing non-excluded fixtures equal to `main`, 0 findings, 0 missing.
- `tests/golden/CORPUS_DIGEST.txt` untouched by this plan (`git status` shows no tracked change).
