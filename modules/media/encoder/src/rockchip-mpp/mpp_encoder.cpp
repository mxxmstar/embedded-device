#include "mpp_encoder.h"
#include "media/media_timestamp.h"
#include "common/log/logger.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <limits>


namespace {
using Clock = std::chrono::steady_clock;

bool HasNal(const std::vector<uint8_t>& bytes, int type) {
    for (std::size_t i = 0; i + 3 < bytes.size(); ++i) {
        if (bytes[i] || bytes[i + 1]) continue;
        std::size_t nal = 0;
        if (bytes[i + 2] == 1) nal = i + 3;
        else if (i + 4 < bytes.size() && bytes[i + 2] == 0 && bytes[i + 3] == 1) nal = i + 4;
        if (nal && (bytes[nal] & 31) == type) return true;
    }
    return false;
}

// One input frame is fully drained before the DMA buffer is reused. Instances
// are owned by one caller thread, just like the generic FFmpeg encoder.
} // namespace

MppEncoder::~MppEncoder() { Close(); }

bool MppEncoder::Open(const EncoderConfig& cfg) {
        Close();
        ResetStats();
        if (!cfg.is_valid()) return fail("Invalid encoder config");
        const auto* video = std::get_if<VideoEncoderConfig>(&cfg.specific);
        if (!video || cfg.codec_type != CodecType::H264 || video->pixel_format != PixelFormat::kNV12 ||
            video->width > 1920 || video->height > 1080 || (video->width & 1) || (video->height & 1) ||
            static_cast<int64_t>(video->fps_num) > 60LL * video->fps_den || video->max_b_frames != 0 || cfg.bitrate > 100'000'000 ||   
            (!cfg.encoder_name.empty() && cfg.encoder_name != "h264_rkmpp" && cfg.encoder_name != "mpp")) {
            return fail("RK3399 MPP requires H264/NV12, even dimensions up to 1920x1080, no B frames, and a valid rate/time base");
        }
        config_ = cfg;
        /// rk 的 VENC/DMA 以 16 字节为单位对齐
        stride_ = (video->width + 15) & ~15;
        rows_ = (video->height + 15) & ~15;
        time_base_ = cfg.time_base_num > 0 ? Rational{cfg.time_base_num, cfg.time_base_den}
                                          : Rational{video->fps_den, video->fps_num};
        nominal_us_ = RescaleMediaTimestamp(1, {video->fps_den, video->fps_num}, {1, 1'000'000});
        if (nominal_us_ <= 0) {
            return fail("Frame duration is not representable in microseconds");
        }

        /// 创建 mpp 上下文
        if (!check(mpp_create(&ctx_, &api_), "mpp_create")) {
            return openFailed();
        }
        RK_S64 timeout_ms = 2000;
        if (!check(api_->control(ctx_, MPP_SET_OUTPUT_TIMEOUT, &timeout_ms), "output timeout") ||
            !check(api_->control(ctx_, MPP_SET_INPUT_TIMEOUT, &timeout_ms), "input timeout") ||
            !check(mpp_init(ctx_, MPP_CTX_ENC, MPP_VIDEO_CodingAVC), "mpp_init") ||
            !check(mpp_enc_cfg_init(&enc_cfg_), "mpp_enc_cfg_init") ||
            !check(api_->control(ctx_, MPP_ENC_GET_CFG, enc_cfg_), "MPP_ENC_GET_CFG")) return openFailed();

        const auto set = [&](const char* key, int value) {
            return check(mpp_enc_cfg_set_s32(enc_cfg_, key, value), key);
        };
        // 输入描述和编码参数
        if (!set("prep:width", video->width) || !set("prep:height", video->height) ||
            !set("prep:hor_stride", stride_) || !set("prep:ver_stride", rows_) ||
            !set("prep:format", MPP_FMT_YUV420SP) ||
            !set("rc:mode", MPP_ENC_RC_MODE_CBR) ||
            !set("rc:fps_in_flex", 0) || !set("rc:fps_in_num", video->fps_num) ||
            !set("rc:fps_in_denorm", video->fps_den) || !set("rc:fps_out_flex", 0) ||
            !set("rc:fps_out_num", video->fps_num) || !set("rc:fps_out_denorm", video->fps_den) ||
            !set("rc:gop", video->gop_size) || !set("rc:bps_target", static_cast<int>(cfg.bitrate)) ||
            !set("rc:bps_max", static_cast<int>(cfg.bitrate * 17 / 16)) ||
            !set("rc:bps_min", static_cast<int>(std::max<int64_t>(1, cfg.bitrate * 15 / 16))) ||
            !set("rc:qp_init", -1) || !set("rc:qp_min", 10) || !set("rc:qp_max", 51) ||
            !set("rc:qp_min_i", 10) || !set("rc:qp_max_i", 51) || !set("rc:qp_ip", 2) ||
            !set("codec:type", MPP_VIDEO_CodingAVC) || !set("h264:profile", 66) ||
            !set("h264:level", 42) || !set("h264:cabac_en", 0) || !set("h264:cabac_idc", -1) || !set("h264:trans8x8", 0) ||
            !check(api_->control(ctx_, MPP_ENC_SET_CFG, enc_cfg_), "MPP_ENC_SET_CFG")) return openFailed();

        MppEncHeaderMode mode = cfg.global_header ? MPP_ENC_HEADER_MODE_DEFAULT : MPP_ENC_HEADER_MODE_EACH_IDR;
        if (!check(api_->control(ctx_, MPP_ENC_SET_HEADER_MODE, &mode), "header mode") ||
            !check(mpp_buffer_group_get_internal(&group_, MPP_BUFFER_TYPE_DRM), "buffer group") ||
            !check(mpp_buffer_get(group_, &buffer_, static_cast<std::size_t>(stride_) * rows_ * 3 / 2), "input buffer")) return openFailed();

        // 获取 SPS/PPS 作为 extradata
        std::vector<uint8_t> header(4096);
        MppPacket packet = nullptr;
        if (!check(mpp_packet_init(&packet, header.data(), header.size()), "header packet")) {
            return openFailed();
        }
        mpp_packet_set_length(packet, 0);
        const auto ret = api_->control(ctx_, MPP_ENC_GET_HDR_SYNC, packet);
        if (ret == MPP_OK && mpp_packet_get_length(packet) > 0) {
            const auto* begin = static_cast<const uint8_t*>(mpp_packet_get_pos(packet));
            extra_data_.assign(begin, begin + mpp_packet_get_length(packet));
        }
        mpp_packet_deinit(&packet);
        if (!check(ret, "SPS/PPS") || extra_data_.empty()) {
            return openFailed();
        }
        opened_ = true;
        return true;
    }

