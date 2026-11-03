# RK3399 IspController

独立 ISP 模块，不依赖 FFmpeg、采集模块或配置文件解析。当前实现只针对 NanoPC-T4 的 legacy `camera-engine-rkisp 2.3.0`。

## 依赖与导出

`platform/rockchip/export-rk3399-isp.py` 在 RK3399 上执行，导出系统已安装的 `librkisp.so`、`librkisp_api.so`、AE/AWB/AF 插件及其 DT_NEEDED 依赖，包含路径、版本和 SHA256 清单。它不导出 IQ，不修改设备库。只支持已验证的 2.3.0 包，输出文件必须不存在。

```bash
# 设备端：先复制导出脚本，再运行
python3 /tmp/export-rk3399-isp.py /tmp/rk3399-isp.tar.gz
```

当前已导出到 Ubuntu 工程 `platform/rockchip/deps/aarch64/rkisp/`：

```text
include/rkisp_control_loop.h     # 固定提交的公开接口头文件
rootfs/usr/lib/librkisp.so       # 实机导出的库
rootfs/usr/lib/rkisp/            # 实机插件
rootfs/{lib,usr/lib}/...         # 依赖闭包，供检查和重新部署匹配性参考
manifest.json                   # 实机原始文件清单、SHA256、软件包版本
header-provenance.json           # 头文件来源与校验和
```
文件 rkisp_control_loop.h 的来源与校验和：
```json
{
  "repository": "https://github.com/amarula/rockchip-camera-engine-rkisp",
  "commit": "66874dce46531ccaa3534fcddc5f89415f6349d3",
  "path": "interface/rkisp_control_loop.h",
  "sha256": "2e70884ee1c0194c80b557a151b06b26e3278c48f368995b80057507e27ca0ac",
  "license": "Apache-2.0",
  "verification": "Control-loop struct offsets and signatures compared to target camera-engine-rkisp 2.3.0. Cross-built Prepare/Reset and IQ/plugin loads verified on both NanoPC-T4 media graphs on 2026-10-31. Not a complete matching vendor development SDK."
}
```
SDK 作为独立 overlay 保存，不覆盖现有 sysroot 的 libc/加载器/标准库。头文件在设备未安装开发包的情况下，采用固定提交的公开头文件；结构体布局和函数签名已与板端库/官方服务使用方式核对，再以实机 Prepare 验证。来源及 Apache-2.0 许可证见 `platform/rockchip/rk3399-isp-headers/`。

以后重新导出时，将归档解压到新的 SDK 目录，保留 rootfs 内路径，并把绝对符号链接转成 overlay 内相对链接；不要直接覆盖其他板型 SDK。将受版本管理的接口头文件复制到该目录的 include/。可通过 `RK3399_ISP_SDK_ROOT` 指向新的绝对路径。

## 构建

```bash
cmake -S . -B build-isp-rk3399 \
  -DEMBEDDED_PLATFORM=rockchip -DBUILD_ISP=ON \
  -DBUILD_MEDIA=OFF -DBUILD_RENDER=OFF -DBUILD_TESTS=ON
cmake --build build-isp-rk3399 --target isp_probe test_isp_controller -j4
```

```bash
cmake -S . -B build-isp-loop \
  -DEMBEDDED_PLATFORM=rockchip -DBUILD_ISP=ON \
  -DBUILD_MEDIA=ON -DBUILD_V4L2_CAPTURE=ON -DBUILD_RENDER=OFF -DBUILD_TESTS=ON

cmake --build build-isp-loop --target video_capture_loop -j4
```

RK 平台默认开启 BUILD_ISP，其他平台默认关闭；可显式 `-DBUILD_ISP=OFF`。ISP 是独立的 `Isp::Controller` 库，业务目标需要显式链接该 target，不会在 execution 启动时自动占用摄像头。SDK 路径不写入部署程序 RPATH；板端使用原有 librkisp 与插件，不能仅复制本 overlay 到另一种固件来保证兼容。

## API 与顺序

```cpp
#include "isp/isp_controller.h"

isp::IspController isp;
isp::IspOptions options;
options.media_device = "/dev/media0";
options.expected_sensor = "m00_b_ov13850 1-0010"; // 可选，只做身份核对
std::string error;
if (!isp.Prepare(options, error)) { /* 记录 error，停止启动链路 */ }
// 现有 VideoCapture 执行 V4L2 STREAMON 后：
if (!isp.Start(error)) { /* 停止采集并处理失败 */ }
// 停机时先停止采集，再 Stop / Reset：
isp.Stop(error);
isp.Reset();
```

