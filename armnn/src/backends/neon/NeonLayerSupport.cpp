//
// Copyright © 2017 Arm Ltd. All rights reserved.
// SPDX-License-Identifier: MIT
//

#include "NeonLayerSupport.hpp"
#include "NeonBackendId.hpp"

#include <armnn/Descriptors.hpp>
#include <InternalTypes.hpp>
#include <LayerSupportCommon.hpp>
#include <armnn/Tensor.hpp>
#include <armnn/Types.hpp>

#include <backendsCommon/BackendRegistry.hpp>

#include <boost/core/ignore_unused.hpp>

#if defined(ARMCOMPUTENEON_ENABLED)
#include <aclCommon/ArmComputeUtils.hpp>
#include "workloads/NeonAdditionWorkload.hpp"
#include "workloads/NeonActivationWorkload.hpp"
#include "workloads/NeonBatchNormalizationWorkload.hpp"
#include "workloads/NeonConvolution2dWorkload.hpp"
#include "workloads/NeonDepthwiseConvolutionWorkload.hpp"
#include "workloads/NeonGreaterWorkload.hpp"
#include "workloads/NeonL2NormalizationFloatWorkload.hpp"
#include "workloads/NeonMaximumWorkload.hpp"
#include "workloads/NeonMeanWorkload.hpp"
#include "workloads/NeonConcatWorkload.hpp"
#include "workloads/NeonMinimumWorkload.hpp"
#include "workloads/NeonMultiplicationWorkload.hpp"
#include "workloads/NeonNormalizationFloatWorkload.hpp"
#include "workloads/NeonFullyConnectedWorkload.hpp"
#include "workloads/NeonPadWorkload.hpp"
#include "workloads/NeonPermuteWorkload.hpp"
#include "workloads/NeonPooling2dWorkload.hpp"
#include "workloads/NeonResizeBilinearWorkload.hpp"
#include "workloads/NeonSoftmaxBaseWorkload.hpp"
#include "workloads/NeonSplitterWorkload.hpp"
#include "workloads/NeonSubtractionWorkload.hpp"
#endif

using namespace boost;