bool MppEncoder::Encode(FramePtr frame, std::vector<PacketPtr>& packets) {
        if (!frame) return Flush(packets);
        if (!opened_ || flushed_ || failed_) {
            return fail("Encode requires an open, non-flushed encoder");
        }
#if ENCODE_STATS_ENABLE        
        ++stats.encode_calls;
#endif
        const auto started = Clock::now();
        const auto* video = std::get_if<VideoFrameMeta>(&frame->meta);
        if (frame->type != MediaType::VIDEO || !video || video->width != config_.video().width ||
            video->height != config_.video().height || video->pixel_format != PixelFormat::kNV12 || frame->PlaneCount() != 2) {
            return fail("Input must match the configured NV12 dimensions and contain two planes");
        }
        const auto y = frame->PlaneView(0);
        const auto uv = frame->PlaneView(1);
        const auto valid = [&](const MediaPlaneView& plane, int height) {
            return plane.IsValid() && plane.geometry.row_bytes >= static_cast<std::size_t>(video->width) &&
                   plane.geometry.rows >= static_cast<std::size_t>(height);
        };        
        if (!valid(y, video->height) || !valid(uv, video->height / 2)) {
            return fail("Invalid NV12 plane geometry");
        }

        const auto pts = RescaleMediaTimestamp(frame->time.pts_us, {1, 1'000'000}, time_base_);
        int64_t tick = IsValidTimestamp(pts) ? pts : next_pts_;
        tick = std::max(tick, next_pts_);

        // 帧持续时间
        const int64_t duration_us = frame->time.duration_us > 0 ? frame->time.duration_us : nominal_us_;
        const auto duration = RescaleMediaTimestamp(duration_us, {1, 1'000'000}, time_base_);
        const int64_t step = std::max<int64_t>(1, duration);
        if (tick > std::numeric_limits<int64_t>::max() - step) {
            return fail("Timestamp overflow");
        }

        // 搬运输入帧到 DMA 缓冲区
        if (!check(mpp_buffer_sync_begin(buffer_), "input CPU sync")) {
            return false;
        }
        auto* dst = static_cast<uint8_t*>(mpp_buffer_get_ptr(buffer_));
        if (!dst) {
            mpp_buffer_sync_end(buffer_);
            return fail("Input DMA buffer is not mapped");
        }
        std::memset(dst, 0, static_cast<std::size_t>(stride_) * rows_);
        std::memset(dst + static_cast<std::size_t>(stride_) * rows_, 128, static_cast<std::size_t>(stride_) * rows_ / 2);
        const auto copy = [&](const MediaPlaneView& plane, uint8_t* target, int height) {
            for (int row = 0; row < height; ++row)
                std::memcpy(target + static_cast<std::size_t>(row) * stride_,
                    plane.data + static_cast<std::ptrdiff_t>(row) * plane.geometry.stride, video->width);
        };
        copy(y, dst, video->height);
        copy(uv, dst + static_cast<std::size_t>(stride_) * rows_, video->height / 2);
        if (!check(mpp_buffer_sync_end(buffer_), "input device sync")) {
            return false;
        }

        // 将输入帧送入编码器
        MppFrame input = nullptr;
        if (!check(mpp_frame_init(&input), "mpp_frame_init")) {
            return false;
        }
        mpp_frame_set_width(input, video->width);
        mpp_frame_set_height(input, video->height);
        mpp_frame_set_hor_stride(input, stride_);
        mpp_frame_set_ver_stride(input, rows_);
        mpp_frame_set_fmt(input, MPP_FMT_YUV420SP);
        mpp_frame_set_pts(input, tick);
        mpp_frame_set_buffer(input, buffer_);
        const auto ret = api_->encode_put_frame(ctx_, input);
        mpp_frame_deinit(&input);
        if (!check(ret, "encode_put_frame")) {
            failed_ = true;
            return false;
        }
        ++stats.encode_frames;
        if (!drain(packets, false, tick, step)) {
            failed_ = true;
            return false;
        }
        next_pts_ = tick + step;
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - started).count();
        stats.total_encode_time_us += elapsed;
        stats.min_encode_time_us = std::min<uint64_t>(stats.min_encode_time_us, elapsed);
        stats.max_encode_time_us = std::max<uint64_t>(stats.max_encode_time_us, elapsed);
        stats.avg_encode_time_us = stats.total_encode_time_us / stats.encode_frames;
        return true;
    }

