#include "probe/parser_scan.h"

#include <cstddef>
#include <cstdint>

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace mediadiff {
namespace detail {

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

  return result;
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
      nal_codec_(other.nal_codec_) {
  other.parser_ = nullptr;
  other.codec_ctx_ = nullptr;
  other.attempted_init_ = false;
  other.has_parser_ = false;
  other.nal_codec_ = NalCodec::none;
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
  other.parser_ = nullptr;
  other.codec_ctx_ = nullptr;
  other.attempted_init_ = false;
  other.has_parser_ = false;
  other.nal_codec_ = NalCodec::none;
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

  return true;
}

}  // namespace detail
}  // namespace mediadiff
