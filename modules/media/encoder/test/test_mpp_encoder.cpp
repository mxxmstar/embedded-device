#include "media/encoder/encoder.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

EncoderConfig Config(int width = 1920, int height = 1080) {
    EncoderConfig cfg;
    cfg.codec_type = CodecType::H264;
    cfg.video().width = width;
    cfg.video().height = height;
    cfg.video().pixel_format = PixelFormat::kNV12;
    cfg.video().fps_num = 30;
    cfg.video().fps_den = 1;
    cfg.video().gop_size = 30;
    cfg.bitrate = 2'000'000;
    return cfg;
}

FramePtr Frame(int width, int height, int index) {
    // Deliberately padded input; the MPP backend must copy rows using plane stride.
    const int stride = width + 32;
    std::vector<uint8_t> bytes(static_cast<std::size_t>(stride) * height * 3 / 2, 0xee);
    for (int row = 0; row < height; ++row)
        for (int col = 0; col < width; ++col)
            bytes[static_cast<std::size_t>(row) * stride + col] = 16 + ((col / 16 + row / 16 + index) % 200);
    for (int row = 0; row < height / 2; ++row)
        for (int col = 0; col < width; ++col)
            bytes[static_cast<std::size_t>(stride) * height + static_cast<std::size_t>(row) * stride + col] = 128;
    auto storage = std::make_shared<MediaBuffer>();
    storage->PushPlane(bytes.data(), static_cast<std::size_t>(stride) * height,
        PlaneGeometry{stride, static_cast<std::size_t>(width), static_cast<std::size_t>(height)});
    storage->PushPlane(bytes.data() + static_cast<std::size_t>(stride) * height,
        static_cast<std::size_t>(stride) * height / 2,
        PlaneGeometry{stride, static_cast<std::size_t>(width), static_cast<std::size_t>(height / 2)});
    auto frame = std::make_shared<MediaFrame>();
    frame->meta = VideoFrameMeta{PixelFormat::kNV12, width, height};
    frame->view = MediaBufferView::FromOwner(storage);
    frame->time.pts_us = static_cast<int64_t>(index) * 1'000'000 / 30;
    frame->time.duration_us = 1'000'000 / 30;
    return frame;
}
}

int main(int argc, char** argv) {
    try {
        const std::string output = argc > 1 ? argv[1] : "/tmp/mpp-encoder-test.h264";
        auto encoder = CreateEncoder();
        auto cfg = Config();
        std::vector<PacketPtr> packets;
        Require(!encoder->Encode(Frame(1920, 1080, 0), packets), "Encode before Open must fail");
        auto bad = cfg;
        bad.codec_type = CodecType::H265;
        Require(!encoder->Open(bad), "Unsupported H265 must fail");
        bad = cfg;
        bad.video().pixel_format = PixelFormat::kI420;
        Require(!encoder->Open(bad), "Unsupported I420 must fail explicitly");
        bad = cfg;
        bad.video().width = 1919;
        Require(!encoder->Open(bad), "Odd dimensions must fail");
        bad = cfg;
        bad.encoder_name = "libx264";
        Require(!encoder->Open(bad), "Software encoder name must not silently select hardware");

        Require(encoder->Open(cfg), "MPP Open failed");
        const auto info = encoder->GetOutputInfo();
        Require(info.is_valid() && info.codec_type == CodecType::H264 &&
                info.time_base.num == 1 && info.time_base.den == 30 &&
                info.video().width == 1920 && info.video().height == 1080 &&
                !info.extra_data.empty(), "Invalid output information/SPS/PPS");
        Require(!encoder->Encode(Frame(640, 480, 0), packets), "Mismatched dimensions must fail");
        encoder->ResetStats();

        std::ofstream file(output, std::ios::binary);
        Require(file.good(), "Cannot open bitstream output");
        std::size_t count = 0, keyframes = 0;
        int64_t previous = kNoTimestamp;
        std::vector<PacketPtr> retained;
        const auto write = [&] {
            for (const auto& packet : packets) {
                Require(packet && packet->buffer && !packet->buffer->Empty(), "Empty packet");
                Require(packet->type == MediaType::VIDEO && packet->codec == CodecType::H264 &&
                        packet->time_base.num == 1 && packet->time_base.den == 30 &&
                        packet->pts == packet->dts && packet->duration == 1,
                        "Incorrect packet metadata/time base");
                Require(packet->pts == static_cast<int64_t>(count) &&
                        (count == 0 || packet->pts > previous), "Unexpected packet timestamp");
                if (count == 0) Require(packet->keyframe, "First packet must be IDR");
                previous = packet->pts;
                keyframes += packet->keyframe;
                file.write(reinterpret_cast<const char*>(packet->buffer->Data()), packet->buffer->Size());
                retained.push_back(packet);
                ++count;
            }
            packets.clear();
        };
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < 90; ++i) {
            Require(encoder->Encode(Frame(1920, 1080, i), packets), "MPP Encode failed");
            write();
        }
        Require(encoder->Flush(packets), "MPP Flush failed");
        write();
        Require(encoder->Flush(packets) && packets.empty(), "Flush must be idempotent");
        Require(count == 90 && keyframes >= 3, "Missing frames or periodic IDRs");
        Require(encoder->GetStats().encode_frames == 90 && encoder->GetStats().encode_packets == 90 &&
                encoder->GetStats().encode_errors == 0, "Encoder statistics mismatch");
        const auto average_encode_us = encoder->GetStats().avg_encode_time_us;
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        file.close();
        Require(file.good(), "Bitstream write failed");
        Require(!encoder->Encode(Frame(1920, 1080, 90), packets), "Encode after Flush must fail");
        encoder->Close();
        Require(!encoder->GetOutputInfo().is_valid(), "Closed output info must be invalid");
        Require(retained.front()->buffer->Data()[0] == 0, "Packet must remain readable after Close");
        encoder->Close();

        // Reopen at a different size and explicit microsecond time base.
        cfg = Config(640, 480);
        cfg.time_base_num = 1;
        cfg.time_base_den = 1'000'000;
        cfg.global_header = true;
        Require(encoder->Open(cfg), "Reopen failed");
        auto frame = Frame(640, 480, 0);
        frame->time.pts_us = 123456;
        frame->time.duration_us = 40000;
        Require(encoder->Encode(frame, packets) && packets.size() == 1 &&
                packets[0]->pts == 123456 && packets[0]->duration == 40000 &&
                packets[0]->time_base.den == 1'000'000, "Explicit time base failed");
        packets.clear();
        Require(encoder->Flush(packets), "Reopened Flush failed");
        encoder->Close();
        std::cout << "PASS MPP H264 NV12 1920x1080: frames=" << count << " IDRs=" << keyframes
                  << " elapsed_s=" << seconds << " fps=" << count / seconds
                  << " avg_encode_us=" << average_encode_us << " output=" << output << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
