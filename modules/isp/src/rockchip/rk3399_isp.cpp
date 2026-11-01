#include "isp_backend.h"
#include <rkisp_control_loop.h>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>

// ABI verified against the installed NanoPC-T4 camera-engine-rkisp 2.3.0.
static_assert(sizeof(rkisp_cl_prepare_params_s) == 8 * sizeof(void*));
static_assert(offsetof(rkisp_cl_prepare_params_s, staticMeta) == 7 * sizeof(void*));

namespace isp::detail {
namespace {
/// @brief 扫码 /proc 目录，检查 rkisp_3A_server 是否正在运行
bool HasVendorDaemon() {
    std::error_code error;
    std::filesystem::directory_iterator entries("/proc", error);
    if (error) return false;
    for (const auto& entry : entries) {
        std::ifstream stream(entry.path() / "comm");
        std::string name;
        std::getline(stream, name);
        // This executable's name fits Linux's 15-byte comm limit.
        if (name != "rkisp_3A_server") continue;
        return true;
    }
    return false;
}

/// @brief Rockchip RK3399 ISP 后端实现
class Rk3399Isp final : public IspBackend {
public:
    ~Rk3399Isp() override { Reset(); }
    /// @brief 准备 ISP 运行时，探测 ISP 拓扑信息并初始化 ISP 后端
    /// @param options ISP 运行时参数
    /// @param topology ISP 拓扑信息
    /// @param error 错误信息
    /// @return 是否成功准备 ISP 运行时
    bool Prepare(const IspOptions& options, IspTopology& topology, std::string& error) override {
        // 1、探测 ISP 拓扑信息
        if (!ProbeIsp(options, nodes_, error)) {
            return false;
        }
        // 2、确认 3A server 未运行，避免两个 rkisp_3A_server 同时访问同一个 ISP
        if (HasVendorDaemon()) {
            error = "rkisp_3A_server is running; the same ISP must have a single owner";
            return false;
        }
        
        // 3、创建 rkisp_cl 上下文，这里初始化 tuning_file_path 为 nullptr，因为在该版本中此参数会被忽略
        //  librkisp 2.3.0 会自动选择 /etc/iqfiles/<sensor>_<module>_<lens>.xml
        int result = rkisp_cl_init(&context_, nullptr, nullptr);
        if (result || !context_) {
            error = "rkisp_cl_init failed: " + std::to_string(result);
            return false;
        }
        // 无闪光灯单元和静态 metadata，后续可扩展
        rkisp_cl_prepare_params_s params {};
        params.isp_sd_node_path = nodes_.isp_device.c_str();
        params.isp_vd_params_path = nodes_.parameters_device.c_str();
        params.isp_vd_stats_path = nodes_.statistics_device.c_str();
        params.sensor_sd_node_path = nodes_.sensor_device.c_str();
        params.lens_sd_node_path = nodes_.lens_device.empty() ? nullptr : nodes_.lens_device.c_str();
        // 4、设备打开，IQ 文件加载，3A 插件加载
        result = rkisp_cl_prepare(context_, &params);
        if (result) {
            error = "rkisp_cl_prepare failed: " + std::to_string(result);
            return false;
        }
        topology = nodes_;
        return true;
    }

    /// @brief 启动 ISP 控制循环，3A server 后台运行，在 V4L2 STREAMON 后调用
    /// @param error 错误信息
    /// @return 是否成功启动 ISP 控制循环
    bool Start(std::string& error) override {
        const int result = rkisp_cl_start(context_);
        if (result) {
            error = "rkisp_cl_start failed: " + std::to_string(result);
            return false;
        }
        started_ = true;
        return true;
    }

    /// @brief 停止 ISP 控制循环，关闭 3A server
    bool Stop(std::string& error) override {
        const int result = rkisp_cl_stop(context_);
        if (result) {
            error = "rkisp_cl_stop failed: " + std::to_string(result);
            return false;
        }
        started_ = false;
        return true;
    }

    /// @brief 重置 ISP 后端，关闭 3A server，释放资源
    void Reset() noexcept override {
        if (context_) {
            if (started_) {
                rkisp_cl_stop(context_);
            }
            rkisp_cl_deinit(context_);
            context_ = nullptr;
        }
        started_ = false;
        nodes_ = {};
    }
private:
    void* context_ = nullptr;
    bool started_ = false;
    IspTopology nodes_;
};
} // namespace
std::unique_ptr<IspBackend> CreateIspBackend() { return std::make_unique<Rk3399Isp>(); }
} // namespace isp::detail
