//
// Copyright © 2017 Arm Ltd. All rights reserved.
// SPDX-License-Identifier: MIT
//

#include "ClLayerSupport.hpp"
#include "ClBackendId.hpp"

#include <armnn/Descriptors.hpp>
#include <InternalTypes.hpp>
#include <LayerSupportCommon.hpp>

#include <backendsCommon/BackendRegistry.hpp>

#include <boost/core/ignore_unused.hpp>

#if defined(ARMCOMPUTECL_ENABLED)
#include <aclCommon/ArmComputeUtils.hpp>
#include "workloads/ClAdditionWorkload.hpp"
#include "workloads/ClActivationWorkload.hpp"
#include "workloads/ClBatchNormalizationFloatWorkload.hpp"
#include "workloads/ClBatchToSpaceNdWorkload.hpp"
#include "workloads/ClConvertFp16ToFp32Workload.hpp"
#include "workloads/ClConvertFp32ToFp16Workload.hpp"
#include "workloads/ClConvolution2dWorkload.hpp"
#include "workloads/ClDepthwiseConvolutionWorkload.hpp"
#include "workloads/ClDivisionFloatWorkload.hpp"
#include "workloads/ClFullyConnectedWorkload.hpp"
#include "workloads/ClGreaterWorkload.hpp"
#include "workloads/ClL2NormalizationFloatWorkload.hpp"
#include "workloads/ClLstmFloatWorkload.hpp"
#include "workloads/ClMaximumWorkload.hpp"
#include "workloads/ClMeanWorkload.hpp"
#include "workloads/ClConcatWorkload.hpp"
#include "workloads/ClMinimumWorkload.hpp"
#include "workloads/ClMultiplicationWorkload.hpp"
#include "workloads/ClNormalizationFloatWorkload.hpp"
#include "workloads/ClPadWorkload.hpp"
#include "workloads/ClPermuteWorkload.hpp"
#include "workloads/ClPooling2dWorkload.hpp"
#include "workloads/ClSoftmaxBaseWorkload.hpp"
#include "workloads/ClSpaceToBatchNdWorkload.hpp"
#include "workloads/ClSplitterWorkload.hpp"
#include "workloads/ClStridedSliceWorkload.hpp"
#include "workloads/ClSubtractionWorkload.hpp"
#endif

using namespace boost;

