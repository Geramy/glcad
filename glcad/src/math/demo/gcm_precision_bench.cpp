#include <chrono>
#include <cstdio>
#include <random>
#include <vector>

#include "math/predicate.hpp"
#include "math/scalar.hpp"
#include "math/soft_f64.hpp"

using namespace gcm;

static void rule(const char* t) {
    std::printf("\n\033[1m%s\033[0m\n", t);
    std::printf("--------------------------------------------------------------\n");
}

template <Scalar S>
static f64 newton_chain(const std::vector<f64>& in, int iters) {
    S acc = S::from(0.0);
    const S half = S::from(0.5);
    for (f64 v : in) {
        S x = S::from(v);
        for (int i = 0; i < iters; ++i) {
            // x = (x + v/x) / 2   -- Newton step for sqrt
            const S q = S::from(v) / x;
            x = (x + q) * half;
        }
        acc += x;
    }
    return acc.to_f64();
}

template <Scalar S>
static f64 fma_chain(const std::vector<f64>& in) {
    S acc = S::from(0.0);
    const S c = S::from(1.0000001);
    for (f64 v : in) {
        const S x = S::from(v);
        acc = acc + x * c;
    }
    return acc.to_f64();
}

template <class Fn>
static f64 best_ms(Fn&& fn, int reps) {
    f64 best = 1e30;
    for (int r = 0; r < reps; ++r) {
        const auto t0 = std::chrono::steady_clock::now();
        volatile f64 sink = fn();
        (void)sink;
        const auto t1 = std::chrono::steady_clock::now();
        const f64 ms = std::chrono::duration<f64, std::milli>(t1 - t0).count();
        if (ms < best) best = ms;
    }
    return best;
}

int main() {
    std::printf("\n\033[1mGLCAD - precision cost\033[0m\n");
    std::printf("docs/09-brep-kernel.md §3\n");

    std::mt19937_64 rng(1);
    std::uniform_real_distribution<f64> d(1.0, 100.0);
    std::vector<f64> data(200000);
    for (auto& v : data) v = d(rng);

    rule("1. Multiply-accumulate chain (200k values)");
    {
        const f64 f32ms  = best_ms([&] { return fma_chain<F32>(data); }, 20);
        const f64 f64ms  = best_ms([&] { return fma_chain<F64>(data); }, 20);
        const f64 dfms   = best_ms([&] { return fma_chain<DF64>(data); }, 20);
        const f64 softms = best_ms([&] { return fma_chain<SoftF64>(data); }, 5);

        std::printf("     type      bits   time(ms)   ns/op   vs F32   vs F64\n");
        auto row = [&](const char* n, int bits, f64 ms) {
            std::printf("  %-9s %6d   %8.3f  %6.2f   %6.1fx  %6.1fx\n", n, bits, ms,
                        ms * 1e6 / data.size(), ms / f32ms, ms / f64ms);
        };
        row("F32", 24, f32ms);
        row("F64", 53, f64ms);
        row("DF64", 48, dfms);
        row("SoftF64", 53, softms);
    }

    rule("2. Newton refinement, 4 iterations (20k values, div-heavy)");
    {
        std::vector<f64> small(data.begin(), data.begin() + 20000);
        const f64 f32ms  = best_ms([&] { return newton_chain<F32>(small, 4); }, 10);
        const f64 f64ms  = best_ms([&] { return newton_chain<F64>(small, 4); }, 10);
        const f64 dfms   = best_ms([&] { return newton_chain<DF64>(small, 4); }, 10);
        const f64 softms = best_ms([&] { return newton_chain<SoftF64>(small, 4); }, 3);

        std::printf("     type      bits   time(ms)   vs F32   vs F64\n");
        auto row = [&](const char* n, int bits, f64 ms) {
            std::printf("  %-9s %6d   %8.3f   %6.1fx  %6.1fx\n", n, bits, ms,
                        ms / f32ms, ms / f64ms);
        };
        row("F32", 24, f32ms);
        row("F64", 53, f64ms);
        row("DF64", 48, dfms);
        row("SoftF64", 53, softms);
    }

    rule("3. orient2d predicate throughput, by filter tier");
    {
        std::mt19937_64 r2(7);
        std::uniform_int_distribution<i64> dist(-(i64{1} << 32), i64{1} << 32);
        const int n = 500000;
        std::vector<Lattice2> pts(3 * n);
        for (auto& p : pts) p = Lattice2{dist(r2), dist(r2)};

        auto bench = [&](auto tag, const char* name) {
            using P = decltype(tag);
            predicate::stats().reset();
            const auto t0 = std::chrono::steady_clock::now();
            volatile int sink = 0;
            for (int i = 0; i < n; ++i)
                sink += to_int(P::orient2d(pts[3*i], pts[3*i+1], pts[3*i+2]));
            const auto t1 = std::chrono::steady_clock::now();
            (void)sink;
            const f64 ms = std::chrono::duration<f64, std::milli>(t1 - t0).count();
            std::printf("  %-26s %8.2f ms   %6.1f M/s   exact fallbacks %llu\n",
                        name, ms, n / ms / 1000.0,
                        (unsigned long long)predicate::stats().exact_fallbacks);
        };
        bench(predicate::Predicates<F64>{},     "Predicates<F64>    (CPU)");
        bench(predicate::Predicates<DF64>{},    "Predicates<DF64>   (Apple GPU)");
        bench(predicate::Predicates<SoftF64>{}, "Predicates<SoftF64>(bit-exact)");
    }

    rule("4. Reading this");
    std::printf("  DF64 is the GPU default: ~48 bits, cheap enough to be the\n");
    std::printf("  everyday scalar on hardware with no fp64 at all.\n\n");
    std::printf("  SoftF64 buys something DF64 cannot: results BIT-IDENTICAL to\n");
    std::printf("  hardware f64. That is what lets an Authoritative workload move\n");
    std::printf("  between CPU and GPU without changing its answer -- see the\n");
    std::printf("  scheduler in docs/12.\n\n");
    return 0;
}
