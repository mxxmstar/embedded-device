#pragma once
#include <memory>
#include <string>

namespace isp {
/// @brief ISP 运行时参数
struct IspOptions {
    std::string media_device = "/dev/media0";
    std::string expected_sensor;
};

/// @brief ISP 拓扑信息
/// @details 包含 ISP 相关的设备路径、传感器模型、模组名称、镜头名称、IQ 文件路径等。
struct IspTopology {
    std::string media_device;   ///< @brief 媒体设备路径，例如 "/dev/media0"。
    std::string sensor_entity;  ///< @brief 传感器实体名称，例如 "m00_b_ov13850"。
    std::string sensor_device;  ///< @brief 传感器设备路径，例如 "/dev/v4l-subdev2"。
    std::string isp_device;     ///< @brief ISP 设备路径，例如 "/dev/v4l-subdev0"。
    std::string statistics_device;  ///< @brief 统计设备路径，例如 "/dev/video3"。
    std::string parameters_device;  ///< @brief 参数设备路径，例如 "/dev/video4"。
    std::string lens_device;        ///< @brief 镜头设备路径，例如 "/dev/v4l-subdev3"。
    std::string sensor_model;       ///< @brief 传感器模型，例如 "ov13850"。
    std::string module_name;        ///< @brief 模组名称，例如 "CMK-CT0116"。
    std::string lens_name;          ///< @brief 镜头名称，例如 "Largan-50013A1"。
    std::string iq_file;            ///< @brief IQ 文件路径，例如 "/etc/iqfiles/ov13850_CMK-CT0116_Largan-50013A1.xml"。
};

/// @brief 探测 ISP 拓扑信息
/// @details 用于只读获取 ISP 相关的设备路径、传感器模型、模组名称、镜头名称、IQ 文件路径等。
/// @param options ISP 运行时参数
/// @param result ISP 拓扑信息
/// @param error 错误信息
/// @return 是否成功探测到 ISP 拓扑信息
bool ProbeIsp(const IspOptions& options, IspTopology& result, std::string& error);

namespace detail { class IspBackend; }

/// @brief 枚举 media 拓扑、加载 IQ、AE/AWB/AF 插件、运行 3A 控制循环
class IspController final {
public:
    enum class State { Empty, Prepared, Running };
    IspController();
    ~IspController();
    IspController(const IspController&) = delete;
    IspController& operator=(const IspController&) = delete;
    IspController(IspController&&) = delete;
    IspController& operator=(IspController&&) = delete;

    /// @brief 准备 ISP 运行时，探测 ISP 拓扑信息并初始化 ISP 后端
    /// @param options ISP 运行时参数
    /// @param error 错误信息
    /// @return 是否成功准备 ISP 运行时
    bool Prepare(const IspOptions& options, std::string& error);
    
    /// @brief 启动 ISP 控制循环，需要在 V4L2 STREAMON 后调用
    bool Start(std::string& error);

    /// @brief 停止 ISP 控制循环
    bool Stop(std::string& error);

    /// @brief 重置 ISP 控制器，释放资源
    void Reset() noexcept;
    State GetState() const noexcept { return state_; }
    const IspTopology& Topology() const noexcept { return topology_; }

    const std::string SensorDevice() const noexcept { return topology_.sensor_device; }
private:
    std::unique_ptr<detail::IspBackend> backend_;
    State state_ = State::Empty;
    IspTopology topology_;
};
} // namespace isp
