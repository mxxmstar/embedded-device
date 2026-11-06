#include "media/media_timestamp.h"
#include <limits>

std::int64_t RescaleMediaTimestamp(std::int64_t value, Rational source, Rational target) noexcept {
    if (!IsValidTimestamp(value) || !IsValidTimeBase(source) || !IsValidTimeBase(target)) return kNoTimestamp;
    // 先取绝对值转为无符号参与运算，符号留到最后统一还原，避免负数取整方向与溢出的歧义。
    const auto a = static_cast<std::uint64_t>(value < 0 ? -value : value);
    // 换算比例 (source.num/source.den) / (target.num/target.den) 通分后为 b/d，
    // 因此只需计算 a * b / d。
    const auto b = static_cast<std::uint64_t>(source.num) * target.den;
    const auto d = static_cast<std::uint64_t>(source.den) * target.num;
    // 可移植的 128 位乘法 a * b：结果拆成 hi:lo 两段保存，不依赖编译器的 __int128 扩展。
    std::uint64_t hi = 0, lo = 0, xhi = 0, xlo = a;
    for (auto bits = b; bits; bits >>= 1) {
        // b 的当前位为 1 时把 a 的对应移位值累加进 hi:lo；lo 回绕说明低位溢出，需向 hi 补进位。
        if (bits & 1) { const auto before = lo; lo += xlo; hi += xhi + (lo < before); }
        // 被加数 x 左移一位，跨越 64 位边界时把 xlo 的最高位带进 xhi。
        xhi = (xhi << 1) | (xlo >> 63); xlo <<= 1;
    }
    // 128 位 ÷ 64 位的恢复余数除法：从最高位起逐位试商，得到商 quotient 与余数 remainder。
    std::uint64_t remainder = 0, quotient = 0;
    for (int bit = 127; bit >= 0; --bit) {
        // 取出被除数的第 bit 位（bit >= 64 落在 hi，否则落在 lo）补到余数末尾。
        remainder = (remainder << 1) | ((bit >= 64 ? hi >> (bit - 64) : lo >> bit) & 1);
        if (remainder >= d) {
            remainder -= d;
            // 商落在第 63 位说明结果 >= 2^63，已超出 int64_t 量程。
            if (bit >= 63) return kNoTimestamp;
            quotient |= std::uint64_t{1} << bit;
        }
    }
    // (d+1)/2 是 d/2 的向上取整：余数达到半个除数即进位，配合绝对值运算实现“远离 0”的舍入。
    if (remainder >= (d + 1) / 2) {
        ++quotient;
    }
    // 取绝对值后重新检查一次，防止舍入进位把结果推过 int64_t 上界。
    if (quotient > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)())) return kNoTimestamp;
    // 还原符号：负数取相反数，正数原样返回。
    return value < 0 ? -static_cast<std::int64_t>(quotient) : static_cast<std::int64_t>(quotient);
}