Prepare 枚举指定 media 节点中已启用的连接，找到唯一 sensor、ISP、统计、参数和可选镜头节点。不会改 media 链接、传感器模式或采集分辨率；这些应由采集/设备初始化协同处理。expected_sensor 是完整 entity 名称，不是单纯的型号，不会切换硬件。

Prepare 使用 `rkisp_cl_init -> rkisp_cl_prepare`；Start/Stop 使用对应 CL API；Reset/析构释放 vendor context。空状态不能 Start；重复 Start/Stop 是幂等的；重复 Prepare 要先 Reset；初始化或启动失败清理 context 后可重试。所有生命周期方法（含析构）由调用方串行执行。Start/Stop 的成功代表引擎调用成功，不等于已收到图像或 3A 收敛。

与设备官方服务一致：Prepare 在采集 STREAMON 前，Start 在 STREAMON 后。当前不自动监听 stream-start/stop 事件，也没有改动现有 VideoCapture。调用方负责协调，避免在无数据时启动/停止造成旧库内部等待。停机时停止采集后再释放 ISP，并且整个生命周期不与 rkisp_3A_server、rkisp_demo 或其他摄像头管理程序同时占用同一 ISP。模块能检测已运行的官方 3A server，但不是对所有外部程序的互斥保证。

## IQ 选择与 ABI 范围

此固件的 `rkisp_cl_init` 实际忽略 tuning_file_path。模块不提供无效的手动文件选项，按驱动模块信息选择：

```text
/etc/iqfiles/<sensor>_<module>_<lens>.xml
```

Probe 读取已验证旧版 `RKMODULE_GET_MODULE_INFO` UAPI（请求 0x945356c0，5203 字节，仅解析开头的三个 32 字节名称），检查匹配 XML 是否可读。这样缺失 IQ 会在调用库前报错，而不是 vendor prepare 成功却关闭 3A。公开旧头文件的 OTP 尾部大小与这台设备不同，因此没有直接使用它来生成 ioctl。该 ABI 常量只适用当前已验证的 legacy 后端，其他内核必须重新核对或提供匹配 UAPI 头文件。实际 XML 解析成功仍需看 vendor 日志/实机验证，不能仅凭文件存在判定。

本版不做热更新、HDR/AE/AWB 参数控制、不封装 RKAIQ、不安装 IQ 文件，也没有引入配置模块。

## 检查与验证

```bash
# 设备端默认仅枚举、读模块身份并核对 IQ 文件；无 3A 初始化
./isp_probe --media /dev/media0
./isp_probe --media /dev/media1
# 确认 ISP 没有其他使用者后，实际初始化、加载 IQ 并释放，不开始采集
./isp_probe --media /dev/media0 --prepare
# x86 上无需任何 SDK 即可验证生命周期逻辑
cmake -S modules/isp/test -B build-isp-host-tests
cmake --build build-isp-host-tests
ctest --test-dir build-isp-host-tests --output-on-failure
```

单元测试使用可失败的后端验证状态、失败回收、幂等、重试和析构。板端 Prepare 检查只验证接口/拓扑/IQ 加载及释放，不代表图像质量、持续采集或 3A 收敛已验证。

## 本次实测（2026-10-31）

- x86 原生生命周期测试通过；交叉编译出的同一测试也在 NanoPC-T4 运行通过。
- isp_probe 交叉编译成功，使用设备原 librkisp.so；无构建机 SDK RPATH，未替换板端库。
- /dev/media0 -> m00_b_ov13850 1-0010 -> /dev/v4l-subdev2，ISP /dev/v4l-subdev0，stats /dev/video3，params /dev/video4，lens /dev/v4l-subdev3。
- /dev/media1 -> m01_f_ov13850 2-0010 -> /dev/v4l-subdev6，ISP /dev/v4l-subdev4，stats /dev/video8，params /dev/video9，lens /dev/v4l-subdev7。
- 两路驱动均报告模组 CMK-CT0116、镜头 Largan-50013A1；IQ 均匹配 `/etc/iqfiles/ov13850_CMK-CT0116_Largan-50013A1.xml`。
- 两路分别执行 Prepare/Reset 成功，退出码 0；strace 证实实际打开对应 IQ XML 及 AE/AWB/AF 插件，没有 vendor 错误输出。
- 错误 sensor 身份、缺失 media 节点均返回错误且退出码 1。
- 板端测试文件及日志位于 `/home/pi/isp-controller-test-20261031/`。本次没有执行 V4L2 STREAMON，没有验证实机 Start/Stop、连续取帧或 3A 收敛；这些需要下一步与 VideoCapture 联调。生命周期 Start/Stop 分支本次以可注入失败的测试后端验证。
