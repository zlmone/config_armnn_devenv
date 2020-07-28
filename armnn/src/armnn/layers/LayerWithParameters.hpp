//
// Copyright © 2017 Arm Ltd. All rights reserved.
// SPDX-License-Identifier: MIT
//
#pragma once

#include <Layer.hpp>

namespace armnn
{

template <typename Parameters>
class LayerWithParameters : public Layer
{
public:
    using DescriptorType = Parameters;

    const Parameters& GetParameters() const { return m_Param; }

    /// Helper to serialize the layer parameters to string
    /// (currently used in DotSerializer and company).
    void SerializeLayerParameters(ParameterStringifyFunction & fn) const
    {
        StringifyLayerParameters<Parameters>::Serialize(fn, m_Param);
    }

protected:
    LayerWithParameters(unsigned int numInputSlots,
                        unsigned int numOutputSlots,
                        LayerType type,
                        const Parameters& param,
                        const char* name)
        : Layer(numInputSlots, numOutputSlots, type, name)
        , m_Param(param)
    {
    }

    ~LayerWithParameters() = default;

    /// Helper function to reduce duplication in *Layer::CreateWorkload.
    template <typename QueueDescriptor>
    WorkloadInfo PrepInfoAndDesc(QueueDescriptor& descriptor, const Graph& graph) const
    {
        descriptor.m_Parameters = m_Param;
        return Layer::PrepInfoAndDesc(descriptor, graph);
    }

    /// The parameters for the layer (not including tensor-valued weights etc.).
    Parameters m_Param;
};

} // namespace
