#pragma once

#include <cstddef>
#include <cstdint>

void slang_orient2d_batch(const void* inputs, std::int32_t* out,
                          std::size_t count);
void slang_orient3d_batch(const void* inputs, std::int32_t* out,
                          std::size_t count);

std::size_t slang_orient2d_input_size();
std::size_t slang_orient3d_input_size();

void slang_mul_batch(const void* inputs, void* out, std::size_t count);
std::size_t slang_mul_input_size();
std::size_t slang_mul_output_size();
