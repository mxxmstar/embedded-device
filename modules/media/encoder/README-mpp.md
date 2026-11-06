# RK3399 MPP 硬件编码与交叉编译

验证日期：2026-11-05。源码主机为 `ub22@192.168.117.131`，工程 `/home/ub22/mxxmstar/embedded-device`；设备为 `pi@192.168.177.6`，NanoPC-T4 / RK3399。

## 当前实现

Rockchip 平台的 `CreateEncoder()` 创建原生 MPP 编码器，直接链接 `Rockchip::MPP` / `librockchip_mpp.so.1`。generic 平台继续使用原有 FFmpegEncoder。本实现没有修改 FFmpeg，也没有给系统 ffmpeg 命令增加 `h264_rkmpp`。

首版范围：

- H.264，NV12，两平面 CPU 可读帧，偶数尺寸，最大 1920×1080；无 B 帧。
- `encoder_name` 为空、`mpp` 或 `h264_rkmpp`；后两个是本后端接受的名称，并不表示系统 FFmpeg 提供同名编码器。
- Baseline profile，level 4.2，CBR；使用 bitrate、fps、gop 配置。FFmpeg 专属 preset/tune/crf、thread_count、max_bitrate、rc_buffer_size 等设置不映射到 MPP。
- 输入按各 plane 的 stride 逐行复制到 MPP DRM 缓冲，内部高度按 16 对齐；当前是 CPU 拷贝输入，没有 DMABUF 零拷贝或 RGA 转换。
- 同步发送一帧并收完该帧输出后才复用 DMA 缓冲；输入/输出 MPP 超时为 2 秒，分片拼接总截止时间为 5 秒。
- 输出拥有独立 payload；无 B 帧时 DTS=PTS。输入时间戳单位为微秒，输出按配置时间基换算，默认 1/fps。缺失或倒退的 PTS 按下一可用 tick 归一化。
- `GetOutputInfo()` 返回当前尺寸、码率、时间基和 Annex-B SPS/PPS。`global_header=false` 时保证每个 IDR 带参数集；`global_header=true` 时调用方应把 extra_data 提供给封装器，MPP 默认模式仍可能在码流中带头。
- `Flush()` 发送 EOS 并收取输出，重复 Flush 幂等；Flush 后拒绝新帧，需要重新 Open。Close 释放资源，不代替 Flush。
- H.265、音频和 I420 输入均明确拒绝。本次只实现 H.264，板子上已验证的 JPEG/VP8 硬编未在此接口实现。

## 构建

```bash
cd /home/ub22/mxxmstar/embedded-device
cmake -S . -B build-mpp-rk3399 \
  -DEMBEDDED_PLATFORM=rockchip -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_RENDER=OFF -DBUILD_TESTS=ON -DBUILD_MEDIA_TESTS=ON \
  -DMEDIA_FFMPEG_FULL=OFF -DMEDIA_FFMPEG_ROCKCHIP=ON \
  -DBUILD_V4L2_CAPTURE=ON -DROCKCHIP_WITH_MPP=ON \
  -DFFMPEG_ROOT="$PWD/platform/rockchip/deps/aarch64/ffmpeg-board"
cmake --build build-mpp-rk3399 \
  --target test_mpp_encoder capture_loop_pusher -j4


cd /home/ub22/mxxmstar/embedded-device
cmake --build build-mpp-rk3399 --target media_encoder -j4
cmake --build build-mpp-rk3399 --target capture_loop_pusher -j4
```

产物：

- `build-mpp-rk3399/lib/libmedia_encoder.a`
- `build-mpp-rk3399/bin/test_mpp_encoder`
- `build-mpp-rk3399/bin/capture_loop_pusher`

使用现有 GCC12 ARM64 工具链及 NanoPC sysroot。sysroot 和设备的 MPP 库 SHA-256 相同：`dcc24ebc415778fb5038e3227c93718c602d6da369958a03b4cd4968f0711d57`。测试二进制直接依赖 MPP 和 C++ 运行库，不依赖 FFmpeg converter。采集应用仍使用 FFmpeg pusher。

CMake 同时修复了 converter / encoder / publisher 的重复添加，以及 capture_loop_pusher 对 ISP、Capture、Encoder、Publisher 的直接链接。应用仅在依赖目标齐备时创建。Rockchip 路径不再构建尚未适配 FFmpeg 4.2 的软件 converter。

## 板端验证

已将新程序单独放在 `/tmp/mpp-cross-check-20261105/`，没有替换系统库或已有应用。

```bash
cd /tmp/mpp-cross-check-20261105
timeout 30 ./test_mpp_encoder "$PWD/test.h264"
ffprobe -v error -count_frames \
  -show_entries stream=codec_name,width,height,nb_read_frames \
  -of json test.h264
ffmpeg -v error -xerror -i test.h264 -f null -
```

结果：1080p NV12 输入，90 帧，3 个 IDR；测试退出 0，ffprobe 解码计帧为 90，完整解码退出 0，probe/decode 错误日志均为空。

测试包含：未 Open 编码失败、非法配置拒绝、尺寸不匹配拒绝、带 padding 的输入 stride、输出元数据和时间戳、SPS/PPS、周期 IDR、统计、EOS、重复 Flush、Flush 后拒绝编码、重复 Close、Close 后 packet 生命周期、不同分辨率重新 Open、显式微秒时间基。

本次端到端测试约 23.96fps（包括合成图生成、分配、拷贝与编码），Encode 平均约 26.58ms。该短时测试不是持续 1080p30 或真实摄像头吞吐保证，后续应结合摄像头、RGA/DMABUF 及发布队列单独测量。

generic 回归：新建 `build-mpp-generic-check`，使用原有主机 FFmpeg 8.1 SDK，media_encoder 与帧适配测试构建成功，`test_media_test_media_frame_view` 通过。

`capture_loop_pusher` 已构建且板端 ldd 无缺失库；本次没有运行其摄像头/ISP/RTSP 链路，应用原有返回值处理、Flush 调用、发布轨道参数同步等仍需补齐。没有向硬编码 RTSP 地址发送视频。

## 与 FFmpeg 的关系

板端 FFmpeg 4.2 有 `--enable-rkmpp`，但没有 `h264_rkmpp` 编码器。当前原生 MPP 后端不要求升级板端 FFmpeg。若业务要求统一使用 avcodec API，应另行交叉编译包含 MPP 编码补丁的 FFmpeg，并核对其 SDK、API、驱动和板端 ABI；这不属于本次已完成的构建。

## 工作树与日志

原有用户修改未提交、未还原；应用源文件保持原样。修改前相关文件备份：虚拟机 `/tmp/embedded-before-mpp-20261105.tar.gz`。

虚拟机日志：`/tmp/mpp-cross-configure.log`、`/tmp/mpp-cross-build.log`、`/tmp/mpp-generic-check.log`、`/tmp/mpp-generic-build-final.log`。板端日志：`/tmp/mpp-cross-check-20261105/test.log`、`probe.json`、`probe-errors.log`、`decode.log`。

最终板端程序 SHA-256：

```text
3498d8934d0c9d8057817eef7e677d1115acb5dbd2842c64d345d690d0092887  test_mpp_encoder
aa0572e347d9005ff908e0c3bd65dfe5bde40bbb3e749732ad5c132936381a70  capture_loop_pusher
```
