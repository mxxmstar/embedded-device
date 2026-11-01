#include "isp/isp_controller.h"
#include <linux/media.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <array>
#include <cstring>
#include <filesystem>
#include <map>
#include <set>
#include <vector>

namespace isp {
namespace {
struct Fd {
    int value;
    ~Fd() { if (value >= 0) close(value); }
};
bool Fail(std::string& error, const std::string& message) {
    error = message;
    return false;
}

/// @brief 读取相机模块信息，包括传感器型号、模块名称和镜头名称
/// @param topology 相机拓扑结构体，用于存储读取到的相机模块信息
/// @param error 错误信息字符串，用于存储读取失败时的错误信息
bool ReadModuleInfo(IspTopology& topology, std::string& error) {    
    // 逆向推导并锁死旧版 ioctl 命令字
    // 根据 _IOC 宏的编码规则逆向拆解 0x945356c0UL，可以得知它对应的数据大小（Size）是 5203 字节（即 _IOC_SIZE(request)）
    // RKMODULE_GET_MODULE_INFO 是瑞芯微私有的命令序号 0x1453
    constexpr unsigned long request = _IOC(_IOC_READ, 'V', BASE_VIDIOC_PRIVATE, 0x1453);
    static_assert(request == 0x945356c0UL);
    std::array<unsigned char, _IOC_SIZE(request)> data {};  // 5203 字节数据缓冲区
    Fd sensor{open(topology.sensor_device.c_str(), O_RDONLY | O_CLOEXEC)};
    if (sensor.value < 0) {
        return Fail(error, "Open sensor: " + std::string(strerror(errno)));
    }

    // 分配数据缓冲区，大小为 5203 字节
    if (ioctl(sensor.value, request, data.data()) < 0) {
        return Fail(error, "RKMODULE_GET_MODULE_INFO (legacy 2.3.0 ABI): " + std::string(strerror(errno)));
    }
    std::string* fields[] = {&topology.sensor_model, &topology.module_name, &topology.lens_name};
    for (std::size_t i = 0; i < 3; ++i) {
        const auto* text = reinterpret_cast<const char*>(data.data() + 32 * i);
        const auto size = strnlen(text, 32);    // 读取char sensor[32]; char module[32]; char lens[32];
        if (!size || size == 32) {
            return Fail(error, "Missing or unterminated camera module identification");
        }
        fields[i]->assign(text, size);
        if (fields[i]->find('/') != std::string::npos || fields[i]->find("..") != std::string::npos)
            return Fail(error, "Invalid camera module name for IQ selection");
    }
    auto sensor_name = topology.sensor_model;
    // 如果 media entity 名字里含 1608，说明是预处理 ISP，需要在 sensor_name 后添加 -preisp
    if (topology.sensor_entity.find("1608") != std::string::npos) {
        sensor_name += "-preisp";
    }
    topology.iq_file = "/etc/iqfiles/" + sensor_name + "_" + topology.module_name + "_" + topology.lens_name + ".xml";
    
    // 检查 IQ 文件是否存在
    if (access(topology.iq_file.c_str(), R_OK) < 0) {
        return Fail(error, "Matching IQ file is not readable: " + topology.iq_file);
    }
    return true;
}

/// @brief 获取媒体实体的节点路径(例如: /dev/video0)
std::string NodePath(const media_entity_desc& entity) {
    if (!entity.dev.major) return {};
    std::error_code error;
    // /sys/dev/char/major:minor 是内核提供的符号链接，指向该字符设备在设备树中的真实的 sysfs 目录
    auto target = std::filesystem::canonical("/sys/dev/char/" + std::to_string(entity.dev.major) + ":" + std::to_string(entity.dev.minor), error);
    if (error) return {};
    // 
    auto node = std::filesystem::path("/dev") / target.filename();
    struct stat info {};
    if (stat(node.c_str(), &info) || !S_ISCHR(info.st_mode) ||
        info.st_rdev != makedev(entity.dev.major, entity.dev.minor)) return {};
    return node.string();
}

} // namespace

/// @brief 探测指定 media 节点上的 rkisp1 ISP 拓扑
/// @details 全程只读，不改 media 链接、传感器模式或采集分辨率。流程为：
///          校验驱动为 rkisp1 → 分页枚举全部 media entity → 按实体名定位 ISP/统计/参数节点 →
///          为每个实体枚举 link 并只记录已启用项，构成无向连通图 → 从 ISP 出发搜索连通实体，
///          选出唯一连通的 sensor 与可选 lens → 读取模组信息拼出 IQ 文件路径并核对可读性。
/// @param options ISP 运行时参数
/// @param result ISP 拓扑信息，仅在全部检查通过后才被赋值，失败时保持清空状态
/// @param error 错误信息，失败时写入具体原因
/// @return 是否成功探测到完整且唯一的 ISP 拓扑
bool ProbeIsp(const IspOptions& options, IspTopology& result, std::string& error) {
    // 先清空输出与错误，避免失败时调用方看到上一次的半成品数据
    result = {};
    error.clear();
    if (options.media_device.empty()) {
        return Fail(error, "media_device is empty");
    }
    // 只读打开 media 节点，Fd 析构时自动关闭，避免任何提前返回路径泄漏句柄
    Fd media{open(options.media_device.c_str(), O_RDONLY | O_CLOEXEC)};
    if (media.value < 0) {
        return Fail(error, "Open " + options.media_device + ": " + std::string(strerror(errno)));
    }
    // 读取 media 设备信息，用于校验驱动类型
    media_device_info info {};
    if (ioctl(media.value, MEDIA_IOC_DEVICE_INFO, &info) < 0) {
        return Fail(error, "MEDIA_IOC_DEVICE_INFO: " + std::string(strerror(errno)));
    }
    // 本后端只支持 legacy RK3399 的 rkisp1 驱动，其他驱动直接拒绝而不是继续猜测拓扑
    if (std::string(info.driver) != "rkisp1") {
        return Fail(error, "Legacy RK3399 ISP requires driver rkisp1, found " + std::string(info.driver));
    }
    // 分页枚举所有 media entity：每次带上 MEDIA_ENT_ID_FLAG_NEXT 请求下一个 id
    std::map<unsigned int, media_entity_desc> entities;
    unsigned int next = 0;
    while (true) {
        media_entity_desc entity {};
        entity.id = next | MEDIA_ENT_ID_FLAG_NEXT;
        if (ioctl(media.value, MEDIA_IOC_ENUM_ENTITIES, &entity) < 0) {
            // EINVAL 表示没有更多实体，是枚举结束的正常信号；其他错误才是真失败
            if (errno == EINVAL) {
                break;
            }
            return Fail(error, "MEDIA_IOC_ENUM_ENTITIES: " + std::string(strerror(errno)));
        }
        // 防御驱动返回未递增的 id，否则下面的循环会永远不结束
        if (entity.id <= next) return Fail(error, "Non-progressing media entity enumeration");
        next = entity.id;
        entities.emplace(entity.id, entity);
    }
    // 无向连通图：只保留已启用的 link，用于判断哪些实体真正参与当前采集链路
    std::map<unsigned int, std::set<unsigned int>> graph;
    IspTopology topology;
    topology.media_device = options.media_device;
    unsigned int isp_id = 0;
    std::vector<unsigned int> sensors, lenses;
    for (const auto& [id, entity] : entities) {
        // media_entity_desc::name 是定长数组，必须按 strnlen 截断后再构造字符串
        const std::string name(entity.name, strnlen(entity.name, sizeof(entity.name)));
        auto path = NodePath(entity);
        // 按实体名定位三个必需节点，同时记录 ISP 实体 id 作为后续搜索起点
        if (name == "rkisp1-isp-subdev") { topology.isp_device = path; isp_id = id; }
        if (name == "rkisp1-statistics") topology.statistics_device = path;
        if (name == "rkisp1-input-params") topology.parameters_device = path;
        // 按实体类型收集候选 sensor 与 lens，连通性在后面统一判定
        if (entity.type == MEDIA_ENT_T_V4L2_SUBDEV_SENSOR) sensors.push_back(id);
        if (entity.type == MEDIA_ENT_T_V4L2_SUBDEV_LENS) lenses.push_back(id);
        // MEDIA_IOC_ENUM_LINKS 需要调用方按 entity 自带的 pad/link 数量提供缓冲区
        std::vector<media_pad_desc> pads(entity.pads);
        std::vector<media_link_desc> links(entity.links);
        media_links_enum enumeration {};
        enumeration.entity = id;
        enumeration.pads = pads.data();
        enumeration.links = links.data();
        if (ioctl(media.value, MEDIA_IOC_ENUM_LINKS, &enumeration) < 0)
            return Fail(error, "MEDIA_IOC_ENUM_LINKS: " + std::string(strerror(errno)));
        for (const auto& link : links) {
            // 未启用的 link 表示该连接当前不参与取流，不能用来判断设备归属
            if (link.flags & MEDIA_LNK_FL_ENABLED) {
                graph[link.source.entity].insert(link.sink.entity);
                graph[link.sink.entity].insert(link.source.entity);
            }
        }
    }
    // ISP、统计、参数三节点缺一不可，缺失说明不是完整的 rkisp1 拓扑
    if (topology.isp_device.empty() || topology.statistics_device.empty() ||
        topology.parameters_device.empty()) return Fail(error, "Incomplete rkisp1 ISP/statistics/parameters topology");
    // 从 ISP 实体出发做连通性搜索，得到当前实际接入 ISP 的实体集合
    std::set<unsigned int> connected{isp_id};
    std::vector<unsigned int> pending{isp_id};
    while (!pending.empty()) {
        auto id = pending.back(); pending.pop_back();
        for (auto peer : graph[id]) if (connected.insert(peer).second) pending.push_back(peer);
    }
    // 在连通的 sensor 中要求恰好一个：多路同时连通时无法推断该用哪一颗
    unsigned int selected = 0;
    for (auto id : sensors) if (connected.count(id)) {
        if (selected) return Fail(error, "Multiple enabled sensors; select the media graph before preparing ISP");
        selected = id;
    }
    if (!selected) return Fail(error, "No sensor connected to ISP by enabled links");
    const auto& sensor = entities.at(selected);
    topology.sensor_entity.assign(sensor.name, strnlen(sensor.name, sizeof(sensor.name)));
    topology.sensor_device = NodePath(sensor);
    if (topology.sensor_device.empty()) return Fail(error, "Cannot resolve sensor character device");
    // expected_sensor 只做身份核对，匹配失败直接报错，不会尝试切换硬件
    if (!options.expected_sensor.empty() && options.expected_sensor != topology.sensor_entity)
        return Fail(error, "Expected sensor '" + options.expected_sensor + "', found '" + topology.sensor_entity + "'");
    // Legacy camera modules expose one unlinked lens entity per media graph.
    // Ambiguous graphs are rejected rather than opening an unrelated lens.
    // 镜头实体在旧模组上通常不参与 link，因此只按数量判定：多于一个即拒绝，避免打开无关镜头
    if (lenses.size() > 1) return Fail(error, "Multiple lens entities are not supported by this board backend");
    if (lenses.size() == 1) {
        topology.lens_device = NodePath(entities.at(lenses.front()));
        if (topology.lens_device.empty()) return Fail(error, "Cannot resolve lens character device");
    }
    // 读取模组身份并拼出 IQ 文件路径，确认 XML 可读后才算探测成功
    if (!ReadModuleInfo(topology, error)) return false;
    // 全部检查通过后才把结果移交给调用方，失败路径不会污染输出参数
    result = std::move(topology);
    return true;
}
} // namespace isp