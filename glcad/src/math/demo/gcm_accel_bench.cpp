#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

#include "math/cpu.hpp"
#include "math/scalar.hpp"

#if defined(__APPLE__)
#  include <Accelerate/Accelerate.h>
#  define HAVE_ACCELERATE 1
#endif

using namespace gcm;

static void rule(const char* t) {
    std::printf("\n\033[1m%s\033[0m\n", t);
    std::printf("--------------------------------------------------------------\n");
}

static void dgemm_naive(int n, const double* A, const double* B, double* C) {
    for (int i = 0; i < n; ++i)
        for (int k = 0; k < n; ++k) {
            const double a = A[i * n + k];
            for (int j = 0; j < n; ++j) C[i * n + j] += a * B[k * n + j];
        }
}

int main() {
    const CpuInfo cpu = probe_cpu();

    std::printf("\n\033[1mGLCAD - CPU matrix accelerator\033[0m\n");
    std::printf("docs/11-cpu-matrix-acceleration.md\n");

    rule("1. What this CPU has");
    std::printf("  %s\n", cpu.brand.c_str());
    std::printf("  matrix unit        %s\n", cpu.accel_name());
    std::printf("  SME / SME2         %s / %s\n", cpu.sme ? "yes" : "no",
                cpu.sme2 ? "yes" : "no");
    std::printf("  FEAT_SME_F64F64    %s%s\033[0m   <- native fp64 matrix ops\n",
                cpu.sme_f64f64 ? "\033[32m" : "\033[31m",
                cpu.sme_f64f64 ? "yes" : "no");
    std::printf("  streaming vector   %u bytes (%u fp64 lanes)\n",
                cpu.sme_vector_bytes, cpu.f64_lanes());
    std::printf("  cores              %u performance + %u efficiency\n",
                cpu.perf_cores, cpu.efficiency_cores);

    rule("2. fp64 GEMM: finding the crossover");
#ifdef HAVE_ACCELERATE
    {
        // Warm up: Accelerate starts a thread pool on first use.
        {
            std::vector<double> w(64 * 64, 1.0), wc(64 * 64, 0.0);
            for (int i = 0; i < 20; ++i)
                cblas_dgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans, 64, 64, 64,
                            1.0, w.data(), 64, w.data(), 64, 0.0, wc.data(), 64);
        }

        auto best_ms = [](auto&& fn, int reps) {
            double best = 1e30;
            for (int r = 0; r < reps; ++r) {
                auto t0 = std::chrono::steady_clock::now();
                fn();
                auto t1 = std::chrono::steady_clock::now();
                best = std::min(best,
                    std::chrono::duration<double, std::milli>(t1 - t0).count());
            }
            return best;
        };

        std::printf("  best-of-N, after warm-up.  CAD constraint systems live at "
                    "the small end.\n\n");
        std::printf("     n     scalar ms   GFLOP/s      SME ms   GFLOP/s   speedup\n");

        int crossover = 0;
        for (int n : {16, 32, 48, 64, 96, 128, 192, 256, 512, 1024}) {
            std::vector<double> A(size_t(n) * n), B(size_t(n) * n),
                                C1(size_t(n) * n), C2(size_t(n) * n);
            std::mt19937_64 rng(1);
            std::uniform_real_distribution<double> d(-1.0, 1.0);
            for (auto& v : A) v = d(rng);
            for (auto& v : B) v = d(rng);

            const int reps = n <= 128 ? 200 : (n <= 512 ? 20 : 5);

            const double ms_naive = best_ms([&] {
                std::fill(C1.begin(), C1.end(), 0.0);
                dgemm_naive(n, A.data(), B.data(), C1.data());
            }, reps);

            const double ms_accel = best_ms([&] {
                cblas_dgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans, n, n, n,
                            1.0, A.data(), n, B.data(), n, 0.0, C2.data(), n);
            }, reps);

            const double flops = 2.0 * n * n * n;
            const double speed = ms_naive / ms_accel;
            if (!crossover && speed > 1.0) crossover = n;

            std::printf("  %5d   %9.3f   %7.1f   %9.3f   %7.1f   %s%5.1fx\033[0m\n",
                        n, ms_naive, flops / ms_naive / 1e6,
                        ms_accel, flops / ms_accel / 1e6,
                        speed >= 1.0 ? "\033[32m" : "\033[31m", speed);
        }
        std::printf("\n  \033[1mCrossover at roughly n = %d.\033[0m\n", crossover);
        std::printf("  Below it the fixed cost of entering the matrix unit exceeds\n");
        std::printf("  the work. This is the number that decides whether the\n");
        std::printf("  constraint solver benefits -- see section 3.\n");
    }

    rule("2b. fp64 elementwise vectors (vDSP)");
    {
        // Elementwise fp64 has a far lower entry cost than GEMM.
        for (int n : {1024, 65536, 1048576}) {
            std::vector<double> a(n), b(n), c(n);
            std::mt19937_64 rng(2);
            std::uniform_real_distribution<double> d(-1.0, 1.0);
            for (auto& v : a) v = d(rng);
            for (auto& v : b) v = d(rng);

            const int reps = n <= 65536 ? 500 : 50;
            double best_scalar = 1e30, best_vdsp = 1e30;
            for (int r = 0; r < reps; ++r) {
                auto t0 = std::chrono::steady_clock::now();
                for (int i = 0; i < n; ++i) c[i] = a[i] * b[i];
                auto t1 = std::chrono::steady_clock::now();
                vDSP_vmulD(a.data(), 1, b.data(), 1, c.data(), 1, vDSP_Length(n));
                auto t2 = std::chrono::steady_clock::now();
                best_scalar = std::min(best_scalar,
                    std::chrono::duration<double, std::milli>(t1 - t0).count());
                best_vdsp = std::min(best_vdsp,
                    std::chrono::duration<double, std::milli>(t2 - t1).count());
            }
            std::printf("  vmulD n=%-8d  scalar %7.4f ms   vDSP %7.4f ms   %5.2fx\n",
                        n, best_scalar, best_vdsp, best_scalar / best_vdsp);
        }
    }
