#pragma once

#include <spirv_cross_containers.hpp>

// We need a bunch of temporary vectors for storing graphics pipeline information
// e.g. descriptor set layouts and such. The implementation from spirv should do for now.

template <class T, size_t N = 8>
using SmallVector = spirv_cross::SmallVector<T, N>;

template <class T>
using VectorView = spirv_cross::VectorView<T>;
