#pragma once

#include "math/config.hpp"
#include "math/fixed.hpp"
#include "math/scalar.hpp"

namespace ggeo {

using gcm::Scalar;
using gcm::f32;
using gcm::f64;
using gcm::i32;
using gcm::i64;
using gcm::u8;
using gcm::u16;
using gcm::u32;
using gcm::u64;

struct Dim2 { static constexpr int value = 2; };
struct Dim3 { static constexpr int value = 3; };

template <class T>
concept SpaceTraits = requires {
    typename T::Dim;
    typename T::template Vec<gcm::F64>;
    typename T::template Point<gcm::F64>;
    { T::dimension } -> std::convertible_to<int>;
};

enum class Side : i32 { Outside = -1, On = 0, Inside = 1 };

}
