#pragma once

// PROBE-03 (04-01-PLAN.md Task 2): ParserScan, the per-access-unit sibling
// of PacketScan (probe/packet_scan.h) -- pict_type/key_frame/repeat_pict/
// field_order plus a fixed-width NAL-type summary, fused INSIDE
// run_packet_scan's own av_read_frame loop (probe/packet_scan.cpp), never
// a second sweep and never its own orchestrator dispatch arm (see
// probe/orchestrator.cpp's own comment on Pass::parser_scan). This
// translation unit performs no decode: av_parser_init/av_parser_parse2/
// av_parser_close and the AVCodecContext this project allocates purely to
// satisfy av_parser_parse2's own signature are as far as it goes -- never
// avcodec_open2, never avcodec_send_packet, never avcodec_receive_frame.
//
// A2 (04-01-PLAN.md flagged assumption): PROBE-03's wording is "NAL-type
// sequences," but a literal variable-length per-AU sequence would make the
// per-AU footprint unbounded, incompatible with D-01's accounted byte
// budget (03-CONTEXT.md). This file stores the information the classifier
// actually consumes in FIXED WIDTH instead: a 64-bit set of NAL types
// observed in the access unit (`nal_type_mask`) plus the leading VCL NAL
// type (`first_vcl_nal_type`) -- satisfies 04-RESEARCH.md's Pitfall 2 ("the
// raw NAL type, or at minimum an IDR-vs-other-IRAP boolean") with room to
// spare. A later phase needing true intra-AU NAL ordering adds a field;
// this is not a reshape.

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

// Opaque forward declarations, at global scope matching libav's own C
// declaration site (mirrors probe/packet_scan.h's own `struct AVPacket;`)
// -- only detail::StreamParserState's own pointer members need them, and
// only as incomplete types; probe/parser_scan.cpp includes the complete
// libavcodec/avcodec.h definitions itself.
struct AVCodecParserContext;
struct AVCodecContext;
struct AVCodecParameters;

