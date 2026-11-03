#include "isp/isp_controller.h"
#include "media/capture/v4l2_video_capture.h"

#include <atomic>
#include <csignal>
#include <iostream>
#include <string>

namespace {



}  // namespace

// ISP Prepare -> V4L2 Open (STREAMON) -> ISP Start -> capture loop -> V4L2 Close -> ISP Stop/Reset.
int main(int argc, char** argv) {
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
    for (std::size_t i = 0; i < 100; ++i) {
        auto result = capture->ReadFrame();
        if (result.status == CaptureReadStatus::Frame) {
            const auto& frame = result.frame;
            const int64_t pts = frame->time.pts_us;
            std::cout << "frame " << i << " pts=" << pts << "\n";
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