namespace armnn
{

namespace
{

template<unsigned int FilterSize>
bool IsMatchingSize2d(const TensorInfo& weightInfo)
{
    // Width & Height must match.
    return (weightInfo.GetShape()[3] == FilterSize) && (weightInfo.GetShape()[2] == FilterSize);
}

template<uint32_t ValidStride>
bool IsMatchingStride(uint32_t actualStride)
{
    return ValidStride == actualStride;
}

template<uint32_t FirstStride, uint32_t SecondStride, uint32_t... ValidStrides>
bool IsMatchingStride(uint32_t actualStride)
{
    return IsMatchingStride<FirstStride>(actualStride) || IsMatchingStride<SecondStride, ValidStrides...>(actualStride);
}

bool IsClBackendSupported(Optional<std::string&> reasonIfUnsupported)
{
#if defined(ARMCOMPUTECL_ENABLED)
    return true;
#else
    if (reasonIfUnsupported)
    {
        reasonIfUnsupported.value() = "The armnn library has been built without CL support";
    }
    return false;
#endif
}

#if defined(ARMCOMPUTECL_ENABLED)
#define FORWARD_CL_LAYER_SUPPORT_FUNC(expr) (expr)
#else
#define FORWARD_CL_LAYER_SUPPORT_FUNC(expr) IsClBackendSupported(reasonIfUnsupported)
#endif

#if defined(ARMCOMPUTECL_ENABLED)
template<class FuncType, class... Args>
inline bool IsWorkloadSupported(FuncType&& func, Optional<std::string&> reasonIfUnsupported, Args&&... args)
{
    arm_compute::Status aclStatus = func(std::forward<Args>(args)...);
    const bool supported = (aclStatus.error_code() == arm_compute::ErrorCode::OK);
    if (!supported && reasonIfUnsupported)
    {
        reasonIfUnsupported.value() = aclStatus.error_description();
    }
    return supported;
}

#define FORWARD_WORKLOAD_VALIDATE_FUNC(func, reasonIfUnsupported, ...) \
    return IsWorkloadSupported(func, reasonIfUnsupported, __VA_ARGS__);
#else
#define FORWARD_WORKLOAD_VALIDATE_FUNC(func, reasonIfUnsupported, ...) \
    return IsClBackendSupported(reasonIfUnsupported);
#endif

template<typename FloatFunc, typename Uint8Func, typename ... Params>
bool IsSupportedForDataTypeCl(Optional<std::string&> reasonIfUnsupported,
                              DataType dataType,
                              FloatFunc floatFuncPtr,
                              Uint8Func uint8FuncPtr,
                              Params&&... params)
{
    return IsClBackendSupported(reasonIfUnsupported) &&
        IsSupportedForDataTypeGeneric(reasonIfUnsupported,
                                      dataType,
                                      floatFuncPtr,
                                      floatFuncPtr,
                                      uint8FuncPtr,
                                      &FalseFunc<>,
                                      &FalseFunc<>,
                                      std::forward<Params>(params)...);
}

} // anonymous namespace

bool ClLayerSupport::IsActivationSupported(const TensorInfo& input,
                                           const TensorInfo& output,
                                           const ActivationDescriptor& descriptor,
                                           Optional<std::string&> reasonIfUnsupported) const
{
    FORWARD_WORKLOAD_VALIDATE_FUNC(ClActivationWorkloadValidate,
                                   reasonIfUnsupported,
                                   input,
                                   output,
                                   descriptor);
}

bool ClLayerSupport::IsAdditionSupported(const TensorInfo& input0,
                                         const TensorInfo& input1,
                                         const TensorInfo& output,
                                         Optional<std::string&> reasonIfUnsupported) const
{
    FORWARD_WORKLOAD_VALIDATE_FUNC(ClAdditionValidate,
                                   reasonIfUnsupported,
                                   input0,
                                   input1,
                                   output);
}

bool ClLayerSupport::IsBatchNormalizationSupported(const TensorInfo& input,
                                                   const TensorInfo& output,
                                                   const TensorInfo& mean,
                                                   const TensorInfo& var,
                                                   const TensorInfo& beta,
                                                   const TensorInfo& gamma,
                                                   const BatchNormalizationDescriptor& descriptor,
                                                   Optional<std::string&> reasonIfUnsupported) const
{
    FORWARD_WORKLOAD_VALIDATE_FUNC(ClBatchNormalizationValidate,
                                   reasonIfUnsupported,
                                   input,
                                   output,
                                   mean,
                                   var,
                                   beta,
                                   gamma,
                                   descriptor);
}

bool ClLayerSupport::IsBatchToSpaceNdSupported(const TensorInfo& input,
                                               const TensorInfo& output,
                                               const BatchToSpaceNdDescriptor& descriptor,
                                               Optional<std::string&> reasonIfUnsupported) const
{
    FORWARD_WORKLOAD_VALIDATE_FUNC(ClBatchToSpaceNdWorkloadValidate,
                                   reasonIfUnsupported,
                                   input,
                                   output,
                                   descriptor);
}

bool ClLayerSupport::IsConcatSupported(const std::vector<const TensorInfo*> inputs,
                                       const TensorInfo& output,
                                       const OriginsDescriptor& descriptor,
                                       Optional<std::string&> reasonIfUnsupported) const
{
    ARMNN_NO_DEPRECATE_WARN_BEGIN
    return IsMergerSupported(inputs, output, descriptor, reasonIfUnsupported);
    ARMNN_NO_DEPRECATE_WARN_END
}

bool ClLayerSupport::IsConstantSupported(const TensorInfo& output,
                                         Optional<std::string&> reasonIfUnsupported) const
{
    return IsSupportedForDataTypeCl(reasonIfUnsupported,
                                    output.GetDataType(),
                                    &TrueFunc<>,
                                    &FalseFuncU8<>);
}

bool ClLayerSupport::IsConvertFp16ToFp32Supported(const TensorInfo& input,
                                                  const TensorInfo& output,
                                                  Optional<std::string&> reasonIfUnsupported) const
{
    FORWARD_WORKLOAD_VALIDATE_FUNC(ClConvertFp16ToFp32WorkloadValidate,
                                   reasonIfUnsupported,
                                   input,
                                   output);
}

bool ClLayerSupport::IsConvertFp32ToFp16Supported(const TensorInfo& input,
                                                  const TensorInfo& output,
                                                  Optional<std::string&> reasonIfUnsupported) const
{
    FORWARD_WORKLOAD_VALIDATE_FUNC(ClConvertFp32ToFp16WorkloadValidate,
                                   reasonIfUnsupported,
                                   input,
                                   output);
}

bool ClLayerSupport::IsConvolution2dSupported(const TensorInfo& input,
                                              const TensorInfo& output,
                                              const Convolution2dDescriptor& descriptor,
                                              const TensorInfo& weights,
                                              const Optional<TensorInfo>& biases,
                                              Optional<std::string&> reasonIfUnsupported) const
{
    FORWARD_WORKLOAD_VALIDATE_FUNC(ClConvolution2dWorkloadValidate,
                                   reasonIfUnsupported,
                                   input,
                                   output,
                                   descriptor,
                                   weights,
                                   biases);
}

bool ClLayerSupport::IsDepthwiseConvolutionSupported(const TensorInfo& input,
                                                     const TensorInfo& output,
                                                     const DepthwiseConvolution2dDescriptor& descriptor,
                                                     const TensorInfo& weights,
                                                     const Optional<TensorInfo>& biases,
                                                     Optional<std::string&> reasonIfUnsupported) const
{
    FORWARD_WORKLOAD_VALIDATE_FUNC(ClDepthwiseConvolutionWorkloadValidate,
                                   reasonIfUnsupported,
                                   input,
                                   output,
                                   descriptor,
                                   weights,
                                   biases);
}

bool ClLayerSupport::IsDilatedDepthwiseConvolutionSupported(const TensorInfo& input,
                                                            const TensorInfo& output,
                                                            const DepthwiseConvolution2dDescriptor& descriptor,
                                                            const TensorInfo& weights,
                                                            const Optional<TensorInfo>& biases,
                                                            Optional<std::string&> reasonIfUnsupported) const
{
    FORWARD_WORKLOAD_VALIDATE_FUNC(ClDepthwiseConvolutionWorkloadValidate,
                                   reasonIfUnsupported,
                                   input,
                                   output,
                                   descriptor,
                                   weights,
                                   biases);
}


bool ClLayerSupport::IsDivisionSupported(const TensorInfo& input0,
                                         const TensorInfo& input1,
                                         const TensorInfo& output,
                                         Optional<std::string&> reasonIfUnsupported) const
{
    FORWARD_WORKLOAD_VALIDATE_FUNC(ClDivisionWorkloadValidate,
                                   reasonIfUnsupported,
                                   input0,
                                   input1,
                                   output);
}

bool ClLayerSupport::IsFloorSupported(const TensorInfo& input,
                                      const TensorInfo& output,
                                      Optional<std::string&> reasonIfUnsupported) const
{
    ignore_unused(output);
    return IsClBackendSupported(reasonIfUnsupported) &&
           IsSupportedForDataTypeGeneric(reasonIfUnsupported,
                                         input.GetDataType(),
                                         &FalseFuncF16<>,
                                         &TrueFunc<>,
                                         &FalseFuncU8<>,
                                         &FalseFuncI32<>,
                                         &FalseFuncU8<>);
}

bool ClLayerSupport::IsFullyConnectedSupported(const TensorInfo& input,
                                               const TensorInfo& output,
                                               const TensorInfo& weights,
                                               const TensorInfo& biases,
                                               const FullyConnectedDescriptor& descriptor,
                                               Optional<std::string&> reasonIfUnsupported) const
{
    FORWARD_WORKLOAD_VALIDATE_FUNC(ClFullyConnectedWorkloadValidate,
                                   reasonIfUnsupported,
                                   input,
                                   output,
                                   weights,
                                   biases,
                                   descriptor);
}

bool ClLayerSupport::IsGreaterSupported(const TensorInfo& input0,
                                        const TensorInfo& input1,
                                        const TensorInfo& output,
                                        Optional<std::string&> reasonIfUnsupported) const
{
    FORWARD_WORKLOAD_VALIDATE_FUNC(ClGreaterWorkloadValidate,
                                   reasonIfUnsupported,
                                   input0,
                                   input1,
                                   output);
}

bool ClLayerSupport::IsInputSupported(const TensorInfo& input,
                                      Optional<std::string&> reasonIfUnsupported) const
{
    return IsSupportedForDataTypeCl(reasonIfUnsupported,
                                    input.GetDataType(),
                                    &TrueFunc<>,
                                    &TrueFunc<>);
}

bool ClLayerSupport::IsL2NormalizationSupported(const TensorInfo& input,
                                                const TensorInfo& output,
                                                const L2NormalizationDescriptor& descriptor,
                                                Optional<std::string&> reasonIfUnsupported) const
{
    FORWARD_WORKLOAD_VALIDATE_FUNC(ClL2NormalizationWorkloadValidate,
                                   reasonIfUnsupported,
                                   input,
                                   output,
                                   descriptor);
}

bool ClLayerSupport::IsLstmSupported(const TensorInfo& input,
                                     const TensorInfo& outputStateIn,
                                     const TensorInfo& cellStateIn,
                                     const TensorInfo& scratchBuffer,
                                     const TensorInfo& outputStateOut,
                                     const TensorInfo& cellStateOut,
                                     const TensorInfo& output,
                                     const LstmDescriptor& descriptor,
                                     const TensorInfo& inputToForgetWeights,
                                     const TensorInfo& inputToCellWeights,
                                     const TensorInfo& inputToOutputWeights,
                                     const TensorInfo& recurrentToForgetWeights,
                                     const TensorInfo& recurrentToCellWeights,
                                     const TensorInfo& recurrentToOutputWeights,
                                     const TensorInfo& forgetGateBias,
                                     const TensorInfo& cellBias,
                                     const TensorInfo& outputGateBias,
                                     const TensorInfo* inputToInputWeights,
                                     const TensorInfo* recurrentToInputWeights,
                                     const TensorInfo* cellToInputWeights,
                                     const TensorInfo* inputGateBias,
                                     const TensorInfo* projectionWeights,
                                     const TensorInfo* projectionBias,
                                     const TensorInfo* cellToForgetWeights,
                                     const TensorInfo* cellToOutputWeights,
                                     Optional<std::string&> reasonIfUnsupported) const
{
    FORWARD_WORKLOAD_VALIDATE_FUNC(ClLstmFloatWorkloadValidate,
                                   reasonIfUnsupported,
                                   input,
                                   outputStateIn,
                                   cellStateIn,
                                   scratchBuffer,
                                   outputStateOut,
                                   cellStateOut,
                                   output,
                                   descriptor,
                                   inputToForgetWeights,
                                   inputToCellWeights,
                                   inputToOutputWeights,
                                   recurrentToForgetWeights,
                                   recurrentToCellWeights,
                                   recurrentToOutputWeights,
                                   forgetGateBias,
                                   cellBias,
                                   outputGateBias,
                                   inputToInputWeights,
                                   recurrentToInputWeights,
                                   cellToInputWeights,
                                   inputGateBias,
                                   projectionWeights,
                                   projectionBias,
                                   cellToForgetWeights,
                                   cellToOutputWeights);
}

bool ClLayerSupport::IsMaximumSupported(const TensorInfo& input0,
                                        const TensorInfo& input1,
                                        const TensorInfo& output,
                                        Optional<std::string&> reasonIfUnsupported) const
{
    FORWARD_WORKLOAD_VALIDATE_FUNC(ClMaximumWorkloadValidate,
                                   reasonIfUnsupported,
                                   input0,
                                   input1,
                                   output);
}

bool ClLayerSupport::IsMeanSupported(const TensorInfo& input,
                                     const TensorInfo& output,
                                     const MeanDescriptor& descriptor,
                                     Optional<std::string&> reasonIfUnsupported) const
{
    FORWARD_WORKLOAD_VALIDATE_FUNC(ClMeanValidate,
                                   reasonIfUnsupported,
                                   input,
                                   output,
                                   descriptor);
}

bool ClLayerSupport::IsMemCopySupported(const TensorInfo &input,
                                        const TensorInfo &output,
                                        Optional<std::string &> reasonIfUnsupported) const
{
    ignore_unused(input);
    ignore_unused(output);
    return true;
}

bool ClLayerSupport::IsMergerSupported(const std::vector<const TensorInfo*> inputs,
                                       const TensorInfo& output,
                                       const OriginsDescriptor& descriptor,
                                       Optional<std::string&> reasonIfUnsupported) const
{
    if (descriptor.GetNumDimensions() <= descriptor.GetConcatAxis())
    {
        SetValueChecked(reasonIfUnsupported, "Cl Merger: Concat axis > Number of dimensions.");
        return false;
    }

    unsigned int concatInnerAxis = (descriptor.GetNumDimensions() - descriptor.GetConcatAxis()) - 1;
    if(concatInnerAxis < 3) // Width, height, or channels
    {
        FORWARD_WORKLOAD_VALIDATE_FUNC(ClConcatWorkloadValidate,
                                       reasonIfUnsupported,
                                       inputs,
                                       output,
                                       descriptor);
    }
    else if (concatInnerAxis == 3)
    {
        // We rely on the sub-tensor optimization to handle the batch dimension for 4D tensors. If we can't use
        // sub-tensors for this then we can't support it. Here is where we check that the sub-tensors will work.
        for (auto& input : inputs)
        {
            if (input && !output.IsTypeSpaceMatch(*input)) // Cannot use sub-tensors if the types are not same space
            {
                SetValueChecked(reasonIfUnsupported, "Cl Merger: Types and quantization parameters must match.");
                return false;
            }
        }
        return true; // Sub-tensors support concat along batch
    }
    else // > 4 dimensions not supported.
    {
        SetValueChecked(reasonIfUnsupported, "Cl Merger: Maximum of 4 dimensions supported.");
        return false;
    }
}

bool ClLayerSupport::IsMinimumSupported(const TensorInfo& input0,
                                        const TensorInfo& input1,
                                        const TensorInfo& output,
                                        Optional<std::string&> reasonIfUnsupported) const
{
    FORWARD_WORKLOAD_VALIDATE_FUNC(ClMinimumWorkloadValidate,
                                   reasonIfUnsupported,
                                   input0,
                                   input1,
                                   output);
}

bool ClLayerSupport::IsMultiplicationSupported(const TensorInfo& input0,
                                               const TensorInfo& input1,
                                               const TensorInfo& output,
                                               Optional<std::string&> reasonIfUnsupported) const
{
    FORWARD_WORKLOAD_VALIDATE_FUNC(ClMultiplicationWorkloadValidate,
                                   reasonIfUnsupported,
                                   input0,
                                   input1,
                                   output);
}

bool ClLayerSupport::IsNormalizationSupported(const TensorInfo& input,
                                              const TensorInfo& output,
                                              const NormalizationDescriptor& descriptor,
                                              Optional<std::string&> reasonIfUnsupported) const
{
    FORWARD_WORKLOAD_VALIDATE_FUNC(ClNormalizationWorkloadValidate, reasonIfUnsupported, input, output, descriptor);
}

bool ClLayerSupport::IsOutputSupported(const TensorInfo& output,
                                       Optional<std::string&> reasonIfUnsupported) const
{
    return IsClBackendSupported(reasonIfUnsupported) &&
           IsSupportedForDataTypeGeneric(reasonIfUnsupported,
                                         output.GetDataType(),
                                         &TrueFunc<>,
                                         &TrueFunc<>,
                                         &TrueFunc<>,
                                         &FalseFuncI32<>,
                                         &TrueFunc<>);
}

bool ClLayerSupport::IsPadSupported(const TensorInfo& input,
                                    const TensorInfo& output,
                                    const PadDescriptor& descriptor,
                                    Optional<std::string&> reasonIfUnsupported) const
{
    FORWARD_WORKLOAD_VALIDATE_FUNC(ClPadValidate,
                                   reasonIfUnsupported,
                                   input,
                                   output,
                                   descriptor);
}

bool ClLayerSupport::IsPermuteSupported(const TensorInfo& input,
                                        const TensorInfo& output,
                                        const PermuteDescriptor& descriptor,
                                        Optional<std::string&> reasonIfUnsupported) const
{
    ignore_unused(input);
    ignore_unused(output);
    FORWARD_WORKLOAD_VALIDATE_FUNC(ClPermuteWorkloadValidate, reasonIfUnsupported, descriptor);
}

bool ClLayerSupport::IsPooling2dSupported(const TensorInfo& input,
                                          const TensorInfo& output,
                                          const Pooling2dDescriptor& descriptor,
                                          Optional<std::string&> reasonIfUnsupported) const
{
    FORWARD_WORKLOAD_VALIDATE_FUNC(ClPooling2dWorkloadValidate, reasonIfUnsupported, input, output, descriptor);
}

bool ClLayerSupport::IsReshapeSupported(const TensorInfo& input,
                                        const ReshapeDescriptor& descriptor,
                                        Optional<std::string&> reasonIfUnsupported) const
{
    ignore_unused(input);
    ignore_unused(descriptor);
    ignore_unused(reasonIfUnsupported);
    return true;
}

bool ClLayerSupport::IsResizeBilinearSupported(const TensorInfo& input,
                                               const TensorInfo& output,
                                               Optional<std::string&> reasonIfUnsupported) const
{
    ignore_unused(output);
    return IsSupportedForDataTypeCl(reasonIfUnsupported,
                                    input.GetDataType(),
                                    &TrueFunc<>,
                                    &FalseFuncU8<>);
}

bool ClLayerSupport::IsSoftmaxSupported(const TensorInfo& input,
                                        const TensorInfo& output,
                                        const SoftmaxDescriptor& descriptor,
                                        Optional<std::string&> reasonIfUnsupported) const
{
    ignore_unused(descriptor);
    FORWARD_WORKLOAD_VALIDATE_FUNC(ClSoftmaxWorkloadValidate, reasonIfUnsupported, input, output);
}

bool ClLayerSupport::IsSpaceToBatchNdSupported(const TensorInfo& input,
                                               const TensorInfo& output,
                                               const SpaceToBatchNdDescriptor& descriptor,
                                               Optional<std::string&> reasonIfUnsupported) const
{
    FORWARD_WORKLOAD_VALIDATE_FUNC(ClSpaceToBatchNdWorkloadValidate,
                                   reasonIfUnsupported,
                                   input,
                                   output,
                                   descriptor);
}

bool ClLayerSupport::IsSplitterSupported(const TensorInfo& input,
                                         const ViewsDescriptor& descriptor,
                                         Optional<std::string&> reasonIfUnsupported) const
{
    ignore_unused(descriptor);
    return IsSupportedForDataTypeCl(reasonIfUnsupported,
                                    input.GetDataType(),
                                    &TrueFunc<>,
                                    &TrueFunc<>);
}

bool ClLayerSupport::IsSplitterSupported(const TensorInfo& input,
                                         const std::vector<std::reference_wrapper<TensorInfo>>& outputs,
                                         const ViewsDescriptor& descriptor,
                                         Optional<std::string&> reasonIfUnsupported) const
{
#if defined(ARMCOMPUTECL_ENABLED)
    // Split along the last dimension, cannot use sub-tensors
    // as width and height of the sub-tensors do not match
    // the width and height of the parent tensor
    // in case of input with more than 2D.
    std::set<unsigned int> splitAxis = ComputeSplitAxis(descriptor, input.GetShape());
    if (descriptor.GetNumDimensions() > 2 && splitAxis.size() == 1 &&
        *splitAxis.begin() == descriptor.GetNumDimensions() - 1 )
    {
        FORWARD_WORKLOAD_VALIDATE_FUNC(ClSplitterWorkloadValidate,
                                       reasonIfUnsupported,
                                       input,
                                       outputs,
                                       *splitAxis.begin());
    }
#endif
    for (auto output : outputs)
    {
        if (!input.IsTypeSpaceMatch(output)) // Cannot use sub-tensors if the types are not same space
        {
            SetValueChecked(reasonIfUnsupported, "Cl Splitter: Types and quantization parameters must match.");
            return false;
        }
    }
    return true;
}

bool ClLayerSupport::IsStridedSliceSupported(const TensorInfo& input,
                                             const TensorInfo& output,
                                             const StridedSliceDescriptor& descriptor,
                                             Optional<std::string&> reasonIfUnsupported) const
{
    FORWARD_WORKLOAD_VALIDATE_FUNC(ClStridedSliceWorkloadValidate,
                                   reasonIfUnsupported,
                                   input,
                                   output,
                                   descriptor);
}

bool ClLayerSupport::IsSubtractionSupported(const TensorInfo& input0,
                                            const TensorInfo& input1,
                                            const TensorInfo& output,
                                            Optional<std::string&> reasonIfUnsupported) const
{
    FORWARD_WORKLOAD_VALIDATE_FUNC(ClSubtractionValidate,
                                   reasonIfUnsupported,
                                   input0,
                                   input1,
                                   output);
}

} // namespace armnn
