#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>

#include "math/fixed.hpp"
#include "math/platform.hpp"
#include "math/predicate.hpp"
#include "math/scalar.hpp"

using namespace gcm;

static void rule(const char* title) {
    std::printf("\n\033[1m%s\033[0m\n", title);
    std::printf("--------------------------------------------------------------\n");
}

static const char* sign_name(Sign s) {
    switch (s) {
        case Sign::Negative: return "CW      (negative)";
        case Sign::Positive: return "CCW     (positive)";
        default:             return "COLLINEAR (zero)";
    }
}

template <class P>
static void report_platform(const char* name) {
    using T = PlatformTraits<P>;
    std::printf("  %-22s f64:%-5s int64:%-5s atomic64:%-5s scalar:%-5s bits:%2d\n",
                name,
                T::has_native_f64 ? "yes" : "NO",
                T::has_int64 ? "yes" : "no",
                T::has_atomic_i64 ? "yes" : "NO",
                std::is_same_v<typename T::PreferredScalar, DF64> ? "DF64" : "F64",
                T::PreferredScalar::mantissa_bits);
}

int main() {
    std::printf("\n\033[1mGLCAD - gcm numeric foundation\033[0m\n");
    std::printf("docs/09-brep-kernel.md  |  docs/10-numerics-and-platform-abstraction.md\n");

    rule("1. Platform traits - one algorithm, adapted at compile time");
    report_platform<AppleM>("Apple M-series GPU");
    report_platform<AppleA>("Apple A-series GPU");
    report_platform<DiscreteConsumer>("Consumer discrete");
    report_platform<DiscreteWorkstation>("Workstation");
    report_platform<Cpu>("CPU reference");
    std::printf("\n  Apple M-series has NO f64 -> DF64 carries 48 bits from f32 hardware.\n");
    std::printf("  It also has no 64-bit atomics -> reductions must be prefix-sum.\n");

    rule("2. DF64 recovers precision that f32 cannot hold");
    {
        const int n = 1000000;
        const f64 want = 0.1 * static_cast<f64>(n);

        f32 acc32 = 0.0f;
        for (int i = 0; i < n; ++i) acc32 += 0.1f;

        DF64 accdf = DF64::from(0.0);
        const DF64 tenth = DF64::from(0.1);
        for (int i = 0; i < n; ++i) accdf += tenth;

        std::printf("  Accumulating 0.1 one million times:\n");
        std::printf("    exact           %.6f\n", want);
        std::printf("    f32             %.6f   (error %.3e)\n",
                    static_cast<f64>(acc32), std::fabs(static_cast<f64>(acc32) - want));
        std::printf("    DF64 (2x f32)   %.6f   (error %.3e)\n",
                    accdf.to_f64(), std::fabs(accdf.to_f64() - want));
    }

    rule("3. Exact predicates - where every tolerance-based kernel breaks");
    {
        // Consecutive Fibonacci: det is exactly +1 (Cassini); products are ~2^63.
        const Lattice2 a{0, 0};
        const Lattice2 b{2971215073LL, 1836311903LL};
        const Lattice2 c{4807526976LL, 2971215073LL};

        std::printf("  Triangle with true signed area = +1, coordinates ~2^32,\n");
        std::printf("  intermediate products ~2^63 (one f64 ulp there is ~1024):\n\n");
        std::printf("    naive f32       %s   <-- WRONG\n", sign_name(predicate::orient2d_naive_f32(a, b, c)));
        std::printf("    naive f64       %s   <-- WRONG\n", sign_name(predicate::orient2d_naive_f64(a, b, c)));
        std::printf("    gcm::orient2d   %s   <-- correct\n", sign_name(predicate::orient2d(a, b, c)));
        std::printf("\n  A boolean operation hits this whenever two faces are nearly\n");
        std::printf("  tangent. Getting the sign wrong is how kernels emit\n");
        std::printf("  self-intersecting geometry and fail to close a loop.\n");
    }

    rule("4. The filter carries the load; exact arithmetic is rare");
    {
        std::mt19937_64 rng(7);
        std::uniform_int_distribution<i64> dist(-(i64{1} << 32), i64{1} << 32);
        const int n = 2000000;

        predicate::stats().reset();
        auto t0 = std::chrono::steady_clock::now();
        volatile int sink = 0;
        for (int i = 0; i < n; ++i) {
            const Lattice2 a{dist(rng), dist(rng)};
            const Lattice2 b{dist(rng), dist(rng)};
            const Lattice2 c{dist(rng), dist(rng)};
            sink += to_int(predicate::orient2d(a, b, c));
        }
        auto t1 = std::chrono::steady_clock::now();
        const f64 ms = std::chrono::duration<f64, std::milli>(t1 - t0).count();
        (void)sink;

        std::printf("  %d random orient2d calls in %.1f ms  (%.1f M/s)\n",
                    n, ms, static_cast<f64>(n) / ms / 1000.0);
        std::printf("  exact-tier fallbacks: %llu  (%.4f%%)\n",
                    static_cast<unsigned long long>(predicate::stats().exact_fallbacks),
                    predicate::stats().fallback_rate() * 100.0);
        std::printf("\n  On the GPU the uncertain cases are stream-compacted into their\n");
        std::printf("  own dispatch, so divergence becomes a separate kernel launch\n");
        std::printf("  over a dense array rather than a stalled wavefront.\n");
    }

    rule("5. The lattice equalizer - why dual precision is safe");
    {
        WorkingVolume v;
        std::printf("  Working volume  +/- %.0f m,  %d-bit lattice\n", v.extent, v.resolution_bits);
        std::printf("  Half cell       %.3e m   (snap-rounding perturbation bound)\n", v.half_cell());

        std::mt19937_64 rng(21);
        std::uniform_real_distribution<f64> dist(-5000.0, 5000.0);
        int disagreements = 0;
        const int n = 500000;

        for (int i = 0; i < n; ++i) {
            const f64 truth = dist(rng);
            const f64 as_df64 = DF64::from(truth).to_f64();   // ~48 bits
            const f64 as_f64  = F64::from(truth).to_f64();    // 53 bits
            if (to_lattice(v, as_df64, 0.0).x != to_lattice(v, as_f64, 0.0).x)
                ++disagreements;
        }

        std::printf("\n  %d points refined at 48-bit (Apple) and 53-bit (workstation)\n", n);
        std::printf("  precision, then snapped to the lattice:\n");
        std::printf("    lattice disagreements: %d\n", disagreements);
        std::printf("\n  Topology never touches floats -- predicates are exact integers.\n");
        std::printf("  So the precision backend cannot change what geometry IS,\n");
        std::printf("  only how it was arrived at. That is what makes shipping both\n");
        std::printf("  DF64 and F64 a performance choice rather than a data-integrity bug.\n");
    }

    std::printf("\n");
    return 0;
}