namespace mediadiff {

// One access unit's own record, mirroring PacketRecord's shape and
// cardinality class (packet_scan.h's own StreamPacketScan precedent):
// pts/dts verbatim from the packet that produced this AU (sentinels
// preserved exactly as PacketRecord preserves them -- never normalized to
// 0), pict_type/repeat_pict/field_order/key_frame straight from
// AVCodecParserContext, and the fixed-width NAL summary A2 above
// describes. `first_vcl_nal_type == 0xFF` means either "no NAL layer for
// this codec" (VIDEO-12) or "no VCL NAL observed in this AU" -- both
// collapse to the same sentinel, since neither has a real type value to
// report.
struct AccessUnitRecord {
  std::int64_t pts = 0;
  std::int64_t dts = 0;
  int pict_type = 0;
  int repeat_pict = 0;
  int field_order = 0;
  std::uint8_t key_frame = 0;
  // Bit N set == a NAL of type N appeared in this access unit. H.264
  // types occupy bits 0-31 (5-bit nal_unit_type); HEVC types occupy bits
  // 0-63 (6-bit nal_unit_type) -- both fit one std::uint64_t.
  std::uint64_t nal_type_mask = 0;
  std::uint8_t first_vcl_nal_type = 0xFF;
};

// A future field addition here is a deliberate, visible change to D-01's
// per-AU budget arithmetic (probe/packet_scan.cpp accounts
// sizeof(AccessUnitRecord) against the SAME running total PacketRecord
// appends already use) -- never a silent one.
static_assert(sizeof(AccessUnitRecord) == 48,
              "AccessUnitRecord's footprint changed -- review the D-01 budget arithmetic in "
              "probe/packet_scan.cpp alongside this assertion, deliberately, not silently.");

// One stream's own access-unit array -- mirrors StreamPacketScan exactly
// (packet_scan.h): consumers derive their own statistic over this
// read-only array, nothing is pre-computed here (PROBE-10's own
// "IntervalStats was rejected" precedent, packet_scan.h's own header
// comment). `has_parser` is false when av_parser_init returned null for
// this stream's codec (PROBE-03-E1/VIDEO-12) -- set once, on the first
// packet, and never toggles back to true afterward. `partial` mirrors
// StreamPacketScan::partial: true once this stream's own per-AU store hit
// the SAME shared byte budget PacketRecord already accounts against.
struct StreamParserScan {
  std::vector<AccessUnitRecord> access_units;
  bool has_parser = false;
  bool partial = false;
  // 04-09-PLAN.md Task 2 (VIDEO-05, `video.gop.refs`): the H.264 SPS's own
  // `max_num_ref_frames` field, read once -- the first time this stream's
  // own SPS NAL is seen, by `detail::StreamParserState::parse_packet` --
  // and never re-attempted afterward, per this field's own "populated the
  // first time an SPS is seen" contract. `nullopt` for every codec other
  // than H.264 (mpeg4/mpeg2video have no SPS concept at all; HEVC's own
  // SPS is a structurally different layout this reader does not parse --
  // see `detail::read_h264_max_num_ref_frames`'s own header comment), and
  // for an H.264 stream whose SPS was truncated, unreadable, or carries a
  // scaling-list block this reader deliberately does not decode.
  std::optional<std::int64_t> ref_frame_count;
};

// The whole parser scan's result: one StreamParserScan per AVStream, plus
// a result-wide `partial` that is true iff any stream's own is (mirrors
// PacketScanResult::partial's own contract exactly).
struct ParserScanResult {
  std::vector<StreamParserScan> per_stream;
  bool partial = false;
};

// 04-09-PLAN.md Task 2 (`video.frame_types`): libav's own single-letter
// picture-type spelling (`av_get_picture_type_char`, libavutil/avutil.h --
// 'I'/'P'/'B'/'S'/'i'/'p'/'s'/'b'/'?'), resolved HERE (the one place
// src/probe/*.cpp includes libavcodec/avcodec.h) so `src/analyzers/`
// never has to -- mirrors `probe/demux_session.h`'s own "resolve behind
// the libav-including .cpp, expose a plain std::string" boundary
// (`DemuxSession::stream_info`'s own `*_name` fields) for the identical
// reason. Every `AccessUnitRecord::pict_type` value has a defined mapping
// (libav's own default case in `av_get_picture_type_char` returns '?' for
// anything outside its switch, never a crash).
std::string picture_type_name(int pict_type);

// T-4-04's own mitigation: the Annex-B NAL walk stops after this many NALs
// per access unit, bounding a crafted packet of millions of three-byte
// start codes to a fixed cost. Named so no bare literal appears at any use
// site (this plan's own acceptance criterion) -- also exported here so
// tests/unit/test_parser_scan.cpp's Test 7 can drive the bound directly.
inline constexpr int kMaxNalsPerAccessUnit = 4096;

namespace detail {

// Which NAL-layer classifier applies to a stream's codec -- `none` means
// "this codec has no NAL layer at all" (e.g. mpeg4video), and
// walk_annex_b_nal_types returns the zero-mask/0xFF-sentinel result
// unconditionally for it, without attempting any walk.
enum class NalCodec : std::uint8_t {
  none,
  h264,
  hevc,
};

struct NalWalkResult {
  std::uint64_t nal_type_mask = 0;
  std::uint8_t first_vcl_nal_type = 0xFF;
  // 04-09-PLAN.md Task 2 (`video.gop.refs`): the byte offset/length of the
  // FIRST H.264 SPS (type 7) NAL's own RBSP payload within the `data` span
  // this walk was called with -- the NAL header byte excluded, emulation-
  // prevention bytes still present (the caller strips those; see
  // `detail::strip_emulation_prevention`). `nullopt` when no SPS was
  // observed in THIS call's own `data` (a later packet may still carry
  // one -- `detail::StreamParserState` tracks "has an SPS been resolved
  // yet" across calls, not this per-call walk). Populated only for
  // `NalCodec::h264` -- HEVC's own SPS is a structurally different layout
  // this project does not read (see `read_h264_max_num_ref_frames`'s own
  // header comment).
  std::optional<std::pair<std::size_t, std::size_t>> h264_sps_range;
};

// Test-only/production-shared extraction point (T-4-03/T-4-04's own
// regression pins, tests/unit/test_parser_scan.cpp Tests 5-7): the bounded
// Annex-B start-code walk AccessUnitRecord's NAL summary is built from.
// `data` is a raw elementary-stream buffer with embedded 00 00 01 /
// 00 00 00 01 start codes -- MPEG-4 Part 2 (this plan's own mpeg4
// fixtures) and H.264/HEVC Annex-B streams both use this framing; this
// project never walks AVCC length-prefixed H.264/HEVC (not applicable to
// any fixture this plan generates). Every index into `data` is
// bounds-checked before dereference (T-4-03) -- a start code with fewer
// trailing bytes than its NAL-header width needs is dropped, never read
// past the buffer. The walk stops after kMaxNalsPerAccessUnit NALs
// (T-4-04). `codec == NalCodec::none` returns the zero-mask/0xFF-sentinel
// result unconditionally -- no walk is attempted.
NalWalkResult walk_annex_b_nal_types(std::span<const std::uint8_t> data, NalCodec codec);

// Removes Annex-B emulation-prevention bytes (a `0x03` inserted after any
// `00 00` run immediately before a byte `<= 0x03`) from an already-
// extracted RBSP payload -- symmetric with, and the exact inverse of,
// `tools/gen_video_fixtures.py`'s own `emulation_prevention()` writer
// (04-05-SUMMARY.md: "the reader and the writer are symmetric and the
// writer's fixtures are the reader's test vectors"). Exposed here so
// tests/unit/test_gop_classification.cpp can drive it directly over
// hand-built escaped byte sequences.
std::vector<std::uint8_t> strip_emulation_prevention(std::span<const std::uint8_t> data);

// Reads `max_num_ref_frames` from an already-emulation-prevention-stripped
// H.264 SPS RBSP payload (`rbsp`, the 1-byte NAL header already excluded)
// -- 04-09-PLAN.md Task 2, flagged assumption A1: no public libav accessor
// exposes this value (`AVCodecParameters` carries no reference-frame
// count, and `AVCodecParserContext` does not publish the H.264 parser's
// own internal `sps->ref_frame_count` either), so this reader walks the
// SPS's own Exp-Golomb-coded fields directly, symmetric with
// `tools/gen_video_fixtures.py`'s own `build_h264_sps` writer.
//
// Walks every field up to and including `max_num_ref_frames` per the H.264
// spec's own field order (7.3.2.1.1): the high-profile
// chroma_format_idc/bit-depth block when `profile_idc` names one of the
// thirteen profiles that carries it, then the `pic_order_cnt_type`-gated
// branch (0/1/2 each read their own, different, set of fields before
// `max_num_ref_frames`) -- every REAL H.264 stream this check will ever
// see takes one of these three branches, not only the `pic_order_cnt_type
// == 2` shape `tools/gen_video_fixtures.py`'s own fixtures use, so this
// reader does not hardcode that shortcut.
//
// Returns nullopt (never a fabricated value) when: the payload runs out
// before `max_num_ref_frames` is reached (a truncated or malformed SPS --
// T-4-38's own mitigation: every read is bounds-checked against `rbsp`'s
// own length before it executes); `num_ref_frames_in_pic_order_cnt_cycle`
// (a `pic_order_cnt_type == 1` field) exceeds its own spec-legal maximum
// (T-4-38's DoS half -- refuses to loop an attacker-inflated count); or
// the SPS declares `seq_scaling_matrix_present_flag` (a deliberate,
// documented scope boundary -- decoding an arbitrary scaling list is
// materially more complex than every other field this reader touches, no
// fixture in this project's corpus exercises it, and refusing rather than
// guessing at the resulting bit alignment is what keeps a genuinely
// unreadable SPS from producing a confidently wrong reference count).
// Exposed here so tests/unit/test_gop_classification.cpp can drive it
// directly over hand-built SPS payloads, without a fixture file.
std::optional<std::int64_t> read_h264_max_num_ref_frames(std::span<const std::uint8_t> rbsp);

// One stream's own parser lifetime (T-4-02's mitigation): one
// AVCodecParserContext* plus one AVCodecContext* (av_parser_parse2's own
// signature requires the latter even though this project never decodes
// through it -- allocated via avcodec_alloc_context3(nullptr) and
// populated via avcodec_parameters_to_context, never avcodec_open2'd, per
// this file's own top-of-file "no decode" contract), both freed on every
// exit path including this object's own destructor. Move-only (mirrors
// probe/packet_scan.cpp's own ScratchPacket precedent), non-copyable -- a
// copy would double-free the underlying libav contexts.
class StreamParserState {
 public:
  StreamParserState() = default;
  ~StreamParserState();
  StreamParserState(const StreamParserState&) = delete;
  StreamParserState& operator=(const StreamParserState&) = delete;
  StreamParserState(StreamParserState&& other) noexcept;
  StreamParserState& operator=(StreamParserState&& other) noexcept;

