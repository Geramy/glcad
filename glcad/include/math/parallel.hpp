#pragma once

#include <algorithm>
#include <cstddef>
#include <thread>
#include <vector>

namespace gcm {

inline unsigned worker_count() noexcept {
    const unsigned hw = std::thread::hardware_concurrency();
    return std::max(1u, std::min(16u, hw ? hw : 1u));
}

// fn(lo, hi, thread) over [0, n); below min_per_thread items it runs inline.
template <class Fn>
void parallel_for(std::size_t n, std::size_t min_per_thread, Fn&& fn) {
    if (n == 0) return;
    unsigned threads = worker_count();
    if (min_per_thread) threads = static_cast<unsigned>(std::min<std::size_t>(threads, std::max<std::size_t>(1, n / min_per_thread)));
    if (threads <= 1) { fn(std::size_t{0}, n, 0u); return; }
    const std::size_t chunk = (n + threads - 1) / threads;
    std::vector<std::thread> workers;
    workers.reserve(threads - 1);
    for (unsigned t = 1; t < threads; ++t) {
        const std::size_t lo = t * chunk, hi = std::min(n, lo + chunk);
        if (lo >= hi) break;
        workers.emplace_back([&fn, lo, hi, t] { fn(lo, hi, t); });
    }
    fn(std::size_t{0}, std::min(n, chunk), 0u);
    for (auto& w : workers) w.join();
}

}
