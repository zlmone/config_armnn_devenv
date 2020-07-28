﻿//
// Copyright © 2017 Arm Ltd. All rights reserved.
// SPDX-License-Identifier: MIT
//
#pragma once

#include "InternalTypes.hpp"

#include "layers/ActivationLayer.hpp"
#include "layers/AdditionLayer.hpp"
#include "layers/BatchNormalizationLayer.hpp"
#include "layers/BatchToSpaceNdLayer.hpp"
#include "layers/ConstantLayer.hpp"
#include "layers/ConvertFp16ToFp32Layer.hpp"
#include "layers/ConvertFp32ToFp16Layer.hpp"
#include "layers/Convolution2dLayer.hpp"
#include "layers/DebugLayer.hpp"
#include "layers/DepthwiseConvolution2dLayer.hpp"
#include "layers/DequantizeLayer.hpp"
#include "layers/DetectionPostProcessLayer.hpp"
#include "layers/DivisionLayer.hpp"
#include "layers/EqualLayer.hpp"
#include "layers/FakeQuantizationLayer.hpp"
#include "layers/FloorLayer.hpp"
#include "layers/FullyConnectedLayer.hpp"
#include "layers/GatherLayer.hpp"
#include "layers/GreaterLayer.hpp"
#include "layers/InputLayer.hpp"
#include "layers/L2NormalizationLayer.hpp"
#include "layers/LstmLayer.hpp"
#include "layers/MaximumLayer.hpp"
#include "layers/MeanLayer.hpp"
#include "layers/MemCopyLayer.hpp"
#include "layers/MergeLayer.hpp"
#include "layers/MergerLayer.hpp"
#include "layers/MinimumLayer.hpp"
#include "layers/MultiplicationLayer.hpp"
#include "layers/NormalizationLayer.hpp"
#include "layers/OutputLayer.hpp"
#include "layers/PadLayer.hpp"
#include "layers/PermuteLayer.hpp"
#include "layers/Pooling2dLayer.hpp"
#include "layers/PreCompiledLayer.hpp"
#include "layers/QuantizeLayer.hpp"
#include "layers/ReshapeLayer.hpp"
#include "layers/ResizeBilinearLayer.hpp"
#include "layers/RsqrtLayer.hpp"
#include "layers/SoftmaxLayer.hpp"
#include "layers/SpaceToBatchNdLayer.hpp"
#include "layers/SplitterLayer.hpp"
#include "layers/StridedSliceLayer.hpp"
#include "layers/SubtractionLayer.hpp"
#include "layers/SwitchLayer.hpp"

namespace armnn
{

template <LayerType Type>
struct LayerTypeOfImpl;

template <LayerType Type>
using LayerTypeOf = typename LayerTypeOfImpl<Type>::Type;

template <typename T>
constexpr LayerType LayerEnumOf(const T* = nullptr);

#define DECLARE_LAYER_IMPL(_, LayerName)                     \
    class LayerName##Layer;                                  \
    template <>                                              \
    struct LayerTypeOfImpl<LayerType::_##LayerName>          \
    {                                                        \
        using Type = LayerName##Layer;                       \
    };                                                       \
    template <>                                              \
    constexpr LayerType LayerEnumOf(const LayerName##Layer*) \
    {                                                        \
        return LayerType::_##LayerName;                      \
    }

#define DECLARE_LAYER(LayerName) DECLARE_LAYER_IMPL(, LayerName)

DECLARE_LAYER(Activation)
DECLARE_LAYER(Addition)
DECLARE_LAYER(BatchNormalization)
DECLARE_LAYER(BatchToSpaceNd)
DECLARE_LAYER(Constant)
DECLARE_LAYER(ConvertFp16ToFp32)
DECLARE_LAYER(ConvertFp32ToFp16)
DECLARE_LAYER(Convolution2d)
DECLARE_LAYER(Debug)
DECLARE_LAYER(DepthwiseConvolution2d)
DECLARE_LAYER(Dequantize)
DECLARE_LAYER(DetectionPostProcess)
DECLARE_LAYER(Division)
DECLARE_LAYER(Equal)
DECLARE_LAYER(FakeQuantization)
DECLARE_LAYER(Floor)
DECLARE_LAYER(FullyConnected)
DECLARE_LAYER(Gather)
DECLARE_LAYER(Greater)
DECLARE_LAYER(Input)
DECLARE_LAYER(L2Normalization)
DECLARE_LAYER(Lstm)
DECLARE_LAYER(Maximum)
DECLARE_LAYER(Mean)
DECLARE_LAYER(MemCopy)
DECLARE_LAYER(Merge)
DECLARE_LAYER(Merger)
DECLARE_LAYER(Minimum)
DECLARE_LAYER(Multiplication)
DECLARE_LAYER(Normalization)
DECLARE_LAYER(Output)
DECLARE_LAYER(Pad)
DECLARE_LAYER(Permute)
DECLARE_LAYER(Pooling2d)
DECLARE_LAYER(PreCompiled)
DECLARE_LAYER(Quantize)
DECLARE_LAYER(Reshape)
DECLARE_LAYER(ResizeBilinear)
DECLARE_LAYER(Rsqrt)
DECLARE_LAYER(Softmax)
DECLARE_LAYER(SpaceToBatchNd)
DECLARE_LAYER(Splitter)
DECLARE_LAYER(StridedSlice)
DECLARE_LAYER(Subtraction)
DECLARE_LAYER(Switch)

}
