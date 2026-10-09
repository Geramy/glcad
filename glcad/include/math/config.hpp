#pragma once

#include <cstdint>

namespace gcm {

using i8  = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;

using u8  = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

using f32 = float;
using f64 = double;

enum class Sign : int { Negative = -1, Zero = 0, Positive = 1 };

constexpr Sign sign_of(i32 v) noexcept {
    return v < 0 ? Sign::Negative : (v > 0 ? Sign::Positive : Sign::Zero);
}
constexpr Sign sign_of(i64 v) noexcept {
    return v < 0 ? Sign::Negative : (v > 0 ? Sign::Positive : Sign::Zero);
}
constexpr int to_int(Sign s) noexcept { return static_cast<int>(s); }

enum class Cls { Display, Authoritative };

}