bool MppEncoder::Flush(std::vector<PacketPtr>& packets) {
        if (!opened_ || failed_) return fail("Flush requires a healthy open encoder");
        if (flushed_) return true;
        MppFrame eos = nullptr;
        if (!check(mpp_frame_init(&eos), "EOS frame")) return false;
        mpp_frame_set_eos(eos, 1);
        mpp_frame_set_pts(eos, next_pts_);
        const auto ret = api_->encode_put_frame(ctx_, eos);
        mpp_frame_deinit(&eos);
        if (!check(ret, "send EOS") || !drain(packets, true, next_pts_, 0)) {
            failed_ = true;
            return false;
        }
        flushed_ = true;
        return true;
    }

EncodedTrackInfo MppEncoder::GetOutputInfo() const {
    EncodedTrackInfo info;
    if (!opened_) return info;
    info.media_type = MediaType::VIDEO;
    info.codec_type = CodecType::H264;
    info.time_base = time_base_;
    info.bitrate = config_.bitrate;
    info.video().width = config_.video().width;
    info.video().height = config_.video().height;
    info.video().fps = static_cast<float>(config_.video().fps_num) / config_.video().fps_den;
    info.extra_data = extra_data_;
    return info;
}

void MppEncoder::Close() {
        // 先关闭编码器上下文，释放所有 DMA 缓冲区的引用
        if (ctx_) mpp_destroy(ctx_);
        ctx_ = nullptr;
        api_ = nullptr;

        if (enc_cfg_) mpp_enc_cfg_deinit(enc_cfg_);
        enc_cfg_ = nullptr;
        
        if (buffer_) mpp_buffer_put(buffer_);
        buffer_ = nullptr;
        
        if (group_) mpp_buffer_group_put(group_);
        group_ = nullptr;
        opened_ = flushed_ = failed_ = false;
        next_pts_ = 0;
        extra_data_.clear();
    }

bool MppEncoder::fail(const char* message) {
#if ENCODE_STATS_ENABLE
    ++stats.encode_errors;
#endif
    LOG_ERROR("MppEncoder error: {}", message);
    return false;
}

bool MppEncoder::check(MPP_RET ret, const char* operation) {
        if (ret == MPP_OK) return true;
        LOG_ERROR("MppEncoder: {} failed, MPP_RET={}", operation, static_cast<int>(ret));
#if ENCODE_STATS_ENABLE
        ++stats.encode_errors;
#endif
        return false;
    }

bool MppEncoder::openFailed() { Close(); return false; }

bool MppEncoder::drain(std::vector<PacketPtr>& packets, bool eos, int64_t pts, int64_t duration) {
        std::vector<uint8_t> bytes;
        const auto deadline = Clock::now() + std::chrono::seconds(5);
        while (Clock::now() < deadline) {
            MppPacket output = nullptr;
            // 从编码器中获取一个 packet
            const auto ret = api_->encode_get_packet(ctx_, &output);
            if (ret != MPP_OK || !output) {
                if (output) {
                    mpp_packet_deinit(&output);
                }
                if (ret != MPP_OK) {
                    return check(ret, "encode_get_packet");
                }
                return fail("No packet received before output timeout");
            }
            // 判定 packet 是否为 EOS
            const bool end = mpp_packet_get_eos(output) != 0;
            // 非 partition packet 或 EOS packet 表示一个完整的 access unit
            const bool complete = !mpp_packet_is_partition(output) || mpp_packet_is_eoi(output);
            const auto size = mpp_packet_get_length(output);
            const auto* data = static_cast<const uint8_t*>(mpp_packet_get_pos(output));
            // 防止累积过大，或出现空数据
            if ((size && !data) || size > 32 * 1024 * 1024 - bytes.size()) {
                mpp_packet_deinit(&output);
                return fail("Invalid or oversized output packet");
            }

            // 拷贝数据到 bytes 中，释放 packet 内存
            if (size) {
                bytes.insert(bytes.end(), data, data + size);
            }
            mpp_packet_deinit(&output);

            // 无b帧，则一帧进一帧出，无延迟帧
            if (!eos && complete && bytes.empty()) {
                return fail("An input frame produced no encoded payload");
            }

            // 构造 MediaPacket
            if (complete && !bytes.empty()) {
                auto packet = std::make_shared<MediaPacket>();
                packet->type = MediaType::VIDEO;
                packet->codec = CodecType::H264;
                packet->stream_index = 0;
                packet->pts = packet->dts = pts; // 无 B 帧，解码顺序等于显示顺序
                packet->duration = duration;
                packet->time_base = time_base_;
                packet->keyframe = HasNal(bytes, 5);
                // 如果是关键帧且没有全局头信息，则在每个关键帧前插入 SPS/PPS 参数信息
                if (packet->keyframe && !config_.global_header && (!HasNal(bytes, 7) || !HasNal(bytes, 8))) {
                    bytes.insert(bytes.begin(), extra_data_.begin(), extra_data_.end());
                }                    
                packet->buffer = std::make_shared<MediaPacketBuffer>(std::move(bytes));
                packets.push_back(std::move(packet));
                ++stats.encode_packets;
                bytes.clear();
            }
            if (eos ? end : complete) {
                return true;
            }
        }
        return fail("Timed out waiting for a complete output frame/EOS");
    }
std::unique_ptr<IEncoder> CreateEncoder() {
    return std::make_unique<MppEncoder>();
}