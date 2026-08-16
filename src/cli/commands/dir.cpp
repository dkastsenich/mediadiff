#include "cli/commands/dir.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "cli/color_policy.h"
#include "cli/dir_pairing.h"
#include "cli/exit_code.h"
#include "cli/options.h"
#include "cli/tty_render.h"
#include "cli/worker_pool.h"
#include "compare/engine.h"
#include "compare/semantics.h"
#include "config/toml_load.h"
#include "core/error.h"
#include "core/model.h"
#include "core/policy.h"
#include "core/profiles.h"
#include "core/registry.h"
#include "core/snapshot.h"
#include "core/value.h"
#include "report/json.h"
#include "report/junit.h"
#include "report/markdown.h"
#include "report/model.h"
#include "util/fs.h"
#include "util/version.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace mediadiff {

namespace {

// Mirrors src/cli/commands/compare.cpp's own query_terminal_width (that
// function is not exported -- this file is the second, independent
// terminal-query site, matching this project's existing convention of one
// small platform-conditional query per command file rather than a shared
// helper no other command needs). Extended to also report height, which
// this file's own worst-N table needs and compare.cpp's single-report TTY
// path does not.
struct TerminalSize {
  int width = 100;
  int height = 0;  // 0 = unknown; render_tty's own contract treats this as
                    // "use the default N" rather than "render zero rows".
};

TerminalSize query_terminal_size() {
  TerminalSize size;
#ifdef _WIN32
  const HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
  CONSOLE_SCREEN_BUFFER_INFO info;
  if (handle != INVALID_HANDLE_VALUE && handle != nullptr && GetConsoleScreenBufferInfo(handle, &info)) {
    const int width = info.srWindow.Right - info.srWindow.Left + 1;
    const int height = info.srWindow.Bottom - info.srWindow.Top + 1;
    size.width = width > 0 ? width : 100;
    size.height = height > 0 ? height : 0;
  }
#else
  struct winsize ws {};
  if (isatty(fileno(stdout)) != 0 && ioctl(fileno(stdout), TIOCGWINSZ, &ws) == 0) {
    if (ws.ws_col > 0) {
      size.width = ws.ws_col;
    }
    if (ws.ws_row > 0) {
      size.height = ws.ws_row;
    }
  }
#endif
  return size;
}

// Joins a UTF-8 root directory (an argv path, native-separator on the
// platform the user typed it on) with a forward-slash-separated relative
// path (src/cli/dir_pairing.h's own FilePair contract) via plain byte
// concatenation -- both halves are already UTF-8 text, so no encoding
// conversion is needed, matching src/core/snapshot.cpp's own
// basename_utf8/src/cli/commands/snapshot.cpp's split_path precedent of
// staying on the byte-string path rather than routing through
// std::filesystem::path for a simple join. A forward slash is accepted as
// a path separator by every platform mediadiff targets (including
// Windows' own Win32 file APIs), so the joined path opens correctly
// through util/fs.h's fopen_utf8 regardless of which separator style the
// root itself was typed with.
std::string join_relative(const std::string& root, const std::string& relative_path) {
  if (!root.empty() && (root.back() == '/' || root.back() == '\\')) {
    return root + relative_path;
  }
  return root + "/" + relative_path;
}

// One paired-or-unpaired file's own bookkeeping outcome, written by its
// own WorkerPool job index (this plan's own "results written by index"
// contract) -- kept separate from report/model.h's own FileResult (which
// carries only what build_corpus_model needs) because a hard error or a
// partial-decode marker are dir.cpp's OWN exit-code/diagnostics concerns,
// not part of the shared corpus report model.
struct JobOutcome {
  std::optional<Error> hard_error;
  bool partial = false;
};

}  // namespace