#else
    std::printf("  Accelerate unavailable on this platform.\n");
#endif

    rule("3. Where this is worth using in GLCAD");
    std::printf("  YES - linear-algebra shaped, native fp64, above the n=16 crossover:\n");
    std::printf("    - constraint solver: LM/dogleg normal equations, Cholesky/QR  (09 §9)\n");
    std::printf("      Sketches are 20-200 DOF: comfortably past the crossover.\n");
    std::printf("    - NURBS least-squares fitting of intersection points          (09 §6.3.5)\n");
    std::printf("    - mass properties, inertia tensors\n");
    std::printf("    - batched assembly transform composition\n");
    std::printf("    - the CPU reference oracle in the determinism gate            (08 §6)\n\n");
    std::printf("  NO - wrong shape, or already on the GPU:\n");
    std::printf("    - orient2d/orient3d: 3x3 determinants; tile setup dwarfs the work\n");
    std::printf("    - BVH build and traversal: pointer-chasing and branchy\n");
    std::printf("    - per-point Newton refinement: scalar and independent\n");
    std::printf("    - the exact tier: integer multiprecision, not matrix math\n");

    rule("4. Why this does NOT replace DF64 on the GPU");
    std::printf("  SME sits next to the CPU cores. A Vulkan compute shader cannot\n");
    std::printf("  reach it -- there is no path from a dispatch to SME. The GPU still\n");
    std::printf("  has no fp64, so every GPU kernel still uses DF64 (docs/09 §3).\n\n");
    std::printf("  And Accelerate's blocking and threading vary by chip and OS build,\n");
    std::printf("  so its results are not bit-reproducible across machines. Under the\n");
    std::printf("  determinism policy (08 §6) SME output is therefore Display-class\n");
    std::printf("  unless it is snapped to the lattice, which absorbs the difference\n");
    std::printf("  exactly as it absorbs DF64 vs F64 (09 §3.5).\n\n");
    return 0;
}
