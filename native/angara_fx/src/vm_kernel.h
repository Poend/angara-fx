#pragma once
#include <array>
#include <cstdint>
namespace nbvm {
struct KernelRead {
    const uint32_t *data = nullptr;
    bool broadcast = false;
    uint32_t at(int lane) const {
        return data[broadcast ? 0 : lane];
    }
};
using ScalarKernel = void (*)(const std::array<KernelRead, 5> &, const std::array<uint32_t *, 2> &, int);
} // namespace nbvm
