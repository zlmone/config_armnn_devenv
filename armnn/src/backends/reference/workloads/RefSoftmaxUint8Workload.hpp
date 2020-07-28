﻿//
// Copyright © 2017 Arm Ltd. All rights reserved.
// SPDX-License-Identifier: MIT
//

#pragma once

#include <backendsCommon/Workload.hpp>
#include <backendsCommon/WorkloadData.hpp>

namespace armnn
{

class RefSoftmaxUint8Workload : public Uint8Workload<SoftmaxQueueDescriptor>
{
public:
    using Uint8Workload<SoftmaxQueueDescriptor>::Uint8Workload;
    virtual void Execute() const override;
};

} //namespace armnn
