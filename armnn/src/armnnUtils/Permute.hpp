//
// Copyright © 2017 Arm Ltd. All rights reserved.
// SPDX-License-Identifier: MIT
//
#pragma once

#include <armnn/TensorFwd.hpp>
#include <armnn/Types.hpp>

namespace armnnUtils
{

armnn::TensorShape Permuted(const armnn::TensorShape& srcShape, const armnn::PermutationVector& mappings);

armnn::TensorInfo Permuted(const armnn::TensorInfo& info, const armnn::PermutationVector& mappings);

void Permute(const armnn::TensorShape& dstShape, const armnn::PermutationVector& mappings,
             const void* src, void* dst, size_t dataTypeSize);

} // namespace armnnUtils
