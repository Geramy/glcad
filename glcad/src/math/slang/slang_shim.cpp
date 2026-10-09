#include "slang_predicates.h"

// Included, not linked, so its static helpers stay internal.
#include "slang_generated.cpp"

namespace {

struct GlobalParams {
    StructuredBuffer<Orient2dInput_0>   in2;
    RWStructuredBuffer<int32_t>         out2;
    StructuredBuffer<Orient3dInput_0>   in3;
    RWStructuredBuffer<int32_t>         out3;
    StructuredBuffer<MulInput_0>        in_mul;
    RWStructuredBuffer<MulOutput_0>     out_mul;
};

}

void slang_orient2d_batch(const void* inputs, std::int32_t* out,
                          std::size_t count) {
    GlobalParams params{};
    params.in2.data = const_cast<Orient2dInput_0*>(
        static_cast<const Orient2dInput_0*>(inputs));
    params.in2.count = count;
    params.out2.data = out;
    params.out2.count = count;

    for (std::size_t i = 0; i < count; ++i) {
        ComputeThreadVaryingInput varying{};
        varying.groupID = {0, 0, 0};
        varying.groupThreadID = {static_cast<uint32_t>(i), 0, 0};
        orient2d_batch_Thread(&varying, nullptr, &params);
    }
}

void slang_orient3d_batch(const void* inputs, std::int32_t* out,
                          std::size_t count) {
    GlobalParams params{};
    params.in3.data = const_cast<Orient3dInput_0*>(
        static_cast<const Orient3dInput_0*>(inputs));
    params.in3.count = count;
    params.out3.data = out;
    params.out3.count = count;

    for (std::size_t i = 0; i < count; ++i) {
        ComputeThreadVaryingInput varying{};
        varying.groupID = {0, 0, 0};
        varying.groupThreadID = {static_cast<uint32_t>(i), 0, 0};
        orient3d_batch_Thread(&varying, nullptr, &params);
    }
}

void slang_mul_batch(const void* inputs, void* out, std::size_t count) {
    GlobalParams params{};
    params.in_mul.data =
        const_cast<MulInput_0*>(static_cast<const MulInput_0*>(inputs));
    params.in_mul.count = count;
    params.out_mul.data = static_cast<MulOutput_0*>(out);
    params.out_mul.count = count;

    for (std::size_t i = 0; i < count; ++i) {
        ComputeThreadVaryingInput varying{};
        varying.groupID = {0, 0, 0};
        varying.groupThreadID = {static_cast<uint32_t>(i), 0, 0};
        mul_batch_Thread(&varying, nullptr, &params);
    }
}

std::size_t slang_mul_input_size() { return sizeof(MulInput_0); }
std::size_t slang_mul_output_size() { return sizeof(MulOutput_0); }

std::size_t slang_orient2d_input_size() { return sizeof(Orient2dInput_0); }
std::size_t slang_orient3d_input_size() { return sizeof(Orient3dInput_0); }
