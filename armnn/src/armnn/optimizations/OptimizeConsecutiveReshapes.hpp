//
// Copyright © 2017 Arm Ltd. All rights reserved.
// SPDX-License-Identifier: MIT
//
#pragma once

#include "Optimization.hpp"

namespace armnn
{
namespace optimizations
{

class OptimizeConsecutiveReshapesImpl
{
public:
    /// Run for every connection between a base RashapeLayer and a child ReshapeLayer.
    /// Inserts an equivalent ReshapeLayer that bypasses both for that connection.
    void Run(Graph& graph, InputSlot& connection) const
    {
        Layer& base = connection.GetConnectedOutputSlot()->GetOwningLayer();
        Layer& child = connection.GetOwningLayer();

        BOOST_ASSERT(base.GetType() == LayerType::Reshape);
        BOOST_ASSERT(child.GetType() == LayerType::Reshape);

        OutputSlot* parentOut = base.GetInputSlot(0).GetConnectedOutputSlot();

        const TensorInfo& inInfo = parentOut->GetTensorInfo();
        const TensorInfo& outInfo = child.GetOutputHandler().GetTensorInfo();

        if (inInfo.GetShape() != outInfo.GetShape())
        {
            // Inserts equivalent reshape before base layer.
            const std::string name = std::string("merged-") + base.GetName() + std::string("-with-") + child.GetName();
            const ReshapeDescriptor descriptor{outInfo.GetShape()};
            auto& newReshape = *graph.InsertNewLayer<ReshapeLayer>(base.GetInputSlot(0), descriptor, name.c_str());
            // Sets tensor info for new layer.
            newReshape.GetOutputHandler().SetTensorInfo(outInfo);
            // Reconnects base with original parent.
            newReshape.GetOutputSlot().MoveAllConnections(*parentOut);
            // Parent is now the new layer.
            parentOut = &newReshape.GetOutputSlot();
        }

        // Moves connections in child output to parent layer.
        // Child layer will be removed as it's left unconnected.
        // Base layer will be removed if left unconnected.
        child.GetOutputSlot().MoveAllConnections(*parentOut);
    }

protected:
    OptimizeConsecutiveReshapesImpl() = default;
    ~OptimizeConsecutiveReshapesImpl() = default;
};

using OptimizeConsecutiveReshapes = OptimizeForConnection<ReshapeLayer, ReshapeLayer, OptimizeConsecutiveReshapesImpl>;

} // namespace optimizations
} // namespace armnn
