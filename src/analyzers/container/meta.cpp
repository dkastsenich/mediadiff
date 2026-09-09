#include "analyzers/container/analyzers.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/check_id.h"
#include "core/model.h"
#include "probe/demux_session.h"

namespace mediadiff {

namespace {

// CONT-03 (03-CONTEXT.md's own stated Claude's-Discretion default): the
// built-in volatile tag key list ships FIXED as the baseline;
// user-extensibility via `mediadiff.toml` is explicitly DEFERRED (Deferred
// Ideas). Matched case-insensitively over ASCII only (ascii_ieq below), so
// a locale can never change which keys are ignored.
//
// `major_brand` is deliberately NOT on this list -- doc 02 section 2's own
// parenthetical scopes brand changes to `container.mp4.brands`, not this
// generic tag comparison; folding it in here would make that dedicated
// check redundant with (and inconsistent with) this one.
constexpr std::array<std::string_view, 4> kVolatileTagKeys = {"creation_time", "encoder", "handler_name",
                                                                "encoding_tool"};

char ascii_lower_char(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

std::string ascii_lower(std::string_view s) {
  std::string out(s);
  for (char& c : out) {
    c = ascii_lower_char(c);
  }
  return out;
}

bool ascii_ieq(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (ascii_lower_char(a[i]) != ascii_lower_char(b[i])) {
      return false;
    }
  }
  return true;
}

bool is_volatile_key(std::string_view key) {
  for (std::string_view volatile_key : kVolatileTagKeys) {
    if (ascii_ieq(key, volatile_key)) {
      return true;
    }
  }
  return false;
}

// T-3-15's mitigation: AVDictionary values (and, defensively, keys) are
// raw bytes with no encoding guarantee. Replaces any byte sequence that is
// not valid UTF-8 with the Unicode replacement character (U+FFFD, the
// 3-byte sequence EF BF BD) before the string can enter a StringSet or
// evidence, so core/serializer.cpp's canonical writer never receives a
// string it cannot emit -- an unemittable string would break the
// byte-identical --json guarantee (TRUST-05) and could truncate a report
// mid-object. Deliberately does NOT strip C0 control bytes -- that is
// T-2-33's fix, closed once at the single render boundary in a later plan
// (03-CONTEXT.md); doing it here too would leave the compared value and
// the rendered value disagreeing about what the tag says. One invalid
// lead byte (or one invalid continuation byte encountered while walking a
// multi-byte sequence) produces exactly one replacement character and
// advances by one input byte, so a run of garbage bytes never collapses
// into a single replacement character that would hide how much data was
// actually invalid.
std::string sanitize_utf8(std::string_view input) {
  static constexpr std::string_view kReplacement = "\xEF\xBF\xBD";
  std::string out;
  out.reserve(input.size());
  const auto* data = reinterpret_cast<const unsigned char*>(input.data());
  const std::size_t n = input.size();
  const auto is_cont = [&](std::size_t idx) { return idx < n && (data[idx] & 0xC0) == 0x80; };

  std::size_t i = 0;
  while (i < n) {
    const unsigned char c = data[i];
    if (c < 0x80) {
      out += static_cast<char>(c);
      ++i;
      continue;
    }

    std::size_t extra = 0;  // continuation bytes beyond the lead byte
    unsigned char lo1 = 0x80;
    unsigned char hi1 = 0xBF;
    if ((c & 0xE0) == 0xC0 && c >= 0xC2) {
      extra = 1;
    } else if ((c & 0xF0) == 0xE0) {
      extra = 2;
      if (c == 0xE0) lo1 = 0xA0;    // reject overlong 3-byte encodings
      if (c == 0xED) hi1 = 0x9F;    // reject UTF-16 surrogate range
    } else if ((c & 0xF8) == 0xF0 && c <= 0xF4) {
      extra = 3;
      if (c == 0xF0) lo1 = 0x90;    // reject overlong 4-byte encodings
      if (c == 0xF4) hi1 = 0x8F;    // reject codepoints past U+10FFFF
    }

    const bool first_cont_ok = extra > 0 && is_cont(i + 1) && data[i + 1] >= lo1 && data[i + 1] <= hi1;
    bool rest_ok = first_cont_ok;
    if (rest_ok) {
      for (std::size_t k = 2; k <= extra; ++k) {
        if (!is_cont(i + k)) {
          rest_ok = false;
          break;
        }
      }
    }

    if (extra == 0 || !rest_ok) {
      out += kReplacement;
      ++i;
      continue;
    }

    out.append(input.substr(i, extra + 1));
    i += extra + 1;
  }
  return out;
}

// A "key=value" StringSet entry, both sides sanitized.
std::string tag_entry(std::string_view key, std::string_view value) {
  return sanitize_utf8(key) + "=" + sanitize_utf8(value);
}

// Emits one meta.tags Measurement for one dictionary (the container's own,
// or one stream's own) at `scope`. Volatile keys (case-insensitive ASCII
// match) are removed from the compared StringSet but every one that is
// actually PRESENT rides into evidence with its own value -- ignoring a
// key for comparison is never the same as hiding it (CONT-03's own
// prohibition).
void emit_meta_tags(const std::vector<std::pair<std::string, std::string>>& tags, Scope scope, Fingerprint& fp) {
  StringSet entries;
  nlohmann::ordered_json ignored_evidence;
  for (const auto& [key, value] : tags) {
    const std::string sanitized_key = sanitize_utf8(key);
    if (is_volatile_key(sanitized_key)) {
      ignored_evidence[sanitized_key] = sanitize_utf8(value);
      continue;
    }
    entries.insert(tag_entry(key, value));
  }

  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::meta_tags);
  measurement.scope = scope;
  measurement.value = std::move(entries);
  if (!ignored_evidence.empty()) {
    measurement.evidence = std::move(ignored_evidence);
  }
  fp.measurements.push_back(std::move(measurement));
}

// StreamMediaType -> the Scope::Kind a per-stream meta.tags/
// meta.tags.language measurement is scoped under. No Scope::Kind exists
// for `attachment` (core/model.h's Scope::Kind enumerators; 03-PATTERNS.md's
// own note) -- an attachment stream's tags/language are skipped entirely
// here rather than forced into a scope that doesn't represent them,
// mirroring topology.cpp's own per-type-count handling of the same gap.
// `other` folds into `data`, matching demux_session.cpp's own
// stream_media_type()/topology.cpp's media_type_name() convention.
std::optional<Scope::Kind> scope_kind_for_stream(StreamMediaType type) {
  switch (type) {
    case StreamMediaType::video:
      return Scope::Kind::video;
    case StreamMediaType::audio:
      return Scope::Kind::audio;
    case StreamMediaType::subtitle:
      return Scope::Kind::subtitle;
    case StreamMediaType::data:
    case StreamMediaType::other:
      return Scope::Kind::data;
    case StreamMediaType::attachment:
      return std::nullopt;
  }
  return std::nullopt;
}

// Every applicable stream's resolved Scope, in stream index order (nullopt
// for a stream with no Scope::Kind counterpart, i.e. an attachment).
// `index` within each Scope is the stream's own rank AMONG STREAMS OF THE
// SAME MEDIA TYPE (audio stream 0, audio stream 1, ...), not its raw
// overall stream-array position -- Claude's Discretion (03-04-PLAN.md
// leaves the exact per-stream scoping scheme unspecified beyond "the
// stream's own scope"), chosen for the same reason 03-CHECK-ROSTER.md's
// CONT-08 note prefers `program_number` over raw array position for TS
// programs: a per-type rank is stable across two files whose OTHER track
// types were added or removed, while a raw overall index would shift and
// mis-pair "audio stream 1" between baseline and candidate whenever a
// video track count differed.
std::vector<std::optional<Scope>> compute_stream_scopes(const DemuxSession& demux) {
  std::vector<std::optional<Scope>> scopes;
  const int count = demux.stream_count();
  scopes.reserve(static_cast<std::size_t>(count));

  int video_rank = 0;
  int audio_rank = 0;
  int subtitle_rank = 0;
  int data_rank = 0;

  for (int i = 0; i < count; ++i) {
    const std::optional<Scope::Kind> kind = scope_kind_for_stream(demux.stream_info(i).media_type);
    if (!kind.has_value()) {
      scopes.push_back(std::nullopt);
      continue;
    }
    int rank = 0;
    switch (*kind) {
      case Scope::Kind::video:
        rank = video_rank++;
        break;
      case Scope::Kind::audio:
        rank = audio_rank++;
        break;
      case Scope::Kind::subtitle:
        rank = subtitle_rank++;
        break;
      case Scope::Kind::data:
        rank = data_rank++;
        break;
      case Scope::Kind::global:
      case Scope::Kind::program:
        // Unreachable: scope_kind_for_stream never returns these two.
        rank = i;
        break;
    }
    scopes.push_back(Scope{*kind, rank});
  }
  return scopes;
}

// meta.tags.language (CONT-04): per-stream `language` tag, ASCII-lowercased.
// Absent or empty normalizes to the literal string "und" (ISO 639-2
// "undetermined") AT MEASUREMENT CONSTRUCTION -- never in the `exact`
// comparator, which has no per-check knowledge and, more importantly,
// would leave a snapshot written before normalization carrying the
// un-normalized value forever, disagreeing with a fresh probe of the same
// file (core/snapshot.cpp's write/read round trip must see the already-
// normalized value). Deliberately does NOT resolve ISO 639-2 T/B aliases
// (fra/fre, deu/ger) -- see docs/checks/meta.tags.language.md's own
// `### Tune` section for why that split must stay a real difference.
std::string normalized_language(const std::vector<std::pair<std::string, std::string>>& tags) {
  for (const auto& [key, value] : tags) {
    if (ascii_ieq(key, "language")) {
      const std::string sanitized = sanitize_utf8(value);
      return sanitized.empty() ? "und" : ascii_lower(sanitized);
    }
  }
  return "und";
}

void emit_meta_language(const DemuxSession& demux, const std::vector<std::optional<Scope>>& stream_scopes,
                          Fingerprint& fp) {
  const int count = demux.stream_count();
  for (int i = 0; i < count; ++i) {
    if (!stream_scopes[static_cast<std::size_t>(i)].has_value()) {
      continue;
    }
    Measurement measurement;
    measurement.check_index = static_cast<std::uint32_t>(CheckId::meta_tags_language);
    measurement.scope = *stream_scopes[static_cast<std::size_t>(i)];
    measurement.value = normalized_language(demux.stream_tags(i));
    fp.measurements.push_back(std::move(measurement));
  }
}

// meta.tags / meta.tags.language (CONT-03, CONT-04): one meta.tags
// measurement for the container's own dictionary (Scope::Kind::global) and
// one for each applicable stream's own dictionary, plus one
// meta.tags.language measurement per applicable stream -- all sharing one
// required_passes declaration (Pass::demux_header only; no packet scan
// needed for header-level tag dictionaries).
void run(const ProbeResults& results, Fingerprint& fp) {
  if (results.demux == nullptr) {
    // See topology.cpp's identical guard for why this is unreachable in
    // practice but still defended.
    return;
  }
  const DemuxSession& demux = *results.demux;

  emit_meta_tags(demux.container_tags(), Scope{Scope::Kind::global, 0}, fp);

  const std::vector<std::optional<Scope>> stream_scopes = compute_stream_scopes(demux);
  const int count = demux.stream_count();
  for (int i = 0; i < count; ++i) {
    if (!stream_scopes[static_cast<std::size_t>(i)].has_value()) {
      continue;
    }
    emit_meta_tags(demux.stream_tags(i), *stream_scopes[static_cast<std::size_t>(i)], fp);
  }

  emit_meta_language(demux, stream_scopes, fp);
}

}  // namespace

namespace detail {

std::string sanitize_utf8_for_test(std::string_view input) { return sanitize_utf8(input); }

std::vector<std::string> volatile_tag_keys_for_test() {
  std::vector<std::string> keys;
  keys.reserve(kVolatileTagKeys.size());
  for (std::string_view key : kVolatileTagKeys) {
    keys.emplace_back(key);
  }
  return keys;
}

}  // namespace detail

const AnalyzerSpec& container_meta_analyzer() {
  static const AnalyzerSpec spec{"container_meta", PassSet{Pass::demux_header}, ContainerFamily::other, &run};
  return spec;
}

}  // namespace mediadiff