  // Lazily initializes from `codecpar` on the FIRST call; every later call
  // on the same object is a no-op that returns the already-resolved
  // outcome. Initialization failure (av_parser_init returning null, or
  // either libav allocation failing) is not an error -- PROBE-03-E1/
  // VIDEO-12: has_parser() becomes and stays false, and no further attempt
  // is made on later calls. Returns has_parser() for caller convenience.
  bool ensure_initialized(const AVCodecParameters& codecpar);

  bool has_parser() const { return has_parser_; }

  // Feeds one packet's own {data, size, pts, dts, pos, is_key} through
  // av_parser_parse2 exactly once (this project's fixtures are
  // already-complete-AU-per-packet muxed streams -- PROBE-03's own "one
  // mpeg4 packet is one access unit" requirement). Returns true and fills
  // `*out` when a complete access unit was produced by that one call;
  // returns false (leaving `*out` untouched) otherwise. Must only be
  // called when has_parser() is true.
  //
  // `out->key_frame`'s own derivation mirrors libavformat's own
  // parse_packet (libavformat/demux.c) verbatim, since
  // AVCodecParserContext::key_frame is documented to stay -1 ("old-style
  // fallback using AV_PICTURE_TYPE_I ... will be used") for a parser that
  // never sets it explicitly -- confirmed empirically: the mpeg4video
  // parser this plan's own fixtures exercise is exactly such a parser.
  // `is_key` is the packet's own AV_PKT_FLAG_KEY bit (PacketRecord::flags
  // & 0x0001), the demuxer-level fallback libavformat itself falls back
  // to when the parser could not determine pict_type at all:
  //   key_frame = (parser->key_frame == 1)
  //            || (parser->key_frame == -1 && parser->pict_type == I)
  //            || (parser->key_frame == -1 && parser->pict_type == NONE && is_key)
  bool parse_packet(const std::uint8_t* data, int size, std::int64_t pts, std::int64_t dts, std::int64_t pos,
                     bool is_key, AccessUnitRecord* out);

  // 04-09-PLAN.md Task 2 (`video.gop.refs`): the H.264 SPS's own
  // `max_num_ref_frames`, resolved (attempted exactly once, the first time
  // this stream's own SPS NAL is seen, regardless of success) as a side
  // effect of `parse_packet`'s own NAL walk -- `nullopt` before any SPS has
  // been observed, after an observed SPS could not be read, or for any
  // codec other than H.264.
  std::optional<std::int64_t> ref_frame_count() const { return ref_frame_count_; }

 private:
  AVCodecParserContext* parser_ = nullptr;
  AVCodecContext* codec_ctx_ = nullptr;
  bool attempted_init_ = false;
  bool has_parser_ = false;
  NalCodec nal_codec_ = NalCodec::none;
  std::optional<std::int64_t> ref_frame_count_;
  bool ref_frame_count_attempted_ = false;
};

}  // namespace detail

}  // namespace mediadiff
