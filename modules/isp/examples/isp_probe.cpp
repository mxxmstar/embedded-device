#include "isp/isp_controller.h"
#include <iostream>

/// @brief ISP 探测示例程序入口
/// @details 默认仅只读枚举 media 拓扑并核对 IQ 文件；传 --prepare 时额外初始化 vendor 引擎并立即释放，
///          全程不执行 V4L2 STREAMON，因此不产生图像，也不验证 3A 收敛。
/// @param argc 命令行参数个数
/// @param argv 命令行参数数组
/// @return 0 成功或 --help；1 探测/准备失败；2 参数非法
int main(int argc, char** argv) {
    isp::IspOptions options;
    // 默认只读探测，只有显式 --prepare 才占用 ISP 设备
    bool prepare = false;
    // 解析参数：--media 指定 media 节点，--sensor 只做身份核对，不切换硬件
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--prepare") prepare = true;
        else if ((arg == "--media" || arg == "--sensor") && i + 1 < argc) {
            // 这里先消费下一个参数，故使用 ++i
            if (arg == "--media") options.media_device = argv[++i];
            else options.expected_sensor = argv[++i];
        } else {
            // 未知参数或缺少取值：打印用法后退出，--help 视为正常请求
            std::cerr << "Usage: isp_probe [--media /dev/media0] [--sensor 'exact entity'] [--prepare]\n"
                         "Default: read-only probe. --prepare loads IQ and then releases ISP; does not capture.\n";
            return arg == "--help" ? 0 : 2;
        }
    }
    isp::IspTopology topology;
    std::string error;
    // 只读探测：枚举 media graph 已启用连接，定位 ISP/统计/参数/sensor/lens 节点并核对 IQ 文件可读
    if (!isp::ProbeIsp(options, topology, error)) {
        std::cerr << error << '\n'; return 1;
    }
    // 输出探测结果，便于在板端人工核对节点路径与 IQ 选择是否符合预期
    std::cout << "Media: " << topology.media_device << "\nSensor: " << topology.sensor_entity
              << " (" << topology.sensor_device << ")\nISP: " << topology.isp_device
              << "\nStats: " << topology.statistics_device << "\nParams: " << topology.parameters_device
              << "\nLens: " << topology.lens_device << "\nIQ: " << topology.iq_file << std::endl;
    if (prepare) {
        // 实际初始化：rkisp_cl_init -> rkisp_cl_prepare 加载 IQ 与 3A 插件
        isp::IspController controller;
        if (!controller.Prepare(options, error)) { std::cerr << error << '\n'; return 1; }
        std::cout << "ISP prepared; vendor engine selects the matching IQ under /etc/iqfiles.\n";
        // 立即释放，确认 IQ 加载可用后不长期占用 ISP；析构也会兜底 Reset
        controller.Reset();
        std::cout << "ISP released.\n";
    }
}