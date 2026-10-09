#include "render/vertex.hpp"

namespace grender {

namespace {

Palette& mutable_palette() noexcept {
    // Function-local static: immune to the static-initialisation-order race.
    static Palette p = Palette::builtin_dark();
    return p;
}

u64& mutable_revision() noexcept {
    static u64 r = 1;
    return r;
}

}

const Palette& palette() noexcept { return mutable_palette(); }

void set_palette(const Palette& p) noexcept {
    auto& cur = mutable_palette();
    // Moves only on a real change: drawing caches key on it.
    if (cur == p) return;
    cur = p;
    ++mutable_revision();
}

u64 palette_revision() noexcept { return mutable_revision(); }

}
