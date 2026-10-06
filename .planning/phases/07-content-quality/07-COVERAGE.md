# API Coverage — libvmaf 3.2.0

> Full coverage by default. Opt-outs are explicit, reasoned decisions.

Scope: libvmaf is the only third-party library surface Phase 7 adds (07-11, CONTENT-09). It is an
in-process C library that is linked statically through the pinned vcpkg `vmaf` feature, and only
into builds configured with `MEDIADIFF_WITH_VMAF=ON`. There is no network service, account, key or
endpoint. FFmpeg's libav* decode surface was integrated in Phases 3-6. Phase 7 uses it without
widening that surface (`avcodec_send_packet` / `avcodec_receive_frame`, `AV_FRAME_DATA_A53_CC`,
frame side data, and swscale for the thumbnail), so it has no row here.

Capability names follow the public headers `libvmaf/libvmaf.h` and `libvmaf/model.h`.

| capability | decision | reason |
|---|---|---|
| context lifecycle (vmaf_init, vmaf_close) | INTEGRATE | |
| built-in model load of vmaf_v0.6.1 (vmaf_model_load, vmaf_model_destroy) | INTEGRATE | |
| feature extractors required by the model (vmaf_use_features_from_model) | INTEGRATE | |
| picture feed, YUV 4:0:0 to 4:4:4, 8-16 bpc (vmaf_read_pictures) | INTEGRATE | |
| pooled scores: harmonic mean, minimum, arithmetic mean (vmaf_score_pooled) | INTEGRATE | |
| library version reporting (vmaf_version) | INTEGRATE | |
| other built-in models (vmaf_4k_v0.6.1, vmaf_v0.6.1neg, phone variants) | OPT-OUT | explicitly out of scope — CONTENT-09 pins vmaf_v0.6.1, and the model is a correctness constant recorded in the fingerprint |
| model files from disk (vmaf_model_load_from_path) | OPT-OUT | explicitly out of scope (also vmaf_model_collection_load_from_path) — the zero-setup single-binary contract allows no external model files |
| model collections and confidence intervals (vmaf_model_collection_load) | OPT-OUT | not needed (also vmaf_score_pooled_model_collection) — v1 reports one pooled score per pair and gates on the harmonic mean (D-03) |
| model feature overloads (vmaf_model_feature_overload) | OPT-OUT | explicitly out of scope — changing extractor options would change the pinned model's meaning |
| per-frame scores (vmaf_score_at_index, vmaf_feature_score_at_index) | OPT-OUT | not needed — frame localisation belongs to content.video.perceptual's worst-10 list; quality.vmaf reports pooled values |
| standalone extra feature extractors (vmaf_use_feature) | OPT-OUT | explicitly out of scope — quality.psnr and quality.ssim are computed in-tree (CONTENT-08); a second libvmaf value for one id would be ambiguous |
| importing external feature scores (vmaf_import_feature_score) | OPT-OUT | not needed — every score is computed from the decoded frames of the two inputs |
| preallocated picture pools (vmaf_preallocate_pictures) | OPT-OUT | not needed (also vmaf_fetch_preallocated_picture) — the lockstep sweep holds one frame in flight per side, so per-pair allocation is bounded; revisit only if profiling shows it matters |
| libvmaf output writers (vmaf_write_output: XML, JSON, CSV, SUB) | OPT-OUT | not needed — mediadiff's own report model renders every value in its text, JSON and JUnit outputs |
| internal thread pool (VmafConfiguration.n_threads above 0) | OPT-OUT | explicitly out of scope — scores must be byte-identical across runs (determinism constraint), and decode is already pinned to one thread (D-11) |
| frame subsampling (VmafConfiguration.n_subsample above 1) | OPT-OUT | explicitly out of scope — CONTENT-09 refuses sampling as skipped:sampling_conflict and every paired frame is scored |
| CPU feature masking (VmafConfiguration.cpumask) | OPT-OUT | not needed — the default dispatch is kept; cross-machine drift is handled by the decode-path precondition, not by masking SIMD |
| GPU and CUDA extraction (VmafConfiguration.gpumask, CUDA builds) | OPT-OUT | explicitly out of scope — the vcpkg libvmaf port has no CUDA feature; PROJECT.md records CUDA VMAF as a manual build |
| Windows builds | OPT-OUT | the vcpkg libvmaf port declares !windows; --vmaf on any build without MEDIADIFF_WITH_VMAF is a usage error naming the option (07-11, CONTENT-09 amended) |
