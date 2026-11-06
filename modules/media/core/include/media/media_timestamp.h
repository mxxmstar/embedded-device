#pragma once
#include <cstdint>
#include <limits>
#include <string>

/// @brief 有理分数，用于表示时间基等
struct Rational {
    int num{1};
    int den{1};
    std::string toString() const {
        return std::to_string(num) + "/" + std::to_string(den);
    }
};

/// @brief 时间戳
struct MediaTime {
    int64_t pts_us{0};   ///< 显示时间戳（微秒）
    int64_t dts_us{0};   ///< 解码时间戳（微秒）
    int64_t duration_us{0}; ///< 持续时间（微秒）
};

/// @brief 统一表示“没有时间戳”的值。
///
/// 0 是合法的媒体时间点（例如第一帧从 0 开始），因此不能再用 0 表示
/// 时间戳缺失。该值与 FFmpeg 的 AV_NOPTS_VALUE 数值一致，但公共头文件
/// 不依赖 FFmpeg 头文件。
inline constexpr std::int64_t kNoTimestamp = (std::numeric_limits<std::int64_t>::min)();

/// @brief 判断时间戳是否有效。
inline constexpr bool IsValidTimestamp(std::int64_t timestamp) {
    return timestamp != kNoTimestamp;
}

/// @brief 判断时间基是否可以用于整数时间戳换算。
inline constexpr bool IsValidTimeBase(const Rational& value) {    
    return value.num > 0 && value.den > 0;
}

/// @brief 将时间戳从 source 时间基转换为 target 时间基。
/// @note 舍入方式为四舍五入（余数恰为一半时向远离 0 的方向进位）；输入非法或结果溢出返回 kNoTimestamp。
/// @param value 时间戳值
/// @param source 时间戳原始时间基
/// @param target 目标时间基
/// @return 转换后的时间戳值
std::int64_t RescaleMediaTimestamp(std::int64_t value, Rational source, Rational target) noexcept;