namespace armnn
{

namespace
{

bool IsNeonBackendSupported(Optional<std::string&> reasonIfUnsupported)
{
#if defined(ARMCOMPUTENEON_ENABLED)
    return true;
#else
    SetValueChecked(reasonIfUnsupported, "The armnn library has been built without NEON support");
    return false;
#endif
}

template<typename FloatFunc, typename Uint8Func, typename ... Params>
bool IsSupportedForDataTypeNeon(Optional<std::string&> reasonIfUnsupported,
                                DataType dataType,
                                FloatFunc floatFuncPtr,
                                Uint8Func uint8FuncPtr,
                                Params&&... params)
{
    return IsNeonBackendSupported(reasonIfUnsupported) &&
        IsSupportedForDataTypeGeneric(reasonIfUnsupported,
                                         dataType,
                                         floatFuncPtr,
                                         floatFuncPtr,
                                         uint8FuncPtr,
                                         &FalseFunc<>,
                                         &FalseFunc<>,
                                         std::forward<Params>(params)...);
}

#if defined(ARMCOMPUTENEON_ENABLED)
template<class FuncType, class... Args>
inline bool IsWorkloadSupported(FuncType& func, Optional<std::string&> reasonIfUnsupported, Args&&... args)
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
    return IsNeonBackendSupported(reasonIfUnsupported);
#endif

} // anonymous namespace

bool NeonLayerSupport::IsActivationSupported(const TensorInfo& input,
                                             const TensorInfo& output,
                                             const ActivationDescriptor& descriptor,
                                             Optional<std::string&> reasonIfUnsupported) const
{
    ignore_unused(descriptor);
    FORWARD_WORKLOAD_VALIDATE_FUNC(NeonActivationWorkloadValidate,
                                   reasonIfUnsupported,
                                   input,
                                   output,
                                   descriptor);
}

bool NeonLayerSupport::IsAdditionSupported(const TensorInfo& input0,
                                           const TensorInfo& input1,
                                           const TensorInfo& output,
                                           Optional<std::string&> reasonIfUnsupported) const
{
    FORWARD_WORKLOAD_VALIDATE_FUNC(NeonAdditionWorkloadValidate,
                                   reasonIfUnsupported,
                                   input0,
                                   input1,
                                   output);
}

bool NeonLayerSupport::IsBatchNormalizationSupported(const TensorInfo& input,
                                                     const TensorInfo& output,
                                                     const TensorInfo& mean,
                                                     const TensorInfo& var,
                                                     const TensorInfo& beta,
                                                     const TensorInfo& gamma,
                                                     const BatchNormalizationDescriptor& descriptor,
                                                     Optional<std::string&> reasonIfUnsupported) const
{
    FORWARD_WORKLOAD_VALIDATE_FUNC(NeonBatchNormalizationValidate,
                                   reasonIfUnsupported,
                                   input,
                                   output,
                                   mean,
                                   var,
                                   beta,
                                   gamma,
                                   descriptor);
}

bool NeonLayerSupport::IsConcatSupported(const std::vector<const TensorInfo*> inputs,
                                         const TensorInfo& output,
                                         const OriginsDescriptor& descriptor,
                                         Optional<std::string&> reasonIfUnsupported) const
{
     ARMNN_NO_DEPRECATE_WARN_BEGIN
     return IsMergerSupported(inputs, output, descriptor, reasonIfUnsupported);
     ARMNN_NO_DEPRECATE_WARN_END
}

bool NeonLayerSupport::IsConstantSupported(const TensorInfo& output,
                                           Optional<std::string&> reasonIfUnsupported) const
{
    return IsSupportedForDataTypeNeon(reasonIfUnsupported,
                                      output.GetDataType(),
                                      &TrueFunc<>,
                                      &TrueFunc<>);
}

bool NeonLayerSupport::IsConvertFp16ToFp32Supported(const TensorInfo& input,
                                                    const TensorInfo& output,
                                                    Optional<std::string&> reasonIfUnsupported) const
{
    ignore_unused(input);
    ignore_unused(output);
    ignore_unused(reasonIfUnsupported);
    return true;
}

bool NeonLayerSupport::IsConvertFp32ToFp16Supported(const TensorInfo& input,
                                                    const TensorInfo& output,
                                                    Optional<std::string&> reasonIfUnsupported) const
{
    ignore_unused(input);
    ignore_unused(output);
    ignore_unused(reasonIfUnsupported);
    return true;
}

bool NeonLayerSupport::IsConvolution2dSupported(const TensorInfo& input,
                                                const TensorInfo& output,
                                                const Convolution2dDescriptor& descriptor,
                                                const TensorInfo& weights,
                                                const Optional<TensorInfo>& biases,
                                                Optional<std::string&> reasonIfUnsupported) const
{
    FORWARD_WORKLOAD_VALIDATE_FUNC(NeonConvolution2dWorkloadValidate,
                                   reasonIfUnsupported,
                                   input,
                                   output,
                                   descriptor,
                                   weights,
                                   biases);
}

bool NeonLayerSupport::IsDepthwiseConvolutionSupported(const TensorInfo& input,
                                                       const TensorInfo& output,
                                                       const DepthwiseConvolution2dDescriptor& descriptor,
                                                       const TensorInfo& weights,
                                                       const Optional<TensorInfo>& biases,
                                                       Optional<std::string&> reasonIfUnsupported) const
{
    FORWARD_WORKLOAD_VALIDATE_FUNC(NeonDepthwiseConvolutionWorkloadValidate,
                                   reasonIfUnsupported,
                                   input,
                                   output,
                                   descriptor,
                                   weights,
                                   biases);
}

bool NeonLayerSupport::IsDilatedDepthwiseConvolutionSupported(const TensorInfo& input,
                                                              const TensorInfo& output,
                                                              const DepthwiseConvolution2dDescriptor& descriptor,
                                                              const TensorInfo& weights,
                                                              const Optional<TensorInfo>& biases,
                                                              Optional<std::string&> reasonIfUnsupported) const
{
    FORWARD_WORKLOAD_VALIDATE_FUNC(NeonDepthwiseConvolutionWorkloadValidate,
                                   reasonIfUnsupported,
                                   input,
                                   output,
                                   descriptor,
                                   weights,
                                   biases);
}

bool NeonLayerSupport::IsFloorSupported(const TensorInfo& input,
                                        const TensorInfo& output,
                                        Optional<std::string&> reasonIfUnsupported) const
{
    ignore_unused(output);
    return IsNeonBackendSupported(reasonIfUnsupported) &&
           IsSupportedForDataTypeGeneric(reasonIfUnsupported,
                                         input.GetDataType(),
                                         &FalseFuncF16<>,
                                         &TrueFunc<>,
                                         &FalseFuncU8<>,
                                         &FalseFuncI32<>,
                                         &FalseFuncU8<>);
}

bool NeonLayerSupport::IsFullyConnectedSupported(const TensorInfo& input,
                                                 const TensorInfo& output,
                                                 const TensorInfo& weights,
                                                 const TensorInfo& biases,
                                                 const FullyConnectedDescriptor& descriptor,
                                                 Optional<std::string&> reasonIfUnsupported) const
{
    FORWARD_WORKLOAD_VALIDATE_FUNC(NeonFullyConnectedWorkloadValidate,
                                   reasonIfUnsupported,
                                   input,
                                   output,
                                   weights,
                                   biases,
                                   descriptor);
}

bool NeonLayerSupport::IsGreaterSupported(const armnn::TensorInfo& input0,
                                          const armnn::TensorInfo& input1,
                                          const armnn::TensorInfo& output,
                                          armnn::Optional<std::string&> reasonIfUnsupported) const
{
    FORWARD_WORKLOAD_VALIDATE_FUNC(NeonGreaterWorkloadValidate,
                                   reasonIfUnsupported,
                                   input0,
                                   input1,
                                   output);
}

bool NeonLayerSupport::IsInputSupported(const TensorInfo& input,
                                        Optional<std::string&> reasonIfUnsupported) const
{
    return IsSupportedForDataTypeNeon(reasonIfUnsupported,
                                      input.GetDataType(),
                                      &TrueFunc<>,
                                      &TrueFunc<>);
}

bool NeonLayerSupport::IsL2NormalizationSupported(const TensorInfo& input,
                                                  const TensorInfo& output,
                                                  const L2NormalizationDescriptor& descriptor,
                                                  Optional<std::string&> reasonIfUnsupported) const
{
    FORWARD_WORKLOAD_VALIDATE_FUNC(NeonL2NormalizationWorkloadValidate, reasonIfUnsupported, input, output, descriptor);
}

bool NeonLayerSupport::IsMaximumSupported(const TensorInfo& input0,
                                          const TensorInfo& input1,
                                          const TensorInfo& output,
                                          Optional<std::string&> reasonIfUnsupported) const
{
    FORWARD_WORKLOAD_VALIDATE_FUNC(NeonMaximumWorkloadValidate,
                                   reasonIfUnsupported,
                                   input0,
                                   input1,
                                   output);
}

bool NeonLayerSupport::IsMeanSupported(const TensorInfo& input,
                                       const TensorInfo& output,
                                       const MeanDescriptor& descriptor,
                                       Optional<std::string&> reasonIfUnsupported) const
{
    FORWARD_WORKLOAD_VALIDATE_FUNC(NeonMeanWorkloadValidate,
                                   reasonIfUnsupported,
                                   input,
                                   output,
                                   descriptor);
}

bool NeonLayerSupport::IsMemCopySupported(const TensorInfo &input,
                                          const TensorInfo &output,
                                          Optional<std::string &> reasonIfUnsupported) const
{
    ignore_unused(input);
    ignore_unused(output);
    return true;
}

bool NeonLayerSupport::IsMergerSupported(const std::vector<const TensorInfo*> inputs,
                                         const TensorInfo& output,
                                         const OriginsDescriptor& descriptor,
                                         Optional<std::string&> reasonIfUnsupported) const
{
    if (descriptor.GetNumDimensions() <= descriptor.GetConcatAxis())
    {
        SetValueChecked(reasonIfUnsupported, "Neon Merger: Concat axis > Number of dimensions.");
        return false;
    }

    unsigned int concatInnerAxis = (descriptor.GetNumDimensions() - descriptor.GetConcatAxis()) - 1;
    if(concatInnerAxis < 3) // Width, height, or channels
    {
        FORWARD_WORKLOAD_VALIDATE_FUNC(NeonConcatWorkloadValidate,
                                       reasonIfUnsupported,
                                       inputs,
                                       output,
                                       descriptor);
    }
    else if (concatInnerAxis == 3)
    {
        for (auto& input : inputs)
        {
            if (input && !output.IsTypeSpaceMatch(*input)) // Cannot use sub-tensors if the types are not same space
            {
                SetValueChecked(reasonIfUnsupported, "Neon Merger: Types and quantization parameters must match.");
                return false;
            }
        }
        return true; // Sub-tensors support concat along batch
    }
    else // > 4 dimensions not supported.
    {
        SetValueChecked(reasonIfUnsupported, "Neon Merger: Maximum of 4 dimensions supported.");
        return false;
    }
}

bool NeonLayerSupport::IsMinimumSupported(const TensorInfo& input0,
                                          const TensorInfo& input1,
                                          const TensorInfo& output,
                                          Optional<std::string&> reasonIfUnsupported) const
{
    FORWARD_WORKLOAD_VALIDATE_FUNC(NeonMinimumWorkloadValidate,
                                   reasonIfUnsupported,
                                   input0,
                                   input1,
                                   output);
}

bool NeonLayerSupport::IsMultiplicationSupported(const TensorInfo& input0,
                                                 const TensorInfo& input1,
                                                 const TensorInfo& output,
                                                 Optional<std::string&> reasonIfUnsupported) const
{
    FORWARD_WORKLOAD_VALIDATE_FUNC(NeonMultiplicationWorkloadValidate,
                                   reasonIfUnsupported,
                                   input0,
                                   input1,
                                   output);
}

bool NeonLayerSupport::IsNormalizationSupported(const TensorInfo& input,
                                                const TensorInfo& output,
                                                const NormalizationDescriptor& descriptor,
                                                Optional<std::string&> reasonIfUnsupported) const
{
    FORWARD_WORKLOAD_VALIDATE_FUNC(NeonNormalizationWorkloadValidate,
                                   reasonIfUnsupported,
                                   input,
                                   output,
                                   descriptor);
}

bool NeonLayerSupport::IsOutputSupported(const TensorInfo& output,
                                         Optional<std::string&> reasonIfUnsupported) const
{
    return IsNeonBackendSupported(reasonIfUnsupported) &&
           IsSupportedForDataTypeGeneric(reasonIfUnsupported,
                                         output.GetDataType(),
                                         &TrueFunc<>,
                                         &TrueFunc<>,
                                         &TrueFunc<>,
                                         &FalseFuncI32<>,
                                         &TrueFunc<>);
}

bool NeonLayerSupport::IsPadSupported(const TensorInfo& input,
                                      const TensorInfo& output,
                                      const PadDescriptor& descriptor,
                                      Optional<std::string&> reasonIfUnsupported) const
{
    FORWARD_WORKLOAD_VALIDATE_FUNC(NeonPadWorkloadValidate,
                                   reasonIfUnsupported,
                                   input,
                                   output,
                                   descriptor);
}

bool NeonLayerSupport::IsPermuteSupported(const TensorInfo& input,
                                          const TensorInfo& output,
                                          const PermuteDescriptor& descriptor,
                                          Optional<std::string&> reasonIfUnsupported) const
{
    FORWARD_WORKLOAD_VALIDATE_FUNC(NeonPermuteWorkloadValidate, reasonIfUnsupported, input, output, descriptor);
}

bool NeonLayerSupport::IsPooling2dSupported(const TensorInfo& input,
                                            const TensorInfo& output,
                                            const Pooling2dDescriptor& descriptor,
                                            Optional<std::string&> reasonIfUnsupported) const
{
    FORWARD_WORKLOAD_VALIDATE_FUNC(NeonPooling2dWorkloadValidate, reasonIfUnsupported, input, output, descriptor);
}

bool NeonLayerSupport::IsReshapeSupported(const TensorInfo& input,
                                          const ReshapeDescriptor& descriptor,
                                          Optional<std::string&> reasonIfUnsupported) const
{
    ignore_unused(descriptor);
    return IsSupportedForDataTypeNeon(reasonIfUnsupported,
                                      input.GetDataType(),
                                      &TrueFunc<>,
                                      &TrueFunc<>);
}

bool NeonLayerSupport::IsResizeBilinearSupported(const TensorInfo& input,
                                                 const TensorInfo& output,
                                                 Optional<std::string&> reasonIfUnsupported) const
{
    FORWARD_WORKLOAD_VALIDATE_FUNC(NeonResizeBilinearWorkloadValidate,
                                   reasonIfUnsupported,
                                   input,
                                   output);
}

bool NeonLayerSupport::IsSoftmaxSupported(const TensorInfo& input,
                                          const TensorInfo& output,
                                          const SoftmaxDescriptor& descriptor,
                                          Optional<std::string&> reasonIfUnsupported) const
{
    FORWARD_WORKLOAD_VALIDATE_FUNC(NeonSoftmaxWorkloadValidate, reasonIfUnsupported, input, output, descriptor);
}

bool NeonLayerSupport::IsSplitterSupported(const TensorInfo& input,
                                           const ViewsDescriptor& descriptor,
                                           Optional<std::string&> reasonIfUnsupported) const
{
    ignore_unused(descriptor);
    return IsSupportedForDataTypeNeon(reasonIfUnsupported,
                                      input.GetDataType(),
                                      &TrueFunc<>,
                                      &TrueFunc<>);
}

bool NeonLayerSupport::IsSplitterSupported(const TensorInfo& input,
                                           const std::vector<std::reference_wrapper<TensorInfo>>& outputs,
                                           const ViewsDescriptor& descriptor,
                                           Optional<std::string&> reasonIfUnsupported) const
{
#if defined(ARMCOMPUTENEON_ENABLED)
    // Split along the last dimension, cannot use sub-tensors
    // as width and height of the sub-tensors do not match
    // the width and height of the parent tensor
    // in case of input with more than 2D.
    std::set<unsigned int> splitAxis = ComputeSplitAxis(descriptor, input.GetShape());
    if (descriptor.GetNumDimensions() > 2 && splitAxis.size() == 1 &&
        *splitAxis.begin() == descriptor.GetNumDimensions() - 1 )
    {
        FORWARD_WORKLOAD_VALIDATE_FUNC(NeonSplitterWorkloadValidate,
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
            SetValueChecked(reasonIfUnsupported, "Neon Splitter: Types and quantization parameters must match.");
            return false;
        }
    }
    return true;
}

bool NeonLayerSupport::IsSubtractionSupported(const TensorInfo& input0,
                                              const TensorInfo& input1,
                                              const TensorInfo& output,
                                              Optional<std::string&> reasonIfUnsupported) const
{
    FORWARD_WORKLOAD_VALIDATE_FUNC(NeonSubtractionWorkloadValidate,
                                   reasonIfUnsupported,
                                   input0,
                                   input1,
                                   output);
}

} // namespace armnn
