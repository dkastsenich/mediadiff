#include "probe/parser_scan.h"

#include <cstddef>
#include <cstdint>
#include <utility>

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace mediadiff {

std::string picture_type_name(int pict_type) {
  return std::string(1, av_get_picture_type_char(static_cast<AVPictureType>(pict_type)));
}

namespace detail {

// H.264 NAL type 7 -- SPS (libavcodec/h264.h, cited in 04-RESEARCH.md
// Priority Finding 3, the same table `kH264NalIdrSlice`-equivalent
// constants in src/analyzers/video/gop.cpp are transcribed from).
constexpr std::uint8_t kH264NalTypeSps = 7;

NalWalkResult walk_annex_b_nal_types(std::span<const std::uint8_t> data, NalCodec codec) {
  NalWalkResult result;
  if (codec == NalCodec::none) {
    // No NAL layer for this codec (e.g. mpeg4video) -- the zero-mask/
    // 0xFF-sentinel result, unconditionally, no walk attempted.
    return result;
  }

  const std::size_t size = data.size();
  int nal_count = 0;
  std::size_t i = 0;
  // 04-09-PLAN.md Task 2: tracks the RBSP payload of the FIRST H.264 SPS
  // seen so far in THIS call -- closed off (becoming `result.h264_sps_range`)
  // the moment the walk finds ANOTHER start code (everything between a
  // NAL's own header and the NEXT start code IS that NAL's payload, by
  // Annex-B construction) or, failing that, at the walk's own natural end
  // below (the SPS was the last NAL this call observed).
  std::optional<std::size_t> pending_sps_payload_start;

  // Bounded Annex-B start-code search: every index dereferenced below is
  // checked against `size` first (T-4-03), and the loop stops after
  // kMaxNalsPerAccessUnit matches regardless of how many more start codes
  // remain in `data` (T-4-04) -- a crafted packet of millions of
  // three-byte start codes costs a bounded amount, never proportional to
  // an attacker-chosen count.
  while (i + 2 < size && nal_count < kMaxNalsPerAccessUnit) {
    if (data[i] != 0 || data[i + 1] != 0 || data[i + 2] != 1) {
      ++i;
      continue;
    }

    if (pending_sps_payload_start.has_value() && !result.h264_sps_range.has_value()) {
      result.h264_sps_range = std::make_pair(*pending_sps_payload_start, i - *pending_sps_payload_start);
    }
    pending_sps_payload_start.reset();

    const std::size_t header_start = i + 3;
    if (codec == NalCodec::h264) {
      if (header_start >= size) {
        // A start code with no following byte at all -- a truncated final
        // NAL (T-4-03, Test 6). Ignored, never read past the buffer; the
        // walk stops here since no further start code can exist in the
        // remaining <3 bytes.
        break;
      }
      const std::uint8_t nal_type = data[header_start] & 0x1F;
      result.nal_type_mask |= (std::uint64_t{1} << nal_type);
      // H264_NAL_SLICE=1 .. H264_NAL_IDR_SLICE=5: the VCL NAL range
      // (libavcodec/h264.h, cited in 04-RESEARCH.md Priority Finding 3).
      if (nal_type >= 1 && nal_type <= 5 && result.first_vcl_nal_type == 0xFF) {
        result.first_vcl_nal_type = nal_type;
      }
      if (nal_type == kH264NalTypeSps && !result.h264_sps_range.has_value()) {
        pending_sps_payload_start = header_start + 1;
      }
      ++nal_count;
      i = header_start + 1;
    } else {
      // HEVC's NAL header is 2 bytes wide.
      if (header_start + 1 >= size) {
        // Fewer than the 2 header bytes HEVC needs -- ignored, never read
        // past the buffer (T-4-03, Test 6).
        break;
      }
      const std::uint8_t nal_type = static_cast<std::uint8_t>((data[header_start] >> 1) & 0x3F);
      result.nal_type_mask |= (std::uint64_t{1} << nal_type);
      // HEVC_NAL_VPS=32 is the first non-VCL type (libavcodec/hevc/hevc.h,
      // cited in 04-RESEARCH.md Priority Finding 3) -- every type below 32
      // is VCL.
      if (nal_type <= 31 && result.first_vcl_nal_type == 0xFF) {
        result.first_vcl_nal_type = nal_type;
      }
      ++nal_count;
      i = header_start + 2;
    }
  }

  // The walk ended (EOF, the kMaxNalsPerAccessUnit bound, or a trailing
  // truncated NAL breaking out of the loop above) with an SPS payload
  // still pending -- close it off against `i`, the walk's own last
  // start-code-search position (approximately the end of `data`; a NAL
  // with nothing after it runs to the end of the buffer by construction).
  if (pending_sps_payload_start.has_value() && !result.h264_sps_range.has_value() &&
      i > *pending_sps_payload_start) {
    result.h264_sps_range = std::make_pair(*pending_sps_payload_start, i - *pending_sps_payload_start);
  }

  return result;
}

// A plain, unoptimized bit-at-a-time reader over an already-emulation-
// prevention-stripped RBSP payload -- symmetric with
// tools/gen_video_fixtures.py's own BitWriter (04-05-SUMMARY.md). Every
// read checks `bit_pos_`'s own byte index against `data_.size()` BEFORE
// consuming a bit (T-4-38): a read that would run past the end sets
// `overflowed_` and returns 0 from that point forward, never
// dereferencing past the payload's own declared size.
class RbspBitReader {
 public:
  explicit RbspBitReader(std::span<const std::uint8_t> data) : data_(data) {}

  bool overflowed() const { return overflowed_; }

  // u(n): a fixed-width unsigned field, MSB first.
  std::uint32_t u(int n) {
    std::uint32_t value = 0;
    for (int i = 0; i < n; ++i) {
      value = (value << 1) | read_bit();
    }
    return value;
  }

  // ue(): unsigned Exp-Golomb -- a bounded count of leading zero bits
  // (kMaxExpGolombLeadingZeroBits below, T-4-38's own DoS half: an
  // all-zero payload cannot force an unbounded leading-zero count) then
  // that many info bits. `se(v)` fields (offset_for_non_ref_pic and
  // friends, pic_order_cnt_type==1's own branch below) are skipped via
  // this SAME function -- se(v)'s signed reinterpretation of the code
  // changes only how the VALUE is decoded, never how many bits it
  // consumes, and this reader only ever needs to skip past those fields,
  // never their value.
  std::uint32_t ue() {
    int leading_zero_bits = 0;
    while (!overflowed_ && read_bit() == 0) {
      ++leading_zero_bits;
      if (leading_zero_bits > kMaxExpGolombLeadingZeroBits) {
        overflowed_ = true;
        return 0;
      }
    }
    if (overflowed_) {
      return 0;
    }
    const std::uint32_t info = (leading_zero_bits > 0) ? u(leading_zero_bits) : 0;
    return (std::uint32_t{1} << leading_zero_bits) - 1 + info;
  }

 private:
  // A 32-bit Exp-Golomb value's own leading-zero-bit count can never
  // legally exceed 31 (u(leading_zero_bits) would itself overflow a
  // uint32_t past that) -- refuses rather than shifting by an
  // out-of-range amount on a crafted all-zero payload.
  static constexpr int kMaxExpGolombLeadingZeroBits = 31;

  std::uint32_t read_bit() {
    const std::size_t byte_index = bit_pos_ / 8;
    if (overflowed_ || byte_index >= data_.size()) {
      overflowed_ = true;
      return 0;
    }
    const int bit_index = 7 - static_cast<int>(bit_pos_ % 8);
    ++bit_pos_;
    return (data_[byte_index] >> bit_index) & 1U;
  }

  std::span<const std::uint8_t> data_;
  std::size_t bit_pos_ = 0;
  bool overflowed_ = false;
};

std::vector<std::uint8_t> strip_emulation_prevention(std::span<const std::uint8_t> data) {
  std::vector<std::uint8_t> out;
  out.reserve(data.size());
  int zero_run = 0;
  for (const std::uint8_t byte : data) {
    if (zero_run >= 2 && byte == 0x03) {
      // The inserted emulation-prevention byte itself -- dropped, and the
      // run resets: the byte immediately after it (the NEXT loop
      // iteration) starts a fresh count, exactly inverting
      // tools/gen_video_fixtures.py's own emulation_prevention() writer.
      zero_run = 0;
      continue;
    }
    out.push_back(byte);
    zero_run = (byte == 0) ? zero_run + 1 : 0;
  }
  return out;
}

namespace {

// H.264 profile_idc values whose SPS carries the chroma_format_idc/bit-
// depth/scaling-list block BEFORE log2_max_frame_num_minus4 (H.264 spec
// 7.3.2.1.1) -- Baseline/Main/Extended (66/77/88, this project's own
// fixtures among them) do not.
bool is_high_profile_idc(std::uint32_t profile_idc) {
  switch (profile_idc) {
    case 100:
    case 110:
    case 122:
    case 244:
    case 44:
    case 83:
    case 86:
    case 118:
    case 128:
    case 138:
    case 139:
    case 134:
    case 135:
      return true;
    default:
      return false;
  }
}

// num_ref_frames_in_pic_order_cnt_cycle's own spec-legal range (H.264
// spec 7.4.2.1.1: "shall be in the range of 0 to 255, inclusive") -- a
// crafted SPS could still encode an arbitrarily large ue(v) here; this
// bound refuses to loop past the spec's own legal maximum rather than
// trusting a hostile field value (T-4-38's DoS half, mirrors
// kMaxNalsPerAccessUnit's own "named constant checked before the loop"
// precedent).
constexpr std::uint32_t kMaxRefFramesInPocCycle = 255;

}  // namespace

std::optional<std::int64_t> read_h264_max_num_ref_frames(std::span<const std::uint8_t> rbsp) {
  RbspBitReader reader(rbsp);
  const std::uint32_t profile_idc = reader.u(8);
  reader.u(8);  // constraint_set0_flag..constraint_set5_flag (6 bits) + reserved_zero_2bits (2 bits)
  reader.u(8);  // level_idc
  reader.ue();  // seq_parameter_set_id

  if (is_high_profile_idc(profile_idc)) {
    const std::uint32_t chroma_format_idc = reader.ue();
    if (chroma_format_idc == 3) {
      reader.u(1);  // separate_colour_plane_flag
    }
    reader.ue();  // bit_depth_luma_minus8
    reader.ue();  // bit_depth_chroma_minus8
    reader.u(1);  // qpprime_y_zero_transform_bypass_flag
    const std::uint32_t seq_scaling_matrix_present_flag = reader.u(1);
    if (seq_scaling_matrix_present_flag != 0) {
      // Deliberate scope boundary (this function's own header comment,
      // parser_scan.h) -- refuses rather than decoding an arbitrary
      // scaling list and risking a misaligned read past this point.
      return std::nullopt;
    }
  }

  reader.ue();  // log2_max_frame_num_minus4
  const std::uint32_t pic_order_cnt_type = reader.ue();
  if (pic_order_cnt_type == 0) {
    reader.ue();  // log2_max_pic_order_cnt_lsb_minus4
  } else if (pic_order_cnt_type == 1) {
    reader.u(1);  // delta_pic_order_always_zero_flag
    reader.ue();  // offset_for_non_ref_pic (se(v) -- see RbspBitReader::ue's own comment)
    reader.ue();  // offset_for_top_to_bottom_field (se(v))
    const std::uint32_t num_ref_frames_in_poc_cycle = reader.ue();
    if (num_ref_frames_in_poc_cycle > kMaxRefFramesInPocCycle) {
      return std::nullopt;
    }
    for (std::uint32_t i = 0; i < num_ref_frames_in_poc_cycle; ++i) {
      reader.ue();  // offset_for_ref_frame[i] (se(v))
    }
  }
  // pic_order_cnt_type == 2: no further POC syntax at all (H.264 spec) --
  // falls straight through to max_num_ref_frames below.

  const std::uint32_t max_num_ref_frames = reader.ue();
  if (reader.overflowed()) {
    return std::nullopt;
  }
  return static_cast<std::int64_t>(max_num_ref_frames);
}

StreamParserState::~StreamParserState() {
  if (parser_ != nullptr) {
    av_parser_close(parser_);
  }
  if (codec_ctx_ != nullptr) {
    avcodec_free_context(&codec_ctx_);
  }
}

StreamParserState::StreamParserState(StreamParserState&& other) noexcept
    : parser_(other.parser_),
      codec_ctx_(other.codec_ctx_),
      attempted_init_(other.attempted_init_),
      has_parser_(other.has_parser_),
      nal_codec_(other.nal_codec_),
      ref_frame_count_(other.ref_frame_count_),
      ref_frame_count_attempted_(other.ref_frame_count_attempted_) {
  other.parser_ = nullptr;
  other.codec_ctx_ = nullptr;
  other.attempted_init_ = false;
  other.has_parser_ = false;
  other.nal_codec_ = NalCodec::none;
  other.ref_frame_count_.reset();
  other.ref_frame_count_attempted_ = false;
}

StreamParserState& StreamParserState::operator=(StreamParserState&& other) noexcept {
  if (this == &other) {
    return *this;
  }
  if (parser_ != nullptr) {
    av_parser_close(parser_);
  }
  if (codec_ctx_ != nullptr) {
    avcodec_free_context(&codec_ctx_);
  }
  parser_ = other.parser_;
  codec_ctx_ = other.codec_ctx_;
  attempted_init_ = other.attempted_init_;
  has_parser_ = other.has_parser_;
  nal_codec_ = other.nal_codec_;
  ref_frame_count_ = other.ref_frame_count_;
  ref_frame_count_attempted_ = other.ref_frame_count_attempted_;
  other.parser_ = nullptr;
  other.codec_ctx_ = nullptr;
  other.attempted_init_ = false;
  other.has_parser_ = false;
  other.nal_codec_ = NalCodec::none;
  other.ref_frame_count_.reset();
  other.ref_frame_count_attempted_ = false;
  return *this;
}

bool StreamParserState::ensure_initialized(const AVCodecParameters& codecpar) {
  if (attempted_init_) {
    return has_parser_;
  }
  attempted_init_ = true;

  if (codecpar.codec_id == AV_CODEC_ID_H264) {
    nal_codec_ = NalCodec::h264;
  } else if (codecpar.codec_id == AV_CODEC_ID_HEVC) {
    nal_codec_ = NalCodec::hevc;
  } else {
    nal_codec_ = NalCodec::none;
  }

  // PROBE-03-E1/VIDEO-12: a codec with no registered libav parser is not
  // an error -- has_parser_ stays false and this stream records nothing.
  AVCodecParserContext* parser = av_parser_init(codecpar.codec_id);
  if (parser == nullptr) {
    return false;
  }

  // av_parser_parse2's own signature requires an AVCodecContext* even
  // though this translation unit never decodes through it (top-of-file
  // "no decode" contract: never avcodec_open2, never avcodec_send_packet,
  // never avcodec_receive_frame) -- allocated purely to carry `codecpar`'s
  // fields (extradata, in particular) to the parser.
  AVCodecContext* codec_ctx = avcodec_alloc_context3(nullptr);
  if (codec_ctx == nullptr) {
    av_parser_close(parser);
    return false;
  }
  if (avcodec_parameters_to_context(codec_ctx, &codecpar) < 0) {
    av_parser_close(parser);
    avcodec_free_context(&codec_ctx);
    return false;
  }

  // Tells the parser this project's own packets are ALREADY complete
  // access units (this project's fixtures are muxed containers with known
  // per-sample sizes, never a raw elementary stream needing frame-boundary
  // search) -- exactly what libavformat/demux.c itself sets when a stream
  // is opened with AVSTREAM_PARSE_HEADERS (confirmed at that file's own
  // av_parser_init call sites). WITHOUT this flag, mpeg4video_parser.c's
  // own mpeg4_find_frame_end searches the buffer for the START of the
  // NEXT frame to decide the CURRENT one is complete -- since one call
  // here is fed exactly one already-complete VOP with no trailing start
  // code, that search finds nothing and ff_combine_frame buffers across
  // calls instead of returning immediately, breaking the "one call, one
  // access unit" contract parse_packet (below) depends on. H.264/HEVC's
  // own parsers make the identical PARSER_FLAG_COMPLETE_FRAMES check.
  parser->flags |= PARSER_FLAG_COMPLETE_FRAMES;

  parser_ = parser;
  codec_ctx_ = codec_ctx;
  has_parser_ = true;
  return true;
}

bool StreamParserState::parse_packet(const std::uint8_t* data, int size, std::int64_t pts, std::int64_t dts,
                                      std::int64_t pos, bool is_key, AccessUnitRecord* out) {
  std::uint8_t* out_buf = nullptr;
  int out_size = 0;
  // One call per packet: this project's fixtures are already-complete-
  // access-unit-per-packet muxed streams (PROBE-03's own "one mpeg4 packet
  // is one access unit" requirement, tests/unit/test_parser_scan.cpp
  // Test 2) -- no loop-until-fully-consumed is needed for that shape, and
  // this translation unit never decodes, so there is no frame to flush
  // across calls the way a real decoder loop would.
  av_parser_parse2(parser_, codec_ctx_, &out_buf, &out_size, data, size, pts, dts, pos);
  if (out_size <= 0) {
    return false;
  }

  // pts/dts verbatim from the packet that produced this AU (sentinels
  // preserved exactly as PacketRecord preserves them -- never normalized
  // to 0) -- NOT AVCodecParserContext::pts/dts, which some parsers derive
  // through their own internal reordering/sync-point logic rather than
  // echoing the input unchanged.
  out->pts = pts;
  out->dts = dts;
  out->pict_type = parser_->pict_type;
  out->repeat_pict = parser_->repeat_pict;
  out->field_order = static_cast<int>(parser_->field_order);

  // Mirrors libavformat/demux.c's own parse_packet fallback verbatim
  // (04-RESEARCH.md-cited, see this method's own header-comment
  // derivation): a parser that explicitly sets key_frame=1 wins outright;
  // a parser that leaves key_frame at its -1 default falls back to
  // pict_type==I, and finally to the packet's own AV_PKT_FLAG_KEY bit when
  // pict_type could not be determined at all. mpeg4video_parser.c (this
  // plan's own fixture codec) never sets key_frame explicitly, so this
  // fallback is load-bearing, not a defensive no-op.
  const bool key_from_pict_type = parser_->key_frame == -1 && parser_->pict_type == AV_PICTURE_TYPE_I;
  const bool key_from_packet_flag =
      parser_->key_frame == -1 && parser_->pict_type == AV_PICTURE_TYPE_NONE && is_key;
  out->key_frame = (parser_->key_frame == 1 || key_from_pict_type || key_from_packet_flag) ? 1 : 0;

  const NalWalkResult nal_result =
      walk_annex_b_nal_types(std::span<const std::uint8_t>(data, static_cast<std::size_t>(size)), nal_codec_);
  out->nal_type_mask = nal_result.nal_type_mask;
  out->first_vcl_nal_type = nal_result.first_vcl_nal_type;

  // 04-09-PLAN.md Task 2 (`video.gop.refs`): the FIRST time this stream's
  // own SPS is seen -- across every packet, not only this one -- read its
  // `max_num_ref_frames` and never attempt again, regardless of success
  // (this field's own "populated the first time an SPS is seen" contract,
  // parser_scan.h). `nal_result.h264_sps_range` is only ever populated for
  // `nal_codec_ == NalCodec::h264` (the walk's own contract above), so the
  // codec check here is a self-documenting belt, not the only guard.
  if (!ref_frame_count_attempted_ && nal_codec_ == NalCodec::h264 && nal_result.h264_sps_range.has_value()) {
    ref_frame_count_attempted_ = true;
    const auto [sps_offset, sps_length] = *nal_result.h264_sps_range;
    const std::size_t data_size = static_cast<std::size_t>(size);
    if (sps_offset <= data_size && sps_length <= data_size - sps_offset) {
      const std::vector<std::uint8_t> stripped =
          strip_emulation_prevention(std::span<const std::uint8_t>(data + sps_offset, sps_length));
      ref_frame_count_ = read_h264_max_num_ref_frames(stripped);
    }
  }

  return true;
}

}  // namespace detail
}  // namespace mediadiff
