//
// Copyright © 2017 Arm Ltd. All rights reserved.
// SPDX-License-Identifier: MIT
//
#pragma once

#include <Layer.hpp>

namespace armnn
{

/// A layer user-provided data can be bound to (e.g. inputs, outputs).
class OutputLayer : public BindableLayer
{
public:
    /// Returns nullptr for Output type.
    /// @param [in] graph The graph where this layer can be found.
    /// @param [in] factory The workload factory which will create the workload.
    /// @return A pointer to the created workload, or nullptr if not created.
    virtual std::unique_ptr<IWorkload> CreateWorkload(const Graph&            graph,
                                                      const IWorkloadFactory& factory) const override;

    /// Set the outputs to be appropriate sub tensors of the input if sub tensors are supported
    /// otherwise creates tensor handlers by default. Ignores parameters for Output type.
    /// @param [in] graph The graph where this layer can be found.
    /// @param [in] factory The workload factory which will create the workload.
    virtual void CreateTensorHandles(Graph& graph, const IWorkloadFactory& factory) override
    {
        boost::ignore_unused(graph, factory);
    }

    /// Creates a dynamically-allocated copy of this layer.
    /// @param [in] graph The graph into which this layer is being cloned.
    OutputLayer* Clone(Graph& graph) const override;

    /// Check if the input tensor shape(s)
    /// will lead to a valid configuration of @ref OutputLayer.
    void ValidateTensorShapesFromInputs() override;

    void Accept(ILayerVisitor& visitor) const override;

protected:
    /// Constructor to create an OutputLayer.
    /// @param id The layer binding id number.
    /// @param name Optional name for the layer.
    OutputLayer(LayerBindingId id, const char* name);

    /// Default destructor
    ~OutputLayer() = default;
};

} // namespace
