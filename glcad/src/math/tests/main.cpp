#include "test.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <string>

namespace t {

std::vector<Case>& cases() {
    static std::vector<Case> c;
    return c;
}
int& failures() {
    static int f = 0;
    return f;
}
long long& checks() {
    static long long c = 0;
    return c;
}

double scale() {
    static const double s = [] {
        if (const char* e = std::getenv("GLCAD_TEST_SCALE")) {
            const double v = std::atof(e);
            if (v > 0.0) return v;
        }
        return 1.0;
    }();
    return s;
}

std::size_t iters(std::size_t full_count) {
    const double n = static_cast<double>(full_count) * scale();
    return n < 1.0 ? std::size_t{1} : static_cast<std::size_t>(n);
}

}

int main() {
    std::printf("gcm test suite  (%zu cases, scale %.3g)\n", t::cases().size(),
                t::scale());
    std::printf("--------------------------------------------------\n");

    struct Timed { const char* name; double ms; };
    std::vector<Timed> timings;
    const auto suite_start = std::chrono::steady_clock::now();

    const char* only = std::getenv("GLCAD_TEST_ONLY");
    for (const auto& c : t::cases()) {
        if (only && only[0] && std::string(c.name).find(only) == std::string::npos)
            continue;
        const int before = t::failures();
        std::printf("  %-44s", c.name);
        std::fflush(stdout);
        const auto t0 = std::chrono::steady_clock::now();
        c.fn();
        const auto t1 = std::chrono::steady_clock::now();
        const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        timings.push_back({c.name, ms});
        std::printf("%-7s %7.1f ms\n", t::failures() == before ? "ok" : "FAILED", ms);
    }

    const double total =
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - suite_start).count();

    std::printf("--------------------------------------------------\n");
    std::printf("%lld checks, %d failures, %.0f ms\n", t::checks(), t::failures(),
                total);

    std::sort(timings.begin(), timings.end(),
              [](const Timed& a, const Timed& b) { return a.ms > b.ms; });
    const std::size_t n = std::min<std::size_t>(3, timings.size());
    if (n && timings[0].ms > 1.0) {
        std::printf("slowest: ");
        for (std::size_t i = 0; i < n; ++i)
            std::printf("%s%s (%.0f ms)", i ? ", " : "", timings[i].name, timings[i].ms);
        std::printf("\n");
    }
    return t::failures() == 0 ? 0 : 1;
}
