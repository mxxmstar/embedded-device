#include "isp/isp_controller.h"
#include "media/capture/v4l2_video_capture.h"
#include "media/publisher/publisher.h"
#include "media/encoder/encoder.h"
#include "common/log/logger.h"

#include <atomic>
#include <csignal>
#include <iostream>
#include <string>

namespace {



}  // namespace

// ISP Prepare -> V4L2 Open (STREAMON) -> ISP Start -> capture loop -> V4L2 Close -> ISP Stop/Reset.
int main(int argc, char** argv) {
    auto pusher = CreateMediaPusher();    
    auto session = std::make_unique<PusherSession>(std::move(pusher));
    Publisher publisher(std::move(session));
    
    PusherConfig pusher_config{};
    pusher_config.output_url = "rtsp://192.168.177.123:554/live/rk3399_pusher";
    pusher_config.ffmpeg.rtsp = RtspOutputOptions{"tcp"};
    auto& track = pusher_config.video_track;
    track.track_id = 0;
    track.codec_type = CodecType::H264;
    track.media_type = MediaType::VIDEO;
    track.video().width = 1920;
    track.video().height = 1080;
    track.video().fps = 30.0f;
    // track.time_base_num = 1;
    // track.time_base_den = 300;
    
    PusherSessionConfig session_config{};
    session_config.pusher = pusher_config;
    PublisherConfig publisher_config{};
    publisher_config.kind = PublisherKind::Client;
    PublisherTargetConfig target_config{};
    target_config.target_id = "rtsp:zlm";
    target_config.session = session_config;
    publisher_config.targets.push_back(target_config);

    publisher.Open(publisher_config);
    
    EncoderConfig encoder_config{};
    encoder_config.media_type = MediaType::VIDEO;
    encoder_config.codec_type = CodecType::H264;
    encoder_config.bitrate = 1000000;
    encoder_config.video().width = 1920;
    encoder_config.video().height = 1080;
    encoder_config.video().fps_num = 30;
    encoder_config.video().fps_den = 1;
    encoder_config.video().pixel_format = PixelFormat::kNV12;
    
    auto encoder = CreateEncoder();
    encoder->Open(encoder_config);
    std::size_t packet_count = 0;
    const auto write_packets = [&](const std::vector<PacketPtr>& packets) {
        for (const auto& packet : packets) {
            if (!packet || !packet->buffer || packet->buffer->Empty())
            {
                LOG_ERROR("Invalid encoded packet");
            } 
                        
            const auto result = publisher.Publish(*packet);
            if (!result.WasPublished())
            {
                LOG_ERROR("Publisher failed to write capture packet");
            }
            ++packet_count;
        }
    };


    isp::IspOptions isp_options;
    isp::IspController isp_controller;
    std::string error;
    if (!isp_controller.Prepare(isp_options, error)) {
        std::cerr << "ISP Prepare failed: " << error << std::endl;
        return 1;
    }
    std::cout << "ISP prepared successfully." << std::endl;

    V4L2CaptureConfig config;
    config.device = "/dev/video0";
    config.frame_interval_device = isp_controller.SensorDevice();
    config.width = 1920;
    config.height = 1080;
    config.pixel_format = PixelFormat::kNV12;
    config.frame_rate = {30, 1};
    config.delivery_mode = V4L2DeliveryMode::Copy;

    auto capture = std::make_unique<V4L2VideoCapture>(config);
    auto open_result = capture->Open();
    if (!open_result.Succeed()) {
        std::cerr << "V4L2 Open failed: code=" << open_result.error->native_code 
            << ", msg=" << open_result.error->message << std::endl;
        return 1;
    }
    const auto info = capture->GetVideoInfo();
    std::cout << "V4L2 Open successfully. Actual frame rate: "
        << info.frame_rate.num << "/" << info.frame_rate.den << " fps" << std::endl;

    // 启动 ISP 控制循环
    if (!isp_controller.Start(error)) {
        std::cerr << "ISP Start failed: " << error << std::endl;
        capture->Close();
        isp_controller.Reset();
        return 1;
    }
    std::cout << "ISP started successfully." << std::endl;    

    // 视频采集循环
    for (std::size_t i = 0; i < 1000000; ++i) {
        auto result = capture->ReadFrame();
        if (result.status == CaptureReadStatus::Frame) {
            const auto& frame = result.frame;
            const int64_t pts = frame->time.pts_us;
            std::cout << "frame " << i << " pts=" << pts << "\n";
            std::vector<PacketPtr> packets;
            encoder->Encode(frame, packets);
            write_packets(packets);
        } else if (result.status == CaptureReadStatus::NoData) {
            std::cout << "no data, continue\n";
            continue;
        } else {
            std::cerr << "Capture stopped: " << (result.error ? result.error->message : "unknown") << "\n";
            break;
        }
    }

    // 先停采集，再停 ISP
    capture->Close();       // STREAMOFF
    isp_controller.Stop(error);
    isp_controller.Reset();
    return 0;
}