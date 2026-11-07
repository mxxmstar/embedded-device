#pragma once

#include "media/encoder/encoder.h"

extern "C" {
#include <rk_mpi.h>
#include <mpp_buffer.h>
#include <mpp_frame.h>
#include <mpp_packet.h>
#include <rk_venc_cfg.h>
}

#include <cstdint>
#include <vector>

/// @brief Rockchip MPP 编码器实现
class MppEncoder final : public IEncoder {
public:
    ~MppEncoder() override;
    /// @brief 打开编码器并应用配置，返回是否成功
    /// @param cfg 编码器配置
    /// @return 是否成功打开编码器
    bool Open(const EncoderConfig& cfg) override;

    /// @brief 编码一帧数据，frame==nullptr 表示刷新（flush）编码器
    /// @note frame为 cpu 内存的 MediaFrame
    /// @param frame 输入帧，若为 nullptr 则表示刷新编码器
    bool Encode(FramePtr frame, std::vector<PacketPtr>& packets) override;

    /// @brief 显式刷新编码器并返回所有残留 packet；Close 不负责向外输出 packet。
    /// @param packets 输出 packet 列表
    bool Flush(std::vector<PacketPtr>& packets) override;

    /// @brief 查询当前编码会话的实际输出描述。
    /// @return 编码后的视频或音频轨道信息
    EncodedTrackInfo GetOutputInfo() const override;

    /// @brief 关闭编码器，释放所有资源
    void Close() override;
private:
    bool fail(const char* message);
    bool check(MPP_RET ret, const char* operation);
    bool openFailed();

    /// @brief 从编码器中提取所有残留 packet, 聚合为一个 access unit
    /// @param packets 输出 packet 列表
    /// @param eos 是否为结束标志
    /// @param pts 时间戳
    /// @param duration 视频时长
    /// @return 是否成功提取所有残留 packet
    bool drain(std::vector<PacketPtr>& packets, bool eos, int64_t pts, int64_t duration);
    MppCtx ctx_{};
    MppApi* api_{};
    MppEncCfg enc_cfg_{};
    MppBufferGroup group_{};
    MppBuffer buffer_{};
    EncoderConfig config_{};
    Rational time_base_{};
    std::vector<uint8_t> extra_data_;
    int stride_{};
    int rows_{};
    int64_t next_pts_{}, nominal_us_{};
    bool opened_{}, flushed_{}, failed_{};
};
