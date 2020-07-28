//
// Copyright © 2017 Arm Ltd. All rights reserved.
// SPDX-License-Identifier: MIT
//

#include "NeonSoftmaxBaseWorkload.hpp"

#include <aclCommon/ArmComputeTensorUtils.hpp>

#include <arm_compute/runtime/NEON/functions/NESoftmaxLayer.h>

namespace armnn
{

arm_compute::Status NeonSoftmaxWorkloadValidate(const TensorInfo& input,
                                                const TensorInfo& output,
                                                const SoftmaxDescriptor& descriptor)
{
    const arm_compute::TensorInfo aclInputInfo = armcomputetensorutils::BuildArmComputeTensorInfo(input);
    const arm_compute::TensorInfo aclOutputInfo = armcomputetensorutils::BuildArmComputeTensorInfo(output);

    return arm_compute::NESoftmaxLayer::validate(&aclInputInfo, &aclOutputInfo, descriptor.m_Beta);
}

} //namespace armnn

