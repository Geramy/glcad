#pragma once

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace t {

// GLCAD_TEST_SCALE: 0.02 fast, 1 full, 20 soak; counts written at full volume.
double scale();
std::size_t iters(std::size_t full_count);

struct Case {
    const char* name;
    void (*fn)();
};

std::vector<Case>& cases();
int& failures();
long long& checks();

inline void register_case(const char* name, void (*fn)()) {
    cases().push_back({name, fn});
}

struct Reg {
    Reg(const char* n, void (*f)()) { register_case(n, f); }
};

}

#define TEST(name)                                                             \
    static void name();                                                        \
    static ::t::Reg t_reg_##name(#name, name);                                 \
    static void name()

#define CHECK(cond)                                                            \
    do {                                                                       \
        ++::t::checks();                                                       \
        if (!(cond)) {                                                         \
            ++::t::failures();                                                 \
            std::printf("      FAIL  %s:%d   %s\n", __FILE__, __LINE__, #cond);\
        }                                                                      \
    } while (0)

#define CHECK_MSG(cond, ...)                                                   \
    do {                                                                       \
        ++::t::checks();                                                       \
        if (!(cond)) {                                                         \
            ++::t::failures();                                                 \
            std::printf("      FAIL  %s:%d   ", __FILE__, __LINE__);           \
            std::printf(__VA_ARGS__);                                          \
            std::printf("\n");                                                 \
        }                                                                      \
    } while (0)
