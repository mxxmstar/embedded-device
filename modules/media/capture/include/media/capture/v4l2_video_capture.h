#pragma once

#include <atomic>
#include <mutex>
#include <vector>
#include <linux/videodev2.h>
#include "media/capture/i_video_capture.h"
#include "media/media_buffer.h"

/// @brief V4L2 视频捕获模式
enum class V4L2DeliveryMode {
    /// @brief 将有效行复制，拷贝结束后归还驱动 slot，与 buffer 完全解耦
    Copy,
    /// @brief 复制有效行到独立紧凑缓冲区，拷贝结束后归还驱动 slot，与 buffer 完全解耦
    CopyCompact,
    /// @brief 不拷贝任何数据，MediaFrame 直接指向 buffer 内存
    MmapLease,
};

class CaptureQueue;
struct v4l2_fract;

struct V4L2CaptureConfig {
    std::string device{"/dev/video0"};  ///< 视频节点设备
    int width{640};
    int height{360};
    PixelFormat pixel_format{PixelFormat::kYUYV422};
    Rational frame_rate{5, 1};
    unsigned int buffer_count{4};
    std::chrono::milliseconds poll_timeout{100};    
    bool allow_negotiation{false};  ///< 是否允许调整大小、格式或帧率
    V4L2DeliveryMode delivery_mode{V4L2DeliveryMode::MmapLease};    
    std::string frame_interval_device{};  ///< 视频采集帧率设备, 用于当视频节点未实现 G_PARM 时，查询实际的 sensor 时序参数
    unsigned int frame_interval_pad{0}; ///< v4l2_subdev_frame_interval 中的 pad 字段，sensor 节点可能有多个 pad，source pad 一般为 0
};


class V4L2VideoCapture final : public IVideoCapture {
public:
    explicit V4L2VideoCapture(V4L2CaptureConfig config);
    ~V4L2VideoCapture() override;
    V4L2VideoCapture(const V4L2VideoCapture&) = delete;
    V4L2VideoCapture& operator=(const V4L2VideoCapture&) = delete;

    CaptureOpenResult Open() override;
    CaptureReadResult ReadFrame() override;
    void RequestStop() noexcept override;
    void Close() noexcept override;
    CaptureVideoInfo GetVideoInfo() const override;

    /// @brief 获取视频帧几何信息
    /// @return 视频帧平面几何信息
    std::vector<PlaneGeometry> Geometry() const;

    /// @brief 配置帧间隔
    /// @param video_fd 视频设备文件描述符
    /// @param type 缓冲区类型
    /// @param config 视频捕获配置
    /// @param interval 帧间隔
    /// @param operation 操作描述
    /// @return 
    int ConfigureFrameInterval(int video_fd, v4l2_buf_type type, const V4L2CaptureConfig& config, v4l2_fract& interval, const char*& operation);
private:
    void cleanup() noexcept;
    /// @brief 读取 sensor 帧间隔
    /// @param config 视频捕获配置
    /// @param interval sensor 帧间隔
    /// @param operation 操作描述
    /// @return 
    int readSensorInterval(v4l2_fract& interval, const char*& operation);
    V4L2CaptureConfig config_;
    mutable std::mutex io_mutex_;   ///< I/O 操作互斥锁
    std::mutex wake_mutex_;         ///< 唤醒互斥锁
    std::atomic<bool> stop_requested_{true};    ///< 请求停止
    int device_fd_{-1};                        ///< 视频设备文件描述符
    int wake_fd_{-1};                          ///< 唤醒文件描述符
    bool streaming_{false};                   ///< 是否正在流式传输
    bool is_mplane_{false};                   ///< 是否为多平面设备
    std::shared_ptr<CaptureQueue> queue_;
    CaptureVideoInfo info_;
    std::vector<PlaneGeometry> plane_geometry_;  ///< 视频帧平面几何信息
};