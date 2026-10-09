#include "test.hpp"

#include <atomic>

#include "math/thread.hpp"

namespace {
// ~1 KB per frame: 2000 frames is ~2 MB, four times the macOS thread default.
int burn(int depth, volatile char* prev) {
    volatile char frame[1024];
    frame[0] = prev ? prev[0] : 0;
    frame[1023] = char(depth);
    if (depth == 0) return frame[0] + frame[1023];
    return burn(depth - 1, frame) + 1;
}
}

TEST(big_stack_thread_survives_recursion_std_thread_cannot) {
    std::atomic<int> result{-1};
    gcm::BigStackThread t([&] { result = burn(2000, nullptr); });
    CHECK(t.joinable());
    t.join();
    CHECK(!t.joinable());
    CHECK_MSG(result.load() >= 2000, "the deep recursion did not finish (%d)",
              result.load());

    gcm::BigStackThread a([&] { result = 1; });
    gcm::BigStackThread b = std::move(a);
    CHECK(!a.joinable());
    b.join();
    b = gcm::BigStackThread([&] { result = 2; });
    b.join();
    CHECK(result.load() == 2);
}