void register_dir_command(CLI::App& app) {
  auto* cmd = app.add_subcommand("dir", "Compare two directories of media artifacts (corpus diff, DIR-01..05)");
  cmd->footer(
      "Note: by default `dir` runs the header-plus-packet pass set only (no decode). "
      "--content additionally requests the decode-pass content checks, which trades corpus speed "
      "for coverage -- expect a `dir` run to take substantially longer per file with --content set.");

  auto baseline_dir = std::make_shared<std::string>();
  auto candidate_dir = std::make_shared<std::string>();
  cmd->add_option("baseline_dir", *baseline_dir, "Baseline directory")->required();
  cmd->add_option("candidate_dir", *candidate_dir, "Candidate directory")->required();

  auto threads = std::make_shared<int>(0);
  auto content_flag = std::make_shared<bool>(false);
  auto no_content_flag = std::make_shared<bool>(false);
  CLI::Option* threads_opt =
      cmd->add_option("--threads", *threads, "Bounded worker-pool size (default: hardware concurrency)");
  cmd->add_flag("--content", *content_flag, "Enable the decode-pass content checks (opt-in for dir mode)");
  cmd->add_flag("--no-content", *no_content_flag,
                "Explicitly disable the decode-pass content checks (dir mode's own default)");

  CliOptions options = add_common_options(*cmd);

  // ENG-16: exit()/stdout/stderr are the CLI's prerogative -- this
  // callback is the one place in the `dir` path permitted to call
  // std::exit() directly.
  cmd->callback([baseline_dir, candidate_dir, threads, threads_opt, content_flag, no_content_flag, options]() {
    (void)no_content_flag;
    const CheckRegistry& registry = builtin_registry();

    // Doc 01 section 6: mediadiff.toml is read exactly once here, before
    // any worker starts -- every job below shares the SAME loaded
    // ConfigFile (and, for path-independent layers, the SAME resolved
    // base Policy) by const reference; no job re-reads the config.
    const std::optional<std::string> explicit_config_path =
        options.policy.config_path->empty() ? std::nullopt : std::make_optional(*options.policy.config_path);
    auto config_result = discover_and_load(explicit_config_path);
    if (!config_result) {
      const Error& err = config_result.error();
      std::fputs(("mediadiff: " + err.message + "\n").c_str(), stderr);
      std::exit(exit_code_for(err.kind));
    }
    const std::optional<ConfigFile>& config = *config_result;

    auto cli_overrides_result = parse_cli_overrides(*options.policy.set_flags, *options.policy.tol_flags);
    if (!cli_overrides_result) {
      const Error& err = cli_overrides_result.error();
      std::fputs(("mediadiff: " + err.message + "\n").c_str(), stderr);
      std::exit(exit_code_for(err.kind));
    }
    const std::vector<CliOverride>& cli_overrides = *cli_overrides_result;

    auto profile_result = resolve_profile_selection(*options.policy.profile, config);
    if (!profile_result) {
      const Error& err = profile_result.error();
      std::fputs(("mediadiff: " + err.message + "\n").c_str(), stderr);
      std::exit(exit_code_for(err.kind));
    }
    const ProfileId profile = *profile_result;

    // The shared, path-INdependent base: builtin -> profile -> config's
    // own top-level [severity]/[tolerance] tables. [override.*] blocks
    // and --set/--tol are deliberately excluded here -- overrides are
    // applied per file below (resolve_policy_for_file), path-scoped, and
    // CLI overrides always apply last regardless of path, matching doc 01
    // section 6's four-layer order exactly.
    std::optional<ConfigFile> config_for_base = config;
    if (config_for_base.has_value()) {
      config_for_base->overrides.clear();
    }
    auto base_policy_result = resolve_policy(registry, profile, config_for_base, /*cli_overrides=*/{});
    if (!base_policy_result) {
      const Error& err = base_policy_result.error();
      std::fputs(("mediadiff: " + err.message + "\n").c_str(), stderr);
      std::exit(exit_code_for(err.kind));
    }
    const Policy base_policy = *base_policy_result;

    // Report destinations, parsed and cross-checked BEFORE the (possibly
    // expensive) corpus pass -- mirrors src/cli/commands/compare.cpp's own
    // "fail fast on a malformed --report before paying for the work"
    // ordering.
    auto report_destinations_result = parse_report_destinations(*options.report.report_flags);
    if (!report_destinations_result) {
      const Error& err = report_destinations_result.error();
      std::fputs(("mediadiff: " + err.message + "\n").c_str(), stderr);
      std::exit(exit_code_for(err.kind));
    }
    const std::vector<ReportDestination>& report_destinations = *report_destinations_result;
    const bool json_requested = options.report.json_option != nullptr && options.report.json_option->count() > 0;
    const bool json_to_file = json_requested && !options.report.json_path->empty();
    if (json_to_file) {
      for (const ReportDestination& dest : report_destinations) {
        if (dest.path == *options.report.json_path) {
          std::fputs(("mediadiff: --json and --report name the same path '" + dest.path + "'\n").c_str(), stderr);
          std::exit(kExitUsage);
        }
      }
    }

    // DIR-02: --content is plumbing only in this phase -- the probe layer
    // (Phase 3) is what actually consumes a pass-selection request. A
    // diagnostic, not an error: the corpus run still completes on the
    // default header-plus-packet pass set.
    if (*content_flag) {
      std::fputs(
          "mediadiff: --content is accepted but has no effect yet -- the decode pass arrives with a later phase\n",
          stderr);
    }

    // --threads resolution: explicit --threads > [dir] threads > hardware
    // concurrency (clamped). An explicit value of zero or negative is a
    // usage error regardless of source (T-2-41: this is simultaneously
    // the concurrency AND the memory knob).
    constexpr int kMaxDefaultThreads = 32;
    int resolved_threads = 0;
    if (threads_opt->count() > 0) {
      if (*threads <= 0) {
        std::fputs("mediadiff: --threads must be a positive integer\n", stderr);
        std::exit(kExitUsage);
      }
      resolved_threads = *threads;
    } else if (config.has_value() && config->dir.has_value() && config->dir->threads.has_value()) {
      // Already validated positive at config-load time
      // (src/config/toml_load.cpp) -- trusted here.
      resolved_threads = *config->dir->threads;
    } else {
      const unsigned hardware = std::thread::hardware_concurrency();
      resolved_threads = hardware == 0 ? 1 : static_cast<int>(hardware);
      if (resolved_threads > kMaxDefaultThreads) {
        resolved_threads = kMaxDefaultThreads;
      }
    }

    // DIR-01/DIR-04: the full pair list, byte-wise sorted, computed BEFORE
    // any worker starts and never mutated afterward.
    auto pairs_result = pair_directories(*baseline_dir, *candidate_dir);
    if (!pairs_result) {
      const Error& err = pairs_result.error();
      std::fputs(("mediadiff: " + err.message + "\n").c_str(), stderr);
      std::exit(exit_code_for(err.kind));
    }
    const std::vector<FilePair>& pairs = *pairs_result;

    const std::optional<std::uint32_t> missing_candidate_idx = registry.find("meta.missing_candidate");
    const std::optional<std::uint32_t> extra_candidate_idx = registry.find("meta.extra_candidate");
    if (!missing_candidate_idx.has_value() || !extra_candidate_idx.has_value()) {
      // Unreachable against the real, shipped registry (both checks are
      // registered in src/core/checks.def) -- guarded explicitly so a
      // future registry misconfiguration fails loudly (exit 70, a
      // mediadiff bug) rather than silently skipping every unpaired
      // finding.
      std::fputs("mediadiff: internal error: meta.missing_candidate/meta.extra_candidate not registered\n", stderr);
      std::exit(kExitInternal);
    }

    // Test-only injection point (this plan's own Task 3 requirement): a
    // genuine internal-error path for CLI-06's exit-70 contract, reached
    // without requiring a real analyzer bug. Read once, before the pool
    // starts, matching every other config value's "read once" contract.
    const auto inject_internal_env = getenv_utf8("MEDIADIFF_DIR_TEST_INJECT_INTERNAL_ERROR");
    const bool inject_internal_error = inject_internal_env.has_value() && !inject_internal_env->empty();

    std::vector<FileResult> results(pairs.size());
    std::vector<JobOutcome> outcomes(pairs.size());

    auto job = [&](std::size_t i) {
      const FilePair& pair = pairs[i];
      results[i].relative_path = pair.relative_path;

      if (inject_internal_error) {
        outcomes[i].hard_error =
            Error{ErrorKind::internal, "injected test failure (MEDIADIFF_DIR_TEST_INJECT_INTERNAL_ERROR)"};
        return;
      }

      auto per_file_policy_result =
          resolve_policy_for_file(base_policy, registry, config, pair.relative_path, cli_overrides);
      if (!per_file_policy_result) {
        outcomes[i].hard_error = per_file_policy_result.error();
        return;
      }
      const Policy& per_file_policy = *per_file_policy_result;

      if (pair.in_baseline && pair.in_candidate) {
        const std::string baseline_path = join_relative(*baseline_dir, pair.relative_path);
        const std::string candidate_path = join_relative(*candidate_dir, pair.relative_path);

        auto baseline_fp = read_snapshot(baseline_path, registry);
        if (!baseline_fp) {
          outcomes[i].hard_error = baseline_fp.error();
          return;
        }
        auto candidate_fp = read_snapshot(candidate_path, registry);
        if (!candidate_fp) {
          outcomes[i].hard_error = candidate_fp.error();
          return;
        }

        bool partial = baseline_fp->partial || candidate_fp->partial;
        auto compare_result = compare_fingerprints(*baseline_fp, *candidate_fp, per_file_policy, registry);
        if (!compare_result) {
          if (compare_result.error().kind == ErrorKind::decode) {
            partial = true;
          } else {
            outcomes[i].hard_error = compare_result.error();
            return;
          }
        } else {
          results[i].findings = std::move(*compare_result);
        }
        outcomes[i].partial = partial;
        return;
      }

      // Unpaired: exactly one side present. Synthesizes the SAME
      // presence-semantic comparison compare/presence.cpp already
      // performs for any other `presence` check, over two Measurements
      // built to represent "this file's own existence" -- meta.missing_candidate
      // (baseline present, candidate absent) or meta.extra_candidate (the
      // reverse), both going through the real resolved Policy so
      // `--set meta.extra_candidate=ignore` behaves identically to any
      // other check override (DIR-01's own must_have).
      const std::uint32_t check_idx = pair.in_baseline ? *missing_candidate_idx : *extra_candidate_idx;
      const CheckDef& check = registry.at(check_idx);

      Measurement baseline_measurement;
      baseline_measurement.check_index = check_idx;
      baseline_measurement.scope = Scope{Scope::Kind::global, 0};
      baseline_measurement.value = pair.in_baseline ? Value(std::string(pair.relative_path)) : Value(Absent{});

      Measurement candidate_measurement;
      candidate_measurement.check_index = check_idx;
      candidate_measurement.scope = Scope{Scope::Kind::global, 0};
      candidate_measurement.value = pair.in_candidate ? Value(std::string(pair.relative_path)) : Value(Absent{});

      auto finding_result = compare_presence(check, baseline_measurement, candidate_measurement, per_file_policy);
      if (finding_result) {
        results[i].findings.push_back(std::move(*finding_result));
      }
    };

    WorkerPool pool(static_cast<std::size_t>(resolved_threads));
    pool.run_indexed(pairs.size(), job);

    // Structural exit-code priority, decided over the full, now-completed
    // corpus (matching src/cli/commands/compare.cpp's own "finish
    // building the report, write every requested destination, THEN
    // decide the special exit code" pattern for a partial/decode
    // failure): the first hard error in pairing order, else "any file
    // partial" (kExitDecode), else the ordinary findings-based decision.
    // A hard error or a partial marker on one file never discards any
    // OTHER file's own result -- the corpus report below still covers
    // every file the pool actually ran.
    std::optional<Error> first_hard_error;
    bool any_partial = false;
    for (const JobOutcome& outcome : outcomes) {
      if (outcome.hard_error.has_value() && !first_hard_error.has_value()) {
        first_hard_error = outcome.hard_error;
      }
      if (outcome.partial) {
        any_partial = true;
      }
    }

    const RenderOptions render_options{/*show_pass=*/true, /*show_ignored=*/true, /*ascii=*/false,
                                        /*strict=*/*options.strict};
    CorpusModel model = build_corpus_model(results, registry, render_options);
    model.envelope.schema_version = std::string(kSchemaVersion);
    model.envelope.tool_version = tool_version();
    for (std::size_t i = 0; i < outcomes.size(); ++i) {
      if (outcomes[i].hard_error.has_value()) {
        model.diagnostics.push_back("file '" + pairs[i].relative_path + "': " + outcomes[i].hard_error->message);
      } else if (outcomes[i].partial) {
        model.diagnostics.push_back("file '" + pairs[i].relative_path + "': partial (decode)");
      }
    }

    if (!json_requested && !*options.quiet) {
      const ColorInputs color_inputs = read_color_inputs(options.color);
      const ColorDecision color = decide_color(color_inputs);
      const RenderOptions tty_options{/*show_pass=*/*options.verbose, /*show_ignored=*/*options.verbose,
                                       /*ascii=*/color.ascii_glyphs, /*strict=*/*options.strict};
      const CorpusModel tty_model = build_corpus_model(results, registry, tty_options);
      const TerminalSize terminal = query_terminal_size();
      const std::string tty_report = render_tty(tty_model, registry, color, terminal.width, terminal.height);
      std::fputs(tty_report.c_str(), stdout);
    }

    if (json_requested) {
      const std::string report = render_json(model, registry, base_policy, *options.verbose);
      if (json_to_file) {
        FILE* handle = fopen_utf8(*options.report.json_path, "wb");
        if (handle == nullptr) {
          std::fputs(("mediadiff: could not open report destination for writing: " + *options.report.json_path + "\n")
                         .c_str(),
                     stderr);
          std::exit(kExitUsage);
        }
        const std::size_t written = std::fwrite(report.data(), 1, report.size(), handle);
        const bool close_ok = std::fclose(handle) == 0;
        if (written != report.size() || !close_ok) {
          std::fputs(("mediadiff: failed writing report destination: " + *options.report.json_path + "\n").c_str(),
                     stderr);
          std::exit(kExitUsage);
        }
      } else {
        std::fputs(report.c_str(), stdout);
      }
    }

    for (const ReportDestination& dest : report_destinations) {
      std::string rendered;
      switch (dest.kind) {
        case ReportDestination::Kind::md:
          rendered = render_markdown(model, registry, *options.strict);
          break;
        case ReportDestination::Kind::junit:
          rendered = render_junit(model, registry, *options.strict);
          break;
      }
      FILE* handle = fopen_utf8(dest.path, "wb");
      if (handle == nullptr) {
        std::fputs(("mediadiff: could not open report destination for writing: " + dest.path + "\n").c_str(), stderr);
        std::exit(kExitUsage);
      }
      const std::size_t written = std::fwrite(rendered.data(), 1, rendered.size(), handle);
      const bool close_ok = std::fclose(handle) == 0;
      if (written != rendered.size() || !close_ok) {
        std::fputs(("mediadiff: failed writing report destination: " + dest.path + "\n").c_str(), stderr);
        std::exit(kExitUsage);
      }
    }

    if (first_hard_error.has_value()) {
      std::exit(exit_code_for(first_hard_error->kind));
    }
    if (any_partial) {
      std::exit(kExitDecode);
    }
    std::exit(exit_code_for_findings(model.totals, *options.strict));
  });
}

}  // namespace mediadiff
