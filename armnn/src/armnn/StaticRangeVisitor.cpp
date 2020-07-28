//
// Copyright © 2017 Arm Ltd. All rights reserved.
// SPDX-License-Identifier: MIT
//

#include "StaticRangeVisitor.hpp"

#include <boost/core/ignore_unused.hpp>
#include <armnn/Descriptors.hpp>
#include <armnn/Types.hpp>

#include <limits>

namespace armnn
{

StaticRangeVisitor::StaticRangeVisitor(RangeTracker& rangeTracker)
    : m_RangeTracker(rangeTracker)
{}

void StaticRangeVisitor::SetRange(const IConnectableLayer* layer, unsigned int outputIdx, float min, float max)
{
    m_RangeTracker.SetRange(layer, outputIdx, min, max);
}

void StaticRangeVisitor::ForwardParentParameters(const IConnectableLayer* layer)
{
    const auto parentRange = m_RangeTracker.GetRange(layer->GetInputSlot(0).GetConnection()->GetOwningLayerGuid(), 0);
    SetRange(layer, 0, parentRange.first, parentRange.second);
}

void StaticRangeVisitor::VisitAdditionLayer(const IConnectableLayer* layer, const char* name)
{
    SetRange(layer, 0, -20.f, 20.f);
}

void StaticRangeVisitor::VisitBatchNormalizationLayer(const IConnectableLayer* layer,
                                                      const BatchNormalizationDescriptor& desc,
                                                      const ConstTensor& mean,
                                                      const ConstTensor& variance,
                                                      const ConstTensor& beta,
                                                      const ConstTensor& gamma,
                                                      const char* name)
{
    boost::ignore_unused(desc);
    boost::ignore_unused(mean);
    boost::ignore_unused(variance);
    boost::ignore_unused(beta);
    boost::ignore_unused(gamma);
    boost::ignore_unused(name);
    SetRange(layer, 0, -15.0f, 15.0f);
}

void StaticRangeVisitor::VisitConvolution2dLayer(const IConnectableLayer* layer,
                                                 const Convolution2dDescriptor& convolution2dDescriptor,
                                                 const ConstTensor& weights,
                                                 const Optional<ConstTensor>& biases,
                                                 const char* name)
{
    boost::ignore_unused(convolution2dDescriptor);
    boost::ignore_unused(weights);
    boost::ignore_unused(biases);
    boost::ignore_unused(name);
    SetRange(layer, 0, -15.0f, 15.0f);
}

void StaticRangeVisitor::VisitDepthwiseConvolution2dLayer(const IConnectableLayer* layer,
                                                          const DepthwiseConvolution2dDescriptor& desc,
                                                          const ConstTensor& weights,
                                                          const Optional<ConstTensor>& biases,
                                                          const char* name)
{
    boost::ignore_unused(desc);
    boost::ignore_unused(weights);
    boost::ignore_unused(biases);
    boost::ignore_unused(name);
    SetRange(layer, 0, -15.0f, 15.0f);
}

void StaticRangeVisitor::VisitActivationLayer(const IConnectableLayer* layer,
                                              const ActivationDescriptor& activationDescriptor,
                                              const char* name)
{
    switch (activationDescriptor.m_Function)
    {
        // Range is 0, 15 for Abs, Linear, ReLu and Soft ReLu
        case ActivationFunction::Abs:
        case ActivationFunction::Linear:
        case ActivationFunction::ReLu:
        case ActivationFunction::SoftReLu:
            SetRange(layer, 0, 0.f, 15.f);
            break;
        case ActivationFunction::BoundedReLu:
            SetRange(layer, 0, 0.f, activationDescriptor.m_A);
            break;
        case ActivationFunction::TanH:
            SetRange(layer, 0, -1.f, 1.f);
            break;
        case ActivationFunction::LeakyReLu:
            SetRange(layer, 0, -5.f, 15.f);
            break;
        default:
            SetRange(layer, 0, -15.f, 15.f);
            break;
    }
}

void StaticRangeVisitor::VisitFullyConnectedLayer(const IConnectableLayer *layer,
                                                  const FullyConnectedDescriptor& desc,
                                                  const ConstTensor& weights,
                                                  const Optional<ConstTensor>& biases,
                                                  const char *name)
{
    boost::ignore_unused(desc);
    boost::ignore_unused(weights);
    boost::ignore_unused(biases);
    boost::ignore_unused(name);
    SetRange(layer, 0, -15.0f, 15.0f);
}

void StaticRangeVisitor::VisitPermuteLayer(const IConnectableLayer* layer,
                                           const PermuteDescriptor& permuteDescriptor,
                                           const char* name)
{
    boost::ignore_unused(permuteDescriptor);
    boost::ignore_unused(name);
    ForwardParentParameters(layer);
}

void StaticRangeVisitor::VisitSpaceToBatchNdLayer(const IConnectableLayer* layer,
                                                  const SpaceToBatchNdDescriptor& spaceToBatchNdDescriptor,
                                                  const char* name)
{
    boost::ignore_unused(spaceToBatchNdDescriptor);
    boost::ignore_unused(name);
    ForwardParentParameters(layer);
}

void StaticRangeVisitor::VisitPooling2dLayer(const IConnectableLayer* layer,
                                             const Pooling2dDescriptor& pooling2dDescriptor,
                                             const char* name)
{
    boost::ignore_unused(pooling2dDescriptor);
    ForwardParentParameters(layer);
}

void StaticRangeVisitor::VisitSoftmaxLayer(const IConnectableLayer* layer,
                                           const SoftmaxDescriptor& softmaxDescriptor,
                                           const char* name)
{
    boost::ignore_unused(softmaxDescriptor);
    SetRange(layer, 0, 0.f, 1.f);
}

void StaticRangeVisitor::VisitConstantLayer(const IConnectableLayer* layer,
                                            const ConstTensor& input,
                                            const char* name)
{
    boost::ignore_unused(name);

    if (input.GetDataType() != DataType::Float32)
    {
        throw InvalidArgumentException("Quantization is supported only for FP32 tensors");
    }

    // Work out the range based on the input constants
    unsigned int inputNumElements = input.GetNumElements();
    const float* inputData = reinterpret_cast<const float*>(input.GetMemoryArea());

    float min = std::numeric_limits<float>::max();
    float max = std::numeric_limits<float>::lowest();

    for (unsigned int i = 0; i < inputNumElements; i++)
    {
        const float inputValue = inputData[i];

        min = std::min(min, inputValue);
        max = std::max(max, inputValue);
    }
    SetRange(layer, 0, min, max);
}

void StaticRangeVisitor::VisitMergerLayer(const IConnectableLayer* layer,
                                          const OriginsDescriptor& mergerDescriptor,
                                          const char* name)
{
    float min = std::numeric_limits<float>::max();
    float max = std::numeric_limits<float>::lowest();
    for (unsigned int i = 0; i < layer->GetNumInputSlots(); ++i)
    {
        const IOutputSlot* outputSlot = layer->GetInputSlot(i).GetConnection();
        LayerGuid layerId = outputSlot->GetOwningLayerGuid();
        unsigned int slotIndex = outputSlot->CalculateIndexOnOwner();
        RangeTracker::MinMaxRange range = m_RangeTracker.GetRange(layerId, slotIndex);
        min = std::min(min, range.first);
        max = std::max(max, range.second);
    }
    SetRange(layer, 0, min, max);
}

void StaticRangeVisitor::VisitReshapeLayer(const IConnectableLayer* layer,
                                           const ReshapeDescriptor& reshapeDescriptor,
                                           const char* name)
{
    boost::ignore_unused(reshapeDescriptor);
    boost::ignore_unused(name);
    ForwardParentParameters(layer);
}

void StaticRangeVisitor::VisitSplitterLayer(const IConnectableLayer* layer,
                                            const SplitterDescriptor& splitterDescriptor,
                                            const char* name)
{
    boost::ignore_unused(splitterDescriptor);
    boost::ignore_unused(name);
    ForwardParentParameters(layer);
}

void StaticRangeVisitor::VisitResizeBilinearLayer(const IConnectableLayer* layer,
                                                  const ResizeBilinearDescriptor& resizeDesc,
                                                  const char* name)
{
    boost::ignore_unused(resizeDesc);
    boost::ignore_unused(name);
    ForwardParentParameters(layer);
}

void StaticRangeVisitor::VisitStridedSliceLayer(const IConnectableLayer* layer,
                                                const StridedSliceDescriptor& stridedSliceDescriptor,
                                                const char* name)
{
    boost::ignore_unused(stridedSliceDescriptor);
    boost::ignore_unused(name);
    ForwardParentParameters(layer);
}

void StaticRangeVisitor::VisitBatchToSpaceNdLayer(const IConnectableLayer* layer,
                                                  const BatchToSpaceNdDescriptor& batchToSpaceNdDescriptor,
                                                  const char* name)
{
    boost::ignore_unused(batchToSpaceNdDescriptor);
    boost::ignore_unused(name);
    ForwardParentParameters(layer);
}

} //namespace armnn
