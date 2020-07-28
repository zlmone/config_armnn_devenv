﻿//
// Copyright © 2017 Arm Ltd. All rights reserved.
// SPDX-License-Identifier: MIT
//
#include "CaffeParser.hpp"
#include "RecordByRecordCaffeParser.hpp"

#include "armnn/Descriptors.hpp"
#include "armnn/INetwork.hpp"
#include "armnn/Utils.hpp"
#include "armnn/Exceptions.hpp"

#include "GraphTopologicalSort.hpp"
#include "VerificationHelpers.hpp"

#include <boost/numeric/conversion/cast.hpp>
#include <boost/assert.hpp>
#include <boost/format.hpp>
#include <boost/log/trivial.hpp>

// Caffe
#include "caffe/proto/caffe.pb.h"

// ProtoBuf
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <google/protobuf/text_format.h>
#include <google/protobuf/stubs/common.h>
#include <google/protobuf/stubs/once.h>
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/wire_format_lite_inl.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/generated_message_reflection.h>
#include <google/protobuf/reflection_ops.h>
#include <google/protobuf/wire_format.h>

#include <cmath>
#include <sstream>
#include <queue>
#include <fcntl.h>

/// Caffe networks are loaded from protobuf files (binary or text) using the protobuf library and the generated
/// code from caffe.pb.h. This gives us a caffe::NetParameter which is an in-memory version of the file.
/// This contains a flat list of Caffe 'layers' (e.g. convolution, pooling etc.).
/// Each layer has inputs (called "bottoms") and outputs (called "tops"). Data flows from bottom to top.
/// The bottoms of a layer refer to the tops of other layers, not their names.
/// The names of layers seem to be arbitrary (you could rename a layer and the network wouldn't
/// need any other changes).
///
/// Some layers (e.g. Relu) can be configured so that their top and bottom are both the same. This is called an
/// "in-place" layer and is a Caffe runtime feature used to reduce memory usage by modifying tensors in-place.
/// This isn't relevant to the parser and so we preprocess these layers to convert them to regular layers, to result
/// in a consistent graph structure.

namespace armnnCaffeParser
{

using namespace armnn;
using namespace caffe;
using namespace std;
using namespace google::protobuf::io;

namespace
{

const float* GetArrayPtrFromBlob(const LayerParameter& layerParam, unsigned int blobIndex)
{
    auto nBlobs = layerParam.blobs_size();
    if (blobIndex >= boost::numeric_cast<unsigned int>(nBlobs))
    {
        throw ParseException(
            boost::str(
                boost::format(
                    "Expected data blob at index %1% in layer %2% not found. nBlobs=%2%. %4%") %
                    blobIndex %
                    layerParam.name() %
                    nBlobs %
                    CHECK_LOCATION().AsString()));
    }

    const BlobProto& blob = layerParam.blobs(boost::numeric_cast<int>(blobIndex));

    const float* arrayPtr = blob.data().data();
    return arrayPtr;
}

void GetDataFromBlob(const LayerParameter& layerParam, vector<float>& outData, unsigned int blobIndex)
{
    auto nBlobs = layerParam.blobs_size();
    if (blobIndex >= boost::numeric_cast<unsigned int>(nBlobs))
    {
        throw ParseException(
            boost::str(
                boost::format(
                    "Expected data blob at index %1% in layer %2% not found. %3%") %
                    blobIndex %
                    layerParam.name() %
                    CHECK_LOCATION().AsString()));
    }

    const BlobProto& blob = layerParam.blobs(boost::numeric_cast<int>(blobIndex));

    size_t blobSize = boost::numeric_cast<size_t>(blob.data_size());
    if (blobSize != outData.size())
    {
        throw ParseException(
            boost::str(
                boost::format(
                    "Data blob at index %1% in layer %2% has an unexpected size. "
                    "Expected %3% elements but got %4% elements. %5%") %
                    blobIndex %
                    layerParam.name() %
                    outData.size() %
                    blobSize %
                    CHECK_LOCATION().AsString()));
    }

    int outSizeInt = boost::numeric_cast<int>(outData.size());
    for (int i = 0; i < outSizeInt; ++i)
    {
        outData[static_cast<size_t>(i)] = blob.data(i);
    }
}

template <typename T>
size_t SizeOfVectorData(const vector<T>& vec)
{
    return vec.size() * sizeof(T);
}

void ValidateNumInputsOutputs(const caffe::LayerParameter& layerParameter,
                              unsigned int                 numInputs,
                              unsigned int                 numOutputs)
{
    int numInputsActual = layerParameter.bottom_size();
    if (numInputs != boost::numeric_cast<unsigned int>(numInputsActual))
    {
        throw ParseException(
            boost::str(
                boost::format("Invalid number of inputs requested %1% for layer %2% "
                              "while only %3% present. %4%") %
                              numInputs %
                              layerParameter.name() %
                              numInputsActual %
                              CHECK_LOCATION().AsString()));
    }

    int numOutputsActual = layerParameter.top_size();
    if (numOutputs != boost::numeric_cast<unsigned int>(numOutputsActual))
    {
        throw ParseException(
            boost::str(
                boost::format("Invalid number of outputs requested %1% for layer %2% "
                              "while only %3% present. %4%") %
                              numOutputs %
                              layerParameter.name() %
                              numOutputsActual %
                              CHECK_LOCATION().AsString()));
    }
}

template <typename ParamType, typename ExtractOptional, typename ExtractFallback, typename ValueType>
ValueType GetOptionalWithFallback(const ParamType& param,
                                  ExtractOptional extractOptional,
                                  ExtractFallback extractFallback,
                                  ValueType defaultValue)
{
    auto optValue = extractOptional(param, defaultValue);
    if (optValue.first)
    {
        return optValue.second;
    }
    auto fallbackValue = extractFallback(param, defaultValue);
    return fallbackValue.second;
}

#define GET_OPTIONAL_WITH_VECTOR_FALLBACK(PARAM, \
                                          PARAM_TYPE, \
                                          OPTIONAL_VALUE, \
                                          FALLBACK_VECTOR, \
                                          VALUE_TYPE, \
                                          DEFAULT_VALUE) \
    GetOptionalWithFallback( \
        PARAM, \
        [](const PARAM_TYPE & param, VALUE_TYPE defaultValue) \
        { \
            if (param.has_##OPTIONAL_VALUE ()) \
            { \
                return std::make_pair(true, param.OPTIONAL_VALUE ()); \
            } \
            else \
            { \
                return std::make_pair(false, defaultValue); \
            } \
        }, \
        [](const PARAM_TYPE & param, VALUE_TYPE defaultValue) \
        { \
            if (param.FALLBACK_VECTOR##_size() > 0) \
            { \
                return std::make_pair(true, (param.FALLBACK_VECTOR ()).Get(0)); \
            } \
            else \
            { \
                return std::make_pair(false, defaultValue); \
            } \
        }, \
        DEFAULT_VALUE)

#define GET_OPTIONAL_WITH_FALLBACK(PARAM, \
                                   PARAM_TYPE, \
                                   OPTIONAL_VALUE, \
                                   FALLBACK_VALUE, \
                                   VALUE_TYPE, \
                                   DEFAULT_VALUE) \
    GetOptionalWithFallback( \
        PARAM, \
        [](const PARAM_TYPE & param, VALUE_TYPE defaultValue) \
        { \
            if (param.has_##OPTIONAL_VALUE ()) \
            { \
                return std::make_pair(true, param.OPTIONAL_VALUE ()); \
            } \
            else \
            { \
                return std::make_pair(false, defaultValue); \
            } \
        }, \
        [](const PARAM_TYPE & param, VALUE_TYPE defaultValue) \
        { \
            if (param.has_##FALLBACK_VALUE ()) \
            { \
                return std::make_pair(true, param.FALLBACK_VALUE ()); \
            } \
            else \
            { \
                return std::make_pair(false, defaultValue); \
            } \
        }, \
        DEFAULT_VALUE)

} // namespace <anonymous>

const std::map<std::string, CaffeParserBase::OperationParsingFunction>
    CaffeParserBase::ms_CaffeLayerNameToParsingFunctions = {
    { "Input",        &CaffeParserBase::ParseInputLayer },
    { "Convolution",  &CaffeParserBase::ParseConvLayer },
    { "Pooling",      &CaffeParserBase::ParsePoolingLayer },
    { "ReLU",         &CaffeParserBase::ParseReluLayer },
    { "LRN",          &CaffeParserBase::ParseLRNLayer },
    { "InnerProduct", &CaffeParserBase::ParseInnerProductLayer },
    { "Softmax",      &CaffeParserBase::ParseSoftmaxLayer },
    { "Eltwise",      &CaffeParserBase::ParseEltwiseLayer },
    { "Concat",       &CaffeParserBase::ParseConcatLayer },
    { "BatchNorm",    &CaffeParserBase::ParseBatchNormLayer },
    { "Scale",        &CaffeParserBase::ParseScaleLayer },
    { "Split",        &CaffeParserBase::ParseSplitLayer },
    { "Dropout",      &CaffeParserBase::ParseDropoutLayer},
};

ICaffeParser* ICaffeParser::CreateRaw()
{
    return new RecordByRecordCaffeParser();
}

ICaffeParserPtr ICaffeParser::Create()
{
    return ICaffeParserPtr(CreateRaw(), &ICaffeParser::Destroy);
}

void ICaffeParser::Destroy(ICaffeParser* parser)
{
    delete parser;
}

CaffeParserBase::CaffeParserBase()
    : m_Network(nullptr, nullptr)
{

}

CaffeParser::CaffeParser()
: CaffeParserBase()
{

}

BindingPointInfo CaffeParserBase::GetNetworkInputBindingInfo(const std::string& name) const
{
    return GetBindingInfo(name, "input", m_NetworkInputsBindingInfo);
}

BindingPointInfo CaffeParserBase::GetNetworkOutputBindingInfo(const std::string& name) const
{
    return GetBindingInfo(name, "output", m_NetworkOutputsBindingInfo);
}

std::pair<armnn::LayerBindingId, armnn::TensorInfo> CaffeParserBase::GetBindingInfo(const std::string& layerName,
    const char* bindingPointDesc,
    const std::unordered_map<std::string, BindingPointInfo>& nameToBindingInfo)
{
    auto it = nameToBindingInfo.find(layerName);
    if (it == nameToBindingInfo.end())
    {
        throw InvalidArgumentException(
            boost::str(
                boost::format(
                    "Unknown binding %1% for layer '%2%'. %3%") %
                    bindingPointDesc %
                    layerName %
                    CHECK_LOCATION().AsString()));
    }
    return it->second;
}

TensorInfo CaffeParserBase::BlobShapeToTensorInfo(const caffe::BlobShape& blobShape) const
{
    std::vector<unsigned int> shape;
    for (int j = 0; j < blobShape.dim_size(); ++j)
    {
        shape.push_back(static_cast<unsigned int>(blobShape.dim(j)));
    }

    return TensorInfo(boost::numeric_cast<unsigned int>(shape.size()), shape.data(), DataType::Float32);
}

BlobShape TensorDescToBlobShape(const TensorInfo& desc)
{
    BlobShape ret;
    for (unsigned int i = 0; i < desc.GetNumDimensions(); ++i)
    {
        ret.add_dim(i);
        ret.set_dim(boost::numeric_cast<int>(i), desc.GetShape()[i]);
    }

    return ret;
}

// Note: can move to CaffeParser when/if we optimise the text/string format
//       to load on a layer by layer basis
vector<const LayerParameter*> CaffeParserBase::GetInputs(const LayerParameter& layerParam)
{
    std::vector<const caffe::LayerParameter*> ret;
    ret.reserve(boost::numeric_cast<size_t>(layerParam.bottom_size()));
    for (int j = 0; j < layerParam.bottom_size(); ++j)
    {
        std::string inputName = layerParam.bottom(j);
        auto inputIt = m_CaffeLayersByTopName.find(inputName);
        if (inputIt == m_CaffeLayersByTopName.end())
        {
            throw ParseException(
                boost::str(
                    boost::format(
                        "Can't find Caffe layer with top called '%1%', "
                        "which is listed as an input of '%2%'. %3%") %
                        inputName %
                        layerParam.name() %
                        CHECK_LOCATION().AsString()));
        }
        ret.push_back(inputIt->second);
    }

    return ret;
}

void CaffeParserBase::ParseInputLayer(const LayerParameter& layerParam)
{
    BOOST_ASSERT(layerParam.type() == "Input");
    ValidateNumInputsOutputs(layerParam, 0, 1);

    const InputParameter& param = layerParam.input_param();

    const armnn::LayerBindingId inputId = boost::numeric_cast<armnn::LayerBindingId>(
        m_NetworkInputsBindingInfo.size());
    armnn::IConnectableLayer* const inputLayer = m_Network->AddInputLayer(inputId, layerParam.name().c_str());

    // Decides the tensor info for this input. This can be specified in the Caffe network but can also
    // be overriden by user input (m_inputShapes).
    armnn::TensorInfo inputTensorInfo;

    const BlobShape* originalShape = param.shape_size() > 0 && param.shape(0).dim_size() > 0 ?
        &param.shape(0) : nullptr;
    if (originalShape)
    {
        inputTensorInfo = BlobShapeToTensorInfo(*originalShape);
    }

    auto overrideIt = m_InputShapes.find(layerParam.name());
    if (overrideIt != m_InputShapes.end())
    {
        const TensorShape& overrideShape = overrideIt->second;
        if (originalShape &&
            (    originalShape->dim(1) != overrideShape[1]
              || originalShape->dim(2) != overrideShape[2]
              || originalShape->dim(3) != overrideShape[3]))
        {
            throw ParseException(
                boost::str(
                    boost::format(
                        "Parsed input shape for '%1%' is incompatible with the override provided. %2%") %
                        layerParam.name() %
                        CHECK_LOCATION().AsString()));
        }
        inputTensorInfo.SetShape(overrideShape);
    }
    else if (!originalShape)
    {
        throw ParseException(
            boost::str(
                boost::format(
                    "No input descriptor given for '%1%' and no input shape found in caffe model. %2%") %
                    layerParam.name() %
                    CHECK_LOCATION().AsString()));
    }

    TrackInputBinding(inputLayer, inputId, inputTensorInfo);
    inputLayer->GetOutputSlot(0).SetTensorInfo(inputTensorInfo);
    SetArmnnOutputSlotForCaffeTop(layerParam.top(0), inputLayer->GetOutputSlot(0));
}

void CaffeParserBase::AddConvLayerWithSplits(const caffe::LayerParameter& layerParam,
                                             const armnn::Convolution2dDescriptor& desc,
                                             unsigned int kernelW,
                                             unsigned int kernelH)
{
    BOOST_ASSERT(layerParam.type() == "Convolution");
    ValidateNumInputsOutputs(layerParam, 1, 1);

    ConvolutionParameter convParam = layerParam.convolution_param();
    BlobShape inputShape = TensorDescToBlobShape(GetArmnnOutputSlotForCaffeTop(layerParam.bottom(0)).GetTensorInfo());
    const unsigned int numGroups = convParam.has_group() ? convParam.group() : 1;

    // asusme these were already verified by the caller ParseConvLayer() function
    BOOST_ASSERT(numGroups < inputShape.dim(1));
    BOOST_ASSERT(numGroups > 1);

    // Handle grouping
    armnn::IOutputSlot& inputConnection = GetArmnnOutputSlotForCaffeTop(layerParam.bottom(0));

    vector<string> convLayerNames(numGroups);
    vector<armnn::IConnectableLayer*> convLayers(numGroups);
    convLayerNames[0] = layerParam.name();

    // This convolution is to be applied to chunks of the input data so add a splitter layer

    // Redirect the convolution input to the splitter
    unsigned int splitterDimSizes[4] = {static_cast<unsigned int>(inputShape.dim(0)),
                                        static_cast<unsigned int>(inputShape.dim(1)),
                                        static_cast<unsigned int>(inputShape.dim(2)),
                                        static_cast<unsigned int>(inputShape.dim(3))};

    // Split dimension 1 of the splitter output shape and conv input shapes
    // according to the number of groups

    splitterDimSizes[1] /= numGroups;
    inputShape.set_dim(1, splitterDimSizes[1]);

    // This is used to describe how the input is to be split
    ViewsDescriptor splitterDesc(numGroups);

    // Create an output node for each group, giving each a unique name
    for (unsigned int g = 0; g < numGroups; ++g)
    {
        // Work out the names of the splitter layers child convolutions
        stringstream ss;
        ss << layerParam.name() << "_" << g;
        convLayerNames[g] = ss.str();

        splitterDesc.SetViewOriginCoord(g, 1, splitterDimSizes[1] * g);

        // Set the size of the views.
        for (unsigned int dimIdx=0; dimIdx < 4; dimIdx++)
        {
            splitterDesc.SetViewSize(g, dimIdx, splitterDimSizes[dimIdx]);
        }
    }

    const std::string splitterLayerName = std::string("splitter_") + layerParam.bottom(0);
    armnn::IConnectableLayer* splitterLayer = m_Network->AddSplitterLayer(splitterDesc, splitterLayerName.c_str());

    inputConnection.Connect(splitterLayer->GetInputSlot(0));
    for (unsigned int i = 0; i < splitterLayer->GetNumOutputSlots(); i++)
    {
        splitterLayer->GetOutputSlot(i).SetTensorInfo(BlobShapeToTensorInfo(inputShape));
    }

    unsigned int numFilters = convParam.num_output();

    // Populates convolution output tensor descriptor dimensions.
    BlobShape outputShape;
    outputShape.add_dim(0);
    outputShape.set_dim(0, inputShape.dim(0));
    outputShape.add_dim(1);
    // Ensures that dimension 1 of the convolution output is split according to the number of groups.
    outputShape.set_dim(1, numFilters / numGroups);
    outputShape.add_dim(2);
    outputShape.set_dim(
        2, (static_cast<int>(
                static_cast<float>(inputShape.dim(2) + 2 * desc.m_PadBottom - kernelH) /
                static_cast<float>(desc.m_StrideY)) + 1));
    outputShape.add_dim(3);
    outputShape.set_dim(
        3, (static_cast<int>(
                static_cast<float>(inputShape.dim(3) + 2 * desc.m_PadRight - kernelW) /
                static_cast<float>(desc.m_StrideX)) + 1));

    // Load the weight data for ALL groups
    vector<float> weightData(boost::numeric_cast<size_t>(numGroups *
                                                         inputShape.dim(1) *  // number of input channels
                                                         outputShape.dim(1) * // number of output channels
                                                         kernelH *
                                                         kernelW));
    GetDataFromBlob(layerParam, weightData, 0);

    const unsigned int weightDimSizes[4] = {
        static_cast<unsigned int>(outputShape.dim(1)),
        static_cast<unsigned int>(inputShape.dim(1)),
        kernelH,
        kernelW};

    TensorInfo biasInfo;
    vector<float> biasData;

    if (desc.m_BiasEnabled)
    {
        biasData.resize(boost::numeric_cast<size_t>(numGroups * outputShape.dim(1)), 1.f);
        GetDataFromBlob(layerParam, biasData, 1);

        const unsigned int biasDimSizes[1] = {static_cast<unsigned int>(outputShape.dim(1))};
        biasInfo = TensorInfo(1, biasDimSizes, DataType::Float32);
    }

    const unsigned int numWeightsPerGroup = boost::numeric_cast<unsigned int>(weightData.size()) / numGroups;
    const unsigned int numBiasesPerGroup  = boost::numeric_cast<unsigned int>(biasData.size()) / numGroups;

    for (unsigned int g = 0; g < numGroups; ++g)
    {
        // Sets the slot index, group 0 should be connected to the 0th output of the splitter
        // group 1 should be connected to the 1st output of the splitter.

        // Pulls out the weights for this group from that loaded from the model file earlier.
        ConstTensor weights(TensorInfo(4, weightDimSizes, DataType::Float32),
                            weightData.data() + numWeightsPerGroup * g);

        IConnectableLayer* convLayer = nullptr;
        Optional<ConstTensor> optionalBiases;
        if (desc.m_BiasEnabled)
        {
            // Pulls out the biases for this group from that loaded from the model file earlier.
            ConstTensor biases(biasInfo, biasData.data() + numBiasesPerGroup * g);
            optionalBiases = Optional<ConstTensor>(biases);
        }
        convLayer = m_Network->AddConvolution2dLayer(desc,
                                                     weights,
                                                     optionalBiases,
                                                     convLayerNames[g].c_str());
        convLayers[g] = convLayer;

        // If we have more than one group then the input to the nth convolution the splitter layer's nth output,
        // otherwise it's the regular input to this layer.
        armnn::IOutputSlot& splitterInputConnection =
            splitterLayer ? splitterLayer->GetOutputSlot(g) : inputConnection;
        splitterInputConnection.Connect(convLayer->GetInputSlot(0));
        convLayer->GetOutputSlot(0).SetTensorInfo(BlobShapeToTensorInfo(outputShape));
    }

    // If the convolution was performed in chunks, add a layer to merge the results

    // The merge input shape matches that of the convolution output
    unsigned int mergeDimSizes[4] = {static_cast<unsigned int>(outputShape.dim(0)),
                                        static_cast<unsigned int>(outputShape.dim(1)),
                                        static_cast<unsigned int>(outputShape.dim(2)),
                                        static_cast<unsigned int>(outputShape.dim(3))};

    // This is used to describe how the input is to be merged
    OriginsDescriptor mergeDesc(numGroups);

    // Now create an input node for each group, using the name from
    // the output of the corresponding convolution
    for (unsigned int g = 0; g < numGroups; ++g)
    {
        mergeDesc.SetViewOriginCoord(g, 1, mergeDimSizes[1] * g);
    }

    // Make sure the output from the merge is the correct size to hold the data for all groups
    mergeDimSizes[1] *= numGroups;
    outputShape.set_dim(1, mergeDimSizes[1]);

    // Finally add the merge layer
    IConnectableLayer* mergerLayer = m_Network->AddConcatLayer(mergeDesc, layerParam.name().c_str());

    if (!mergerLayer)
    {
        throw ParseException(
            boost::str(
                boost::format(
                    "Failed to create final merger layer for Split+Convolution+Merger. "
                    "Layer=%1% #groups=%2% #filters=%3% %4%") %
                    layerParam.name() %
                    numGroups %
                    numFilters %
                    CHECK_LOCATION().AsString()));
    }

    for (unsigned int g = 0; g < numGroups; ++g)
    {
        convLayers[g]->GetOutputSlot(0).Connect(mergerLayer->GetInputSlot(g));
    }
    mergerLayer->GetOutputSlot(0).SetTensorInfo(armnn::TensorInfo(4, mergeDimSizes, DataType::Float32));
    SetArmnnOutputSlotForCaffeTop(layerParam.top(0), mergerLayer->GetOutputSlot(0));
}

void CaffeParserBase::AddConvLayerWithDepthwiseConv(const caffe::LayerParameter& layerParam,
                                                    const armnn::Convolution2dDescriptor& convDesc,
                                                    unsigned int kernelW,
                                                    unsigned int kernelH)
{
    BOOST_ASSERT(layerParam.type() == "Convolution");
    ValidateNumInputsOutputs(layerParam, 1, 1);

    ConvolutionParameter convParam  = layerParam.convolution_param();
    BlobShape inputShape = TensorDescToBlobShape(GetArmnnOutputSlotForCaffeTop(layerParam.bottom(0)).GetTensorInfo());

    DepthwiseConvolution2dDescriptor desc;
    desc.m_PadLeft      = convDesc.m_PadLeft;
    desc.m_PadRight     = convDesc.m_PadRight;
    desc.m_PadTop       = convDesc.m_PadTop;
    desc.m_PadBottom    = convDesc.m_PadBottom;
    desc.m_StrideX      = convDesc.m_StrideX;
    desc.m_StrideY      = convDesc.m_StrideY;
    desc.m_BiasEnabled  = convDesc.m_BiasEnabled;

    unsigned int numFilters = convParam.num_output();

    BlobShape outputShape;
    outputShape.add_dim(0);
    outputShape.set_dim(0, inputShape.dim(0));
    outputShape.add_dim(1);
    outputShape.set_dim(1, numFilters);
    outputShape.add_dim(2);
    outputShape.set_dim(
        2, (static_cast<int>(
                static_cast<float>(inputShape.dim(2) + 2 * desc.m_PadBottom - kernelH) /
                static_cast<float>(desc.m_StrideY)) + 1));
    outputShape.add_dim(3);
    outputShape.set_dim(
        3, (static_cast<int>(
                static_cast<float>(inputShape.dim(3) + 2 * desc.m_PadRight - kernelW) /
                static_cast<float>(desc.m_StrideX)) + 1));

    // Load the weight data
    size_t allWeightsSize = boost::numeric_cast<size_t>(inputShape.dim(1) * kernelH * kernelW);
    vector<float> weightData(allWeightsSize);

    GetDataFromBlob(layerParam, weightData, 0);

    // depth multiplier will be 1 for the depthwise convolution
    const unsigned int weightDimSizes[4] = {
        static_cast<unsigned int>(1),                 // depth multiplier
        static_cast<unsigned int>(inputShape.dim(1)), // #channels
        kernelH,
        kernelW};

    armnn::IConnectableLayer* returnLayer = nullptr;
    ConstTensor weights(TensorInfo(4, weightDimSizes, DataType::Float32), weightData.data());
    Optional<ConstTensor> optionalBiases;
    vector<float> biasData;
    if (desc.m_BiasEnabled)
    {
        TensorInfo biasInfo;

        biasData.resize(boost::numeric_cast<size_t>(outputShape.dim(1)), 1.f);
        GetDataFromBlob(layerParam, biasData, 1);

        const unsigned int biasDimSizes[1] = {static_cast<unsigned int>(outputShape.dim(1))};
        biasInfo = TensorInfo(1, biasDimSizes, DataType::Float32);

        ConstTensor biases(biasInfo, biasData.data());
        optionalBiases = Optional<ConstTensor>(biases);
    }
    returnLayer = m_Network->AddDepthwiseConvolution2dLayer(desc,
                                                            weights,
                                                            optionalBiases,
                                                            layerParam.name().c_str());

    if (!returnLayer)
    {
        throw ParseException(
            boost::str(
                boost::format(
                    "Failed to create depthwise convolution layer. "
                    "Layer=%1% #filters=%2% %3%") %
                    layerParam.name() %
                    numFilters %
                    CHECK_LOCATION().AsString()));
    }
    armnn::IOutputSlot& inputConnection = GetArmnnOutputSlotForCaffeTop(layerParam.bottom(0));
    inputConnection.Connect(returnLayer->GetInputSlot(0));
    returnLayer->GetOutputSlot(0).SetTensorInfo(BlobShapeToTensorInfo(outputShape));
    SetArmnnOutputSlotForCaffeTop(layerParam.top(0), returnLayer->GetOutputSlot(0));
}

void CaffeParserBase::ParseConvLayer(const LayerParameter& layerParam)
{
    // Ignored Caffe Parameters
    // * Dilation Size
    // * Weight Filler
    // * Bias Filler
    // * Engine
    // * Force nd_im2col
    // * Axis

    // Not Available ArmNN Interface Parameters
    // * Rounding policy;

    BOOST_ASSERT(layerParam.type() == "Convolution");
    ValidateNumInputsOutputs(layerParam, 1, 1);

    ConvolutionParameter convParam = layerParam.convolution_param();
    BlobShape inputShape = TensorDescToBlobShape(GetArmnnOutputSlotForCaffeTop(layerParam.bottom(0)).GetTensorInfo());
    const unsigned int numGroups = convParam.has_group() ? convParam.group() : 1;
    unsigned int numFilters = convParam.num_output();

    const auto notFound = std::numeric_limits<unsigned int>::max();

    unsigned int kernelH = GET_OPTIONAL_WITH_VECTOR_FALLBACK(convParam, ConvolutionParameter,
                                                             kernel_h, kernel_size, unsigned int, notFound);
    unsigned int kernelW = GET_OPTIONAL_WITH_VECTOR_FALLBACK(convParam, ConvolutionParameter,
                                                             kernel_w, kernel_size, unsigned int, notFound);

    unsigned int strideH = GET_OPTIONAL_WITH_VECTOR_FALLBACK(convParam, ConvolutionParameter,
                                                             stride_h, stride, unsigned int, 1u);
    unsigned int strideW = GET_OPTIONAL_WITH_VECTOR_FALLBACK(convParam, ConvolutionParameter,
                                                             stride_w, stride, unsigned int, 1u);

    unsigned int padH = GET_OPTIONAL_WITH_VECTOR_FALLBACK(convParam, ConvolutionParameter,
                                                          pad_h, pad, unsigned int, 0u);
    unsigned int padW = GET_OPTIONAL_WITH_VECTOR_FALLBACK(convParam, ConvolutionParameter,
                                                          pad_w, pad, unsigned int, 0u);

    Convolution2dDescriptor convolution2dDescriptor;
    convolution2dDescriptor.m_PadLeft     = padW;
    convolution2dDescriptor.m_PadRight    = padW;
    convolution2dDescriptor.m_PadTop      = padH;
    convolution2dDescriptor.m_PadBottom   = padH;
    convolution2dDescriptor.m_StrideX     = strideW;
    convolution2dDescriptor.m_StrideY     = strideH;
    convolution2dDescriptor.m_BiasEnabled = convParam.has_bias_term() ? convParam.bias_term() : true;

    if (numGroups > numFilters)
    {
        throw ParseException(
            boost::str(
                boost::format(
                    "Error parsing Convolution: %1%. "
                    "The 'group'=%2% parameter cannot be larger than the "
                    "number of filters supplied ='%3%'. %4%") %
                    layerParam.name() %
                    numGroups %
                    numFilters %
                    CHECK_LOCATION().AsString()));
    }

    if (inputShape.dim_size() != 4)
    {
        throw ParseException(
            boost::str(
                boost::format(
                    "Convolution input shape is expected to have 4 dimensions. "
                    "%1%'s input has only %2%. %3%") %
                    layerParam.name() %
                    inputShape.dim_size() %
                    CHECK_LOCATION().AsString()));
    }

    if (numGroups > 1)
    {
        if (numGroups > inputShape.dim(1))
        {
            throw ParseException(
                boost::str(
                    boost::format(
                        "Error parsing Convolution: %1%. "
                        "The 'group'=%2% parameter cannot be larger than the "
                        "channel of the input shape=%3% (in NCHW format). %4%") %
                        layerParam.name() %
                        numGroups %
                        inputShape.dim(1) %
                        CHECK_LOCATION().AsString()));
        }
        else if (numGroups == inputShape.dim(1))
        {
            // we use a depthwise convolution here, because the number of groups equals to the
            // input channels
            AddConvLayerWithDepthwiseConv(layerParam, convolution2dDescriptor, kernelW, kernelH);
            return;
        }
        else
        {
            // we split the input by channels into channels/groups separate convolutions
            // and merger the results afterwards
            AddConvLayerWithSplits(layerParam, convolution2dDescriptor, kernelW, kernelH);
            return;
        }
    }

    // NOTE: at this point we only need to handle #group=1 case, all other cases should be
    //       handled by the AddConvLayer* helpers

    // Populate convolution output tensor descriptor dimensions
    BlobShape outputShape;
    outputShape.add_dim(0);
    outputShape.set_dim(0, inputShape.dim(0));
    outputShape.add_dim(1);
    outputShape.set_dim(1, numFilters);
    outputShape.add_dim(2);
    outputShape.set_dim(
        2, (static_cast<int>(
                static_cast<float>(inputShape.dim(2) + 2 * padH - kernelH) /
                static_cast<float>(strideH)) + 1));
    outputShape.add_dim(3);
    outputShape.set_dim(
        3, (static_cast<int>(
                static_cast<float>(inputShape.dim(3) + 2 * padW - kernelW) /
                static_cast<float>(strideW)) + 1));

    // Load the weight data for ALL groups
    vector<float> weightData(boost::numeric_cast<size_t>(inputShape.dim(1) *
                                                         outputShape.dim(1) *
                                                         kernelH *
                                                         kernelW));
    GetDataFromBlob(layerParam, weightData, 0);

    const unsigned int weightDimSizes[4] = {
        static_cast<unsigned int>(outputShape.dim(1)), // output channels
        static_cast<unsigned int>(inputShape.dim(1)),  // input channels
        kernelH,
        kernelW};

    armnn::IConnectableLayer* returnLayer = nullptr;

    // Pull out the weights for this group from that loaded from the model file earlier
    ConstTensor weights(TensorInfo(4, weightDimSizes, DataType::Float32), weightData.data());
    Optional<ConstTensor> optionalBiases;
    vector<float> biasData;
    if (convolution2dDescriptor.m_BiasEnabled)
    {
        TensorInfo biasInfo;

        biasData.resize(boost::numeric_cast<size_t>(outputShape.dim(1)), 1.f);
        GetDataFromBlob(layerParam, biasData, 1);

        const unsigned int biasDimSizes[1] = {static_cast<unsigned int>(outputShape.dim(1))};
        biasInfo = TensorInfo(1, biasDimSizes, DataType::Float32);

        // Pull out the biases for this group from that loaded from the model file earlier
        ConstTensor biases(biasInfo, biasData.data());
        optionalBiases = Optional<ConstTensor>(biases);
    }
    returnLayer = m_Network->AddConvolution2dLayer(convolution2dDescriptor,
                                                   weights,
                                                   optionalBiases,
                                                   layerParam.name().c_str());

    armnn::IOutputSlot& inputConnection = GetArmnnOutputSlotForCaffeTop(layerParam.bottom(0));
    inputConnection.Connect(returnLayer->GetInputSlot(0));
    returnLayer->GetOutputSlot(0).SetTensorInfo(BlobShapeToTensorInfo(outputShape));

    if (!returnLayer)
    {
        throw ParseException(
            boost::str(
                boost::format(
                    "Failed to create Convolution layer. "
                    "Layer=%1% #groups=%2% #filters=%3% %4%") %
                    layerParam.name() %
                    numGroups %
                    numFilters %
                    CHECK_LOCATION().AsString()));
    }

    SetArmnnOutputSlotForCaffeTop(layerParam.top(0), returnLayer->GetOutputSlot(0));
}

void CaffeParserBase::ParsePoolingLayer(const LayerParameter& layerParam)
{
    // Ignored Caffe Parameters
    //      Stochastic Pooling
    //      Engine

    ValidateNumInputsOutputs(layerParam, 1, 1);
    PoolingParameter param = layerParam.pooling_param();
    const TensorInfo& inputInfo = GetArmnnOutputSlotForCaffeTop(layerParam.bottom(0)).GetTensorInfo();

    const auto notFound = std::numeric_limits<unsigned int>::max();

    unsigned int kernel_h = GET_OPTIONAL_WITH_FALLBACK(param, PoolingParameter,
                                                       kernel_h, kernel_size, unsigned int, notFound);
    unsigned int kernel_w = GET_OPTIONAL_WITH_FALLBACK(param, PoolingParameter,
                                                       kernel_w, kernel_size, unsigned int, notFound);

    if ((kernel_h == notFound || kernel_w == notFound) && param.has_global_pooling())
    {
        kernel_h = inputInfo.GetShape()[2];
        kernel_w = inputInfo.GetShape()[3];
    }

    unsigned int stride_h = GET_OPTIONAL_WITH_FALLBACK(param, PoolingParameter,
                                                       stride_h, stride, unsigned int, notFound);
    unsigned int stride_w = GET_OPTIONAL_WITH_FALLBACK(param, PoolingParameter,
                                                       stride_h, stride, unsigned int, notFound);

    if ((stride_h == notFound || stride_w == notFound) && param.has_global_pooling())
    {
        stride_h = 1;
        stride_w = 1;
    }

    unsigned int pad_h = GET_OPTIONAL_WITH_FALLBACK(param, PoolingParameter,
                                                    pad_h, pad, unsigned int, 0u);
    unsigned int pad_w = GET_OPTIONAL_WITH_FALLBACK(param, PoolingParameter,
                                                    pad_w, pad, unsigned int, 0u);

    // Populate Weight and Bias Filter Descriptor
    Pooling2dDescriptor pooling2dDescriptor;
    if (param.has_pool())
    {
        PoolingParameter_PoolMethod p = param.pool();
        switch (p)
        {
            case PoolingParameter_PoolMethod_MAX:
            {
                pooling2dDescriptor.m_PoolType = PoolingAlgorithm::Max;
                break;
            }
            case PoolingParameter_PoolMethod_AVE:
            {
                pooling2dDescriptor.m_PoolType = PoolingAlgorithm::Average;
                break;
            }
            case PoolingParameter_PoolMethod_STOCHASTIC:
            {
                throw ParseException(
                    boost::str(
                        boost::format(
                            "Pooling Layer: Stochastic Pooling Not Supported. Layer=%1% %2%") %
                            layerParam.name() %
                            CHECK_LOCATION().AsString()));
            }
            default:
            {
                throw ParseException(
                    boost::str(
                        boost::format(
                            "Pooling Layer: unknown pooling method: %1% for layer: %2% %3%") %
                            p %
                            layerParam.name() %
                            CHECK_LOCATION().AsString()));
            }
        }
    }
    else
    {
        throw ParseException(
            boost::str(
                boost::format(
                    "No Pooling Method Defined for %1% %2%") %
                    layerParam.name() %
                    CHECK_LOCATION().AsString()));
    }

    pooling2dDescriptor.m_PadLeft     = pad_w;
    pooling2dDescriptor.m_PadRight    = pad_w;
    pooling2dDescriptor.m_PadTop      = pad_h;
    pooling2dDescriptor.m_PadBottom   = pad_h;
    pooling2dDescriptor.m_StrideX     = stride_w;
    pooling2dDescriptor.m_StrideY     = stride_h;
    pooling2dDescriptor.m_PoolWidth   = kernel_w;
    pooling2dDescriptor.m_PoolHeight  = kernel_h;

    pooling2dDescriptor.m_OutputShapeRounding = OutputShapeRounding::Ceiling;
    pooling2dDescriptor.m_PaddingMethod  = PaddingMethod::IgnoreValue;

    armnn::IConnectableLayer* poolingLayer = m_Network->AddPooling2dLayer(pooling2dDescriptor,
        layerParam.name().c_str());

    TensorInfo outputInfo(
        { inputInfo.GetShape()[0],
          inputInfo.GetShape()[1],
          static_cast<unsigned int>(ceil(
              static_cast<float>(inputInfo.GetShape()[2] + 2 * pad_h - kernel_h) /
              boost::numeric_cast<float>(stride_h))) + 1,
          static_cast<unsigned int>(ceil(
              static_cast<float>(inputInfo.GetShape()[3] + 2 * pad_w - kernel_w) /
              boost::numeric_cast<float>(stride_w))) + 1 },
        DataType::Float32);

    GetArmnnOutputSlotForCaffeTop(layerParam.bottom(0)).Connect(poolingLayer->GetInputSlot(0));
    poolingLayer->GetOutputSlot(0).SetTensorInfo(outputInfo);
    SetArmnnOutputSlotForCaffeTop(layerParam.top(0), poolingLayer->GetOutputSlot(0));
}

void CaffeParserBase::ParseReluLayer(const LayerParameter& layerParam)
{
    ValidateNumInputsOutputs(layerParam, 1, 1);

    const string& name = layerParam.name();
    const ReLUParameter& param = layerParam.relu_param();

    ActivationDescriptor activationDescriptor;
    const float negativeSlope = param.negative_slope();
    if (negativeSlope == 0.0f)
    {
        activationDescriptor.m_Function = ActivationFunction::ReLu;
    }
    else
    {
        activationDescriptor.m_Function = ActivationFunction::LeakyReLu;
        activationDescriptor.m_A = negativeSlope;
    }

    const TensorInfo& inputInfo = GetArmnnOutputSlotForCaffeTop(layerParam.bottom(0)).GetTensorInfo();
    IConnectableLayer* const activationLayer = m_Network->AddActivationLayer(activationDescriptor, name.c_str());
    GetArmnnOutputSlotForCaffeTop(layerParam.bottom(0)).Connect(activationLayer->GetInputSlot(0));
    activationLayer->GetOutputSlot(0).SetTensorInfo(inputInfo);
    SetArmnnOutputSlotForCaffeTop(layerParam.top(0), activationLayer->GetOutputSlot(0));
}

void CaffeParserBase::ParseLRNLayer(const LayerParameter& layerParam)
{
    ValidateNumInputsOutputs(layerParam, 1, 1);

    LRNParameter param = layerParam.lrn_param();

    const TensorInfo& inputInfo = GetArmnnOutputSlotForCaffeTop(layerParam.bottom(0)).GetTensorInfo();

    // Ignored BATCH NORMALIZATION Caffe Parameters.
    // Ignored MVN Caffe Parameters.
    // Ignored LRN Caffe Parameters.
    //      Engine

    NormalizationDescriptor normalizationDescriptor;
    if (param.has_norm_region())
    {
        LRNParameter_NormRegion n = param.norm_region();
        switch (n)
        {
            case LRNParameter_NormRegion_ACROSS_CHANNELS:
            {
                normalizationDescriptor.m_NormChannelType = NormalizationAlgorithmChannel::Across;
                break;
            }
            case LRNParameter_NormRegion_WITHIN_CHANNEL:
            {
                normalizationDescriptor.m_NormChannelType = NormalizationAlgorithmChannel::Within;
                break;
            }
            default:
            {
                throw ParseException(
                    boost::str(
                        boost::format(
                            "Unknown region %1% for LRN layer %2% %3%") %
                            n %
                            layerParam.name() %
                            CHECK_LOCATION().AsString()));
            }
        }
    }
    else
    {
        // Caffe defaults to normalization across channels.
        normalizationDescriptor.m_NormChannelType = NormalizationAlgorithmChannel::Across;
    }

    normalizationDescriptor.m_NormMethodType = NormalizationAlgorithmMethod::LocalBrightness;
    if (param.has_local_size())
    {
        normalizationDescriptor.m_NormSize = param.local_size();
    }
    else
    {
        throw ParseException(
            boost::str(
                boost::format(
                    "local_size not defined for LRN layer %1% %2%") %
                    layerParam.name() %
                    CHECK_LOCATION().AsString()));
    }

    if (param.has_alpha())
    {
        normalizationDescriptor.m_Alpha = param.alpha();
        normalizationDescriptor.m_Alpha /= boost::numeric_cast<float>(param.local_size());
    }
    else
    {
        throw ParseException(
            boost::str(
                boost::format(
                    "Alpha not defined for LRN layer %1% %2%") %
                    layerParam.name() %
                    CHECK_LOCATION().AsString()));
    }
    if (param.has_beta())
    {
        normalizationDescriptor.m_Beta = param.beta();
    }
    else
    {
        throw ParseException(
            boost::str(
                boost::format(
                    "Beta not defined for LRN layer %1% %2%") %
                    layerParam.name() %
                    CHECK_LOCATION().AsString()));
    }

    if (param.has_k())
    {
        normalizationDescriptor.m_K = param.k();
    }
    else
    {
        normalizationDescriptor.m_K = 1;
    }

    IConnectableLayer* const normLayer = m_Network->AddNormalizationLayer(normalizationDescriptor,
        layerParam.name().c_str());
    GetArmnnOutputSlotForCaffeTop(layerParam.bottom(0)).Connect(normLayer->GetInputSlot(0));
    normLayer->GetOutputSlot(0).SetTensorInfo(inputInfo);

    SetArmnnOutputSlotForCaffeTop(layerParam.top(0), normLayer->GetOutputSlot(0));
}

void CaffeParserBase::ParseInnerProductLayer(const LayerParameter& layerParam)
{
    InnerProductParameter param = layerParam.inner_product_param();

    ValidateNumInputsOutputs(layerParam, 1, 1);

    unsigned int outputSize = param.num_output();

    // Ignored Caffe Parameters:
    // Weight Filler
    // Bias Filler
    // Engine
    // Axis

    FullyConnectedDescriptor tensorFullyConnectedDescriptor;

    if (param.has_transpose())
    {
        // If true, assumes transposed weights.
        tensorFullyConnectedDescriptor.m_TransposeWeightMatrix = param.transpose();
    }
    else
    {
        // Caffe defaults to transposed.
        tensorFullyConnectedDescriptor.m_TransposeWeightMatrix = true;
    }

    const TensorInfo& inputInfo = GetArmnnOutputSlotForCaffeTop(layerParam.bottom(0)).GetTensorInfo();

    TensorInfo weightInfo;
    TensorInfo biasInfo;

    // Allows implicit flattening of extra dimensions.
    unsigned int inputSize = inputInfo.GetShape()[1];
    for (unsigned int i = 2; i < inputInfo.GetNumDimensions(); ++i)
    {
        inputSize *= inputInfo.GetShape()[i];
    }

    const float* weightDataPtr = GetArrayPtrFromBlob(layerParam, 0);
    const unsigned int swTD[2] = { outputSize, inputSize };
    ConstTensor weights(TensorInfo(2, swTD, DataType::Float32), weightDataPtr);

    tensorFullyConnectedDescriptor.m_BiasEnabled = true;
    // Todo: check whether bias enabled.
    armnn::IConnectableLayer* fullyConnectedLayer = nullptr;
    if (tensorFullyConnectedDescriptor.m_BiasEnabled)
    {
        // BIAS VALUE
        const float* biasDataPtr = GetArrayPtrFromBlob(layerParam, 1);

        const unsigned int sbTD[1] = { outputSize };

        ConstTensor biases(TensorInfo(1, sbTD, DataType::Float32), biasDataPtr);

        fullyConnectedLayer = m_Network->AddFullyConnectedLayer(tensorFullyConnectedDescriptor,
                                                                weights,
                                                                Optional<ConstTensor>(biases),
                                                                layerParam.name().c_str());
    }
    else
    {
        fullyConnectedLayer = m_Network->AddFullyConnectedLayer(tensorFullyConnectedDescriptor,
                                                                weights,
                                                                EmptyOptional(),
                                                                layerParam.name().c_str());
    }

    TensorInfo outputInfo({ inputInfo.GetShape()[0], outputSize }, DataType::Float32);
    GetArmnnOutputSlotForCaffeTop(layerParam.bottom(0)).Connect(fullyConnectedLayer->GetInputSlot(0));
    fullyConnectedLayer->GetOutputSlot(0).SetTensorInfo(outputInfo);
    SetArmnnOutputSlotForCaffeTop(layerParam.top(0), fullyConnectedLayer->GetOutputSlot(0));
}

void CaffeParserBase::ParseSoftmaxLayer(const LayerParameter& layerParam)
{
    ValidateNumInputsOutputs(layerParam, 1, 1);

    SoftmaxParameter param = layerParam.softmax_param();

    const TensorInfo& inputInfo = GetArmnnOutputSlotForCaffeTop(layerParam.bottom(0)).GetTensorInfo();

    // Ignored Caffe Parameters:
    //      axis
    //      Engine

    armnn::SoftmaxDescriptor softmaxDescriptor;
    armnn::IConnectableLayer* const softmaxLayer = m_Network->AddSoftmaxLayer(
        softmaxDescriptor,
        layerParam.name().c_str());
    GetArmnnOutputSlotForCaffeTop(layerParam.bottom(0)).Connect(softmaxLayer->GetInputSlot(0));
    softmaxLayer->GetOutputSlot(0).SetTensorInfo(inputInfo);
    SetArmnnOutputSlotForCaffeTop(layerParam.top(0), softmaxLayer->GetOutputSlot(0));
}

void CaffeParserBase::ParseEltwiseLayer(const LayerParameter& layerParam)
{
    ValidateNumInputsOutputs(layerParam, 2, 1);

    const TensorInfo& inputInfo = GetArmnnOutputSlotForCaffeTop(layerParam.bottom(0)).GetTensorInfo();

    // Ignored Caffe Parameters:
    //      coeff

    EltwiseParameter_EltwiseOp operation = EltwiseParameter_EltwiseOp_SUM; // Defaults to sum as per caffe.

    if (layerParam.has_eltwise_param() && layerParam.eltwise_param().has_operation())
    {
        operation = layerParam.eltwise_param().operation();
    }

    armnn::IConnectableLayer* newLayer = nullptr;
    switch (operation)
    {
        case EltwiseParameter_EltwiseOp_SUM:
        {
            newLayer = m_Network->AddAdditionLayer(layerParam.name().c_str());
            break;
        }
        case EltwiseParameter_EltwiseOp_PROD:
        {
            newLayer = m_Network->AddMultiplicationLayer(layerParam.name().c_str());
            break;
        }
        default:
        {
            throw ParseException(
                boost::str(
                    boost::format(
                        "Unsupported operation %1% in Eltwise layer %2% %3%") %
                        operation %
                        layerParam.name() %
                        CHECK_LOCATION().AsString()));
        }
    }

    GetArmnnOutputSlotForCaffeTop(layerParam.bottom(0)).Connect(newLayer->GetInputSlot(0));
    GetArmnnOutputSlotForCaffeTop(layerParam.bottom(1)).Connect(newLayer->GetInputSlot(1));
    newLayer->GetOutputSlot(0).SetTensorInfo(inputInfo);
    SetArmnnOutputSlotForCaffeTop(layerParam.top(0), newLayer->GetOutputSlot(0));
}

void CaffeParserBase::ParseConcatLayer(const LayerParameter& layerParam)
{
    unsigned int numInputs = static_cast<unsigned int>(layerParam.bottom_size());
    // We assume concat happens along the channel dimension, which is 1 in (0, 1, 2, 3).
    unsigned int concatDim = 1;
    unsigned int numOfDims = 4;

    // we only consider 4-D tensor here
    OriginsDescriptor concatDescriptor(static_cast<uint32_t>(numInputs), numOfDims);
    std::vector<unsigned int>mergeDimSizes(numOfDims, 0u);

    unsigned int mergeDim = 0;
    for (unsigned int viewIndex = 0; viewIndex < numInputs; ++viewIndex)
    {
        const TensorInfo& inputInfo = GetArmnnOutputSlotForCaffeTop(
            layerParam.bottom(boost::numeric_cast<int>(viewIndex))).GetTensorInfo();
        // Checks whether the dimensions of the input tensors are actually 4.
        if (inputInfo.GetNumDimensions()!=4)
        {
            throw ParseException(
                boost::str(
                    boost::format(
                        "The number of dimensions for input tensors of "
                        "the concatenation op should be 4. Inputs of %1% has "
                        "%2% dimensions. %3%") %
                        layerParam.name() %
                        inputInfo.GetNumDimensions() %
                        CHECK_LOCATION().AsString()));
        }

        mergeDimSizes[0] = inputInfo.GetShape()[0];
        mergeDimSizes[1] = inputInfo.GetShape()[1];
        mergeDimSizes[2] = inputInfo.GetShape()[2];
        mergeDimSizes[3] = inputInfo.GetShape()[3];

        for (unsigned int j = 0; j < concatDim; ++j)
        {
            concatDescriptor.SetViewOriginCoord(viewIndex, j, 0);
        }

        concatDescriptor.SetViewOriginCoord(viewIndex, concatDim, mergeDim);
        mergeDim += mergeDimSizes[concatDim];

        for (unsigned int j = concatDim+1; j < numOfDims; ++j)
        {
            concatDescriptor.SetViewOriginCoord(viewIndex, j, 0);
        }
    }
    mergeDimSizes[concatDim] = mergeDim;

    armnn::IConnectableLayer* concatlayer = m_Network->AddConcatLayer(concatDescriptor, layerParam.name().c_str());
    for (unsigned int i = 0; i < numInputs; ++i)
    {
        armnn::IOutputSlot& outputSlot = GetArmnnOutputSlotForCaffeTop(layerParam.bottom(boost::numeric_cast<int>(i)));
        outputSlot.Connect(concatlayer->GetInputSlot(i));
    }

    concatlayer->GetOutputSlot(0).SetTensorInfo(armnn::TensorInfo(numOfDims, mergeDimSizes.data(), DataType::Float32));
    SetArmnnOutputSlotForCaffeTop(layerParam.top(0), concatlayer->GetOutputSlot(0));
}

void CaffeParserBase::ParseBatchNormLayer(const LayerParameter& layerParam)
{
    ValidateNumInputsOutputs(layerParam, 1, 1);

    const TensorInfo& inputInfo = GetArmnnOutputSlotForCaffeTop(layerParam.bottom(0)).GetTensorInfo();

    string name = layerParam.name();

    BatchNormParameter param = layerParam.batch_norm_param();
    // If use_global_stats is not explicitly set in the model, assume it to be true (its default value
    // when the network is in the testing phase).
    if (param.has_use_global_stats())
    {
        if (!param.use_global_stats())
        {
            throw ParseException(
                boost::str(
                    boost::format(
                        "Error parsing Batch Norm layer '%1%': "
                        "Parameter 'use_global_stats' is set to false, which is "
                        "unsupported (value used for training). %2%") %
                        name %
                        CHECK_LOCATION().AsString()));
        }
    }

    BatchNormalizationDescriptor desc;
    desc.m_Eps = param.eps();

    unsigned int channels = inputInfo.GetShape()[1];
    unsigned int shape[]  = {channels};

    vector<float> meanData(channels);
    GetDataFromBlob(layerParam, meanData, 0);

    vector<float> varianceData(channels);
    GetDataFromBlob(layerParam, varianceData, 1);

    // Reads moving average factor and applies scaling (if required).
    const BlobProto& blob = layerParam.blobs(boost::numeric_cast<int>(2));
    const float movingAverageFactor = blob.data(boost::numeric_cast<int>(0));
    if(movingAverageFactor != 0.0f)
    {
        const float scaleFactor = 1.0f / movingAverageFactor;
        auto scaleFunction = [scaleFactor](float f) -> float { return f * scaleFactor; };

        std::transform(varianceData.begin(), varianceData.end(), varianceData.begin(), scaleFunction);
        std::transform(meanData.begin(), meanData.end(), meanData.begin(), scaleFunction);
    }

    // Identifies scale operation.
    vector<float> betaData(channels, 0.0f);
    vector<float> gammaData(channels, 1.0f);

    ConstTensor mean(TensorInfo(1, shape, armnn::DataType::Float32), meanData);
    ConstTensor variance(TensorInfo(1, shape, armnn::DataType::Float32), varianceData);
    ConstTensor beta(TensorInfo(1, shape, armnn::DataType::Float32), betaData);
    ConstTensor gamma(TensorInfo(1, shape, armnn::DataType::Float32), gammaData);

    armnn::IConnectableLayer* const batchNormLayer = m_Network->AddBatchNormalizationLayer(desc,
        mean, variance, beta, gamma, name.c_str());
    GetArmnnOutputSlotForCaffeTop(layerParam.bottom(0)).Connect(batchNormLayer->GetInputSlot(0));
    batchNormLayer->GetOutputSlot(0).SetTensorInfo(inputInfo);
    SetArmnnOutputSlotForCaffeTop(layerParam.top(0), batchNormLayer->GetOutputSlot(0));
}

void CaffeParserBase::ParseScaleLayer(const LayerParameter& layerParam)
{
    // Current unoptimal solution: add a batchnormalization layer with 0 mean and 1 variance.
    ValidateNumInputsOutputs(layerParam, 1, 1);

    const TensorInfo& inputInfo = GetArmnnOutputSlotForCaffeTop(layerParam.bottom(0)).GetTensorInfo();

    string name = layerParam.name();

    ScaleParameter param = layerParam.scale_param();
    if (param.axis() != 1)
    {
        // Would have to use something other than BatchNormalizationLayer in this case
        throw ParseException(
            boost::str(
                boost::format(
                    "Loading Scale Layer: Only axis 1 is supported currently. "
                    "Layer=%1% Axis=%2% %3%") %
                    layerParam.name() %
                    param.axis() %
                    CHECK_LOCATION().AsString()));
    }

    unsigned int     channels = inputInfo.GetShape()[1];
    unsigned int     shape[]  = {channels};

    BatchNormalizationDescriptor desc;
    desc.m_Eps = 0.0f; // Don't need epsilon if variance is 1.
    vector<float> meanData(channels, 0.0f);
    vector<float> varianceData(channels, 1.0f);
    vector<float> betaData(channels, 0.0f);
    vector<float> gammaData(channels);

    GetDataFromBlob(layerParam, gammaData, 0);

    if(param.has_bias_term())
    {
        GetDataFromBlob(layerParam, betaData, 1);
    }

    ConstTensor mean(TensorInfo(1, shape, armnn::DataType::Float32), meanData);
    ConstTensor variance(TensorInfo(1, shape, armnn::DataType::Float32), varianceData);
    ConstTensor beta(TensorInfo(1, shape, armnn::DataType::Float32), betaData);
    ConstTensor gamma(TensorInfo(1, shape, armnn::DataType::Float32), gammaData);

    armnn::IConnectableLayer* const batchNormLayer = m_Network->AddBatchNormalizationLayer(desc,
        mean, variance, beta, gamma, name.c_str());
    GetArmnnOutputSlotForCaffeTop(layerParam.bottom(0)).Connect(batchNormLayer->GetInputSlot(0));
    batchNormLayer->GetOutputSlot(0).SetTensorInfo(inputInfo);
    SetArmnnOutputSlotForCaffeTop(layerParam.top(0), batchNormLayer->GetOutputSlot(0));
}

void CaffeParserBase::ParseSplitLayer(const caffe::LayerParameter& layerParam)
{
    // Used in caffe to duplicate memory - not necessary in armnn.
    if (layerParam.bottom_size() != 1)
    {
        throw ParseException(
            boost::str(
                boost::format(
                    "Split layer '%1%' should have exactly 1 bottom. "
                    "#bottoms=%2% %3%") %
                    layerParam.name() %
                    layerParam.bottom_size() %
                    CHECK_LOCATION().AsString()));
    }
    armnn::IOutputSlot& outputSlot = GetArmnnOutputSlotForCaffeTop(layerParam.bottom(0));
    for (int i = 0; i < layerParam.top_size(); i++)
    {
        SetArmnnOutputSlotForCaffeTop(layerParam.top(i), outputSlot);
    }
}

void CaffeParserBase::ParseDropoutLayer(const caffe::LayerParameter& layerParam)
{
    // Ignored for inference, so patch the single input to its single output.
    if (layerParam.bottom_size() != 1 || layerParam.top_size() != 1)
    {
        throw ParseException(
            boost::str(
                boost::format(
                    "Dropout layer '%1%' should have exactly 1 bottom and 1 top. "
                    "#bottoms=%2% #tops=%3% %4%") %
                    layerParam.name() %
                    layerParam.bottom_size() %
                    layerParam.top_size() %
                    CHECK_LOCATION().AsString()));
    }
    SetArmnnOutputSlotForCaffeTop(layerParam.top(0), GetArmnnOutputSlotForCaffeTop(layerParam.bottom(0)));
}

void CaffeParserBase::TrackInputBinding(armnn::IConnectableLayer* layer,
    armnn::LayerBindingId id,
    const armnn::TensorInfo& tensorInfo)
{
    return TrackBindingPoint(layer, id, tensorInfo, layer->GetName(), m_NetworkInputsBindingInfo);
}

void CaffeParserBase::TrackOutputBinding(armnn::IConnectableLayer* layer,
    armnn::LayerBindingId id,
    const armnn::TensorInfo& tensorInfo)
{
    return TrackBindingPoint(layer, id, tensorInfo, layer->GetName(), m_NetworkOutputsBindingInfo);
}

void CaffeParserBase::TrackBindingPoint(armnn::IConnectableLayer* layer,
    armnn::LayerBindingId id,
    const armnn::TensorInfo& tensorInfo,
    const char* bindingPointDesc,
    std::unordered_map<std::string, BindingPointInfo>& nameToBindingInfo)
{
    const std::string layerName = layer->GetName();
    auto it = nameToBindingInfo.find(layerName);
    if (it == nameToBindingInfo.end())
    {
        nameToBindingInfo[layerName] = std::make_pair(id, tensorInfo);
    }
    else
    {
        throw ParseException(
            boost::str(
                boost::format(
                    "Id %1% used by more than one %2% layer %3%") %
                    id %
                    bindingPointDesc %
                    CHECK_LOCATION().AsString()));
    }
}

armnn::IOutputSlot& CaffeParserBase::GetArmnnOutputSlotForCaffeTop(const std::string& caffeTopName) const
{
    auto it = m_ArmnnOutputSlotForCaffeTop.find(caffeTopName);
    if (it != m_ArmnnOutputSlotForCaffeTop.end())
    {
        return *it->second;
    }
    else
    {
        throw ParseException(
            boost::str(
                boost::format(
                    "Could not find armnn output slot for Caffe top '%1%' %2%") %
                    caffeTopName %
                    CHECK_LOCATION().AsString()));
    }
}

void CaffeParserBase::SetArmnnOutputSlotForCaffeTop(
    const std::string& caffeTopName, armnn::IOutputSlot& armnnOutputSlot)
{
    auto it = m_ArmnnOutputSlotForCaffeTop.find(caffeTopName);
    if (it == m_ArmnnOutputSlotForCaffeTop.end())
    {
        m_ArmnnOutputSlotForCaffeTop[caffeTopName] = &armnnOutputSlot;
    }
    else
    {
        throw ParseException(
            boost::str(
                boost::format(
                    "Attempting to add duplicate entry for Caffe top '%1%' %2%") %
                    caffeTopName %
                    CHECK_LOCATION().AsString()));
    }
}

// Note: can move to CaffeParser when/if we optimise the text/string format
//       to load on a layer by layer basis
void CaffeParserBase::ResolveInPlaceLayers(caffe::NetParameter& netParameter)
{
    // Finds layers with the same top.
    std::map<std::string, std::vector<caffe::LayerParameter*>> layersByTop;
    for (int layerIdx = 0; layerIdx < netParameter.layer_size(); ++layerIdx)
    {
        caffe::LayerParameter& layer = *netParameter.mutable_layer(layerIdx);
        std::string name = layer.name();
        for (int i = 0; i < layer.top_size(); ++i)
        {
            layersByTop[layer.top(i)].push_back(&layer);
        }
    }

    // For each set of layers with the same top, resolves them to a linear chain rather than in-place layers.
    // Note that for 'regular' layers, there will be a single layer in each group and so this will be a no-op.
    for (auto layersWithSameTopIt : layersByTop)
    {
        const std::string& top = layersWithSameTopIt.first;
        const std::vector<caffe::LayerParameter*>& layersWithSameTop = layersWithSameTopIt.second;

        // Chains the layers together in the order that they are listed in the prototxt (hopefully this is correct).
        // Note that the last layer will not have its top modified so that other layers will continue to reference it.
        for (unsigned int layerIdx = 0; layerIdx < layersWithSameTop.size() - 1; ++layerIdx)
        {
            caffe::LayerParameter& layer1 = *layersWithSameTop[layerIdx];
            caffe::LayerParameter& layer2 = *layersWithSameTop[layerIdx+1];
            if (layer1.top_size() != 1)
            {
                throw ParseException(
                    boost::str(
                        boost::format(
                            "Node '%1%' is an in-place layer but doesn't have exactly one "
                            "top. It has %2% instead. %3%") %
                            layer1.name() %
                            layer1.top_size() %
                            CHECK_LOCATION().AsString()));
            }
            std::string newTop = layer1.name() + "_top";
            layer1.set_top(0, newTop);
            if (layer2.bottom_size() != 1 || layer2.bottom(0) != top)
            {
                throw ParseException(
                    boost::str(
                        boost::format(
                            "Node '%1%' is an in-place layer but "
                            "doesn't have exactly one bottom, or it doesn't match its top. "
                            "#bottoms=%2%, first bottom is %3%, top is %4% %5%") %
                            layer2.name() %
                            layer2.bottom(0) %
                            top %
                            CHECK_LOCATION().AsString()));
            }
            layer2.set_bottom(0, newTop);
        }
    }
}

// Note: can move to CaffeParser when/if we optimise the text/string format
//       to load on a layer by layer basis
void CaffeParserBase::LoadNetParam(NetParameter& netParameter)
{
    // Caffe models sometimes have an implicit input layer.
    // In that case, add an explicit one.
    if (netParameter.input_size() > 0)
    {
        LayerParameter* newLayer = netParameter.add_layer();

        newLayer->set_type("Input");
        newLayer->set_name(netParameter.input(0));
        newLayer->add_top(netParameter.input(0));

        InputParameter* inputParam = newLayer->mutable_input_param();
        BlobShape* shape = inputParam->add_shape();

        int dim_size = netParameter.input_dim_size();
        for (int i = 0; i < dim_size; ++i)
        {
            shape->add_dim(netParameter.input_dim(i));
        }
    }

    // Replaces in-place layers with regular ones to make the rest of the parsing easier.
    ResolveInPlaceLayers(netParameter);

    // Creates a lookup of Caffe layers by name.
    for (int i = 0; i < netParameter.layer_size(); ++i)
    {
        const caffe::LayerParameter& layer = netParameter.layer(i);
        for (int i = 0; i < layer.top_size(); ++i)
        {
            m_CaffeLayersByTopName[layer.top(i)] = &layer;
        }
    }

    // Finds the output layers the user requested.
    std::vector<const caffe::LayerParameter*> targetLayers;
    for (const std::string& requestedOutputName : m_RequestedOutputs)
    {
        auto nodeIt = m_CaffeLayersByTopName.find(requestedOutputName);
        if (nodeIt == m_CaffeLayersByTopName.end())
        {
            throw ParseException(
                boost::str(
                    boost::format(
                        "Couldn't find requested output layer '%1%' in graph %2%") %
                        requestedOutputName %
                        CHECK_LOCATION().AsString()));
        }
        targetLayers.push_back(nodeIt->second);
    }

    // Sorts them into a linear ordering such that all inputs of a node are before the node itself.
    std::vector<const caffe::LayerParameter*> sortedNodes;
    if (!armnnUtils::GraphTopologicalSort<const caffe::LayerParameter*>(
        targetLayers,
        [this](const caffe::LayerParameter* node)
        {
            return GetInputs(*node);
        },
        sortedNodes))
    {
        throw ParseException(
            boost::str(
                boost::format(
                    "Cycle detected in graph. #nodes: %1% %2%") %
                    sortedNodes.size() %
                    CHECK_LOCATION().AsString()));
    }

    // Parses each node in order, knowing that all inputs of a node will be processed before the node itself.
    for (const caffe::LayerParameter* current : sortedNodes)
    {
        auto it = ms_CaffeLayerNameToParsingFunctions.find(current->type());
        if (it == ms_CaffeLayerNameToParsingFunctions.end())
        {
            throw ParseException(
                boost::str(
                    boost::format("Unsupported layer type: '%1%' for layer %2% %3%") %
                    current->type() %
                    current->name() %
                    CHECK_LOCATION().AsString()));
        }
        auto func = it->second;
        (this->*func)(*current);
    }

    // Adds ArmNN output layers connected to each requested output.
    for (const std::string& requestedOutput : m_RequestedOutputs)
    {
        armnn::IOutputSlot& outputSlot = GetArmnnOutputSlotForCaffeTop(requestedOutput);

        const armnn::LayerBindingId outputId = boost::numeric_cast<armnn::LayerBindingId>(
            m_NetworkOutputsBindingInfo.size());
        armnn::IConnectableLayer* const outputLayer = m_Network->AddOutputLayer(outputId, requestedOutput.c_str());
        outputSlot.Connect(outputLayer->GetInputSlot(0));

        TrackOutputBinding(outputLayer, outputId, outputLayer->GetInputSlot(0).GetConnection()->GetTensorInfo());
    }
}

INetworkPtr CaffeParserBase::CreateNetworkFromTextFile(const char* graphFile,
    const std::map<std::string, armnn::TensorShape>& inputShapes,
    const std::vector<std::string>& requestedOutputs)
{
    FILE* fd = fopen(graphFile, "r");

    if (fd == nullptr)
    {
        throw FileNotFoundException(
            boost::str(
                boost::format(
                    "Failed to open graph file: %1% %2%") %
                    graphFile %
                    CHECK_LOCATION().AsString()));
    }

    // Parses the file into a message.
    NetParameter netParam;
    auto         input   = new google::protobuf::io::FileInputStream(fileno(fd));
    bool         success = google::protobuf::TextFormat::Parse(input, &netParam);
    delete input;
    fclose(fd);

    if (!success)
    {
        throw ParseException(
            boost::str(
                boost::format(
                    "Failed to parse graph file: %1% %2%") %
                    graphFile %
                    CHECK_LOCATION().AsString()));
    }

    return CreateNetworkFromNetParameter(netParam, inputShapes, requestedOutputs);
}

INetworkPtr CaffeParserBase::CreateNetworkFromString(const char* protoText,
    const std::map<std::string, armnn::TensorShape>& inputShapes,
    const std::vector<std::string>& requestedOutputs)
{
    // Parses the string into a message.
    NetParameter netParam;
    bool         success = google::protobuf::TextFormat::ParseFromString(protoText, &netParam);

    if (!success)
    {
        throw ParseException(
            boost::str(
                boost::format(
                    "Failed to parse graph string %1%") %
                    CHECK_LOCATION().AsString()));
    }

    return CreateNetworkFromNetParameter(netParam, inputShapes, requestedOutputs);
}

INetworkPtr CaffeParser::CreateNetworkFromBinaryFile(const char* graphFile,
    const std::map<std::string, armnn::TensorShape>& inputShapes,
    const std::vector<std::string>& requestedOutputs)
{
    FILE* fd = fopen(graphFile, "rb");

    if (fd == nullptr)
    {
        throw FileNotFoundException(
            boost::str(
                boost::format(
                    "Failed to open graph file at: %1% %2%") %
                    graphFile %
                    CHECK_LOCATION().AsString()));
    }

    // Parses the file into a message.
    NetParameter netParam;

    FileInputStream  inStream(fileno(fd));
    CodedInputStream codedStream(&inStream);
    codedStream.SetTotalBytesLimit(INT_MAX, INT_MAX);
    bool success = netParam.ParseFromCodedStream(&codedStream);
    fclose(fd);

    if (!success)
    {
        throw ParseException(
            boost::str(
                boost::format(
                    "Failed to parse protobuf file: %1% %2%") %
                    graphFile %
                    CHECK_LOCATION().AsString()));
    }

    return CreateNetworkFromNetParameter(netParam, inputShapes, requestedOutputs);
}

// Note: can move to CaffeParser when/if we optimise the text/string format
//       to load on a layer by layer basis
INetworkPtr CaffeParserBase::CreateNetworkFromNetParameter(NetParameter& netParam,
    const std::map<std::string, armnn::TensorShape>& inputShapes,
    const std::vector<std::string>& requestedOutputs)
{
    m_NetworkInputsBindingInfo.clear();
    m_NetworkOutputsBindingInfo.clear();

    m_Network = INetwork::Create();

    m_InputShapes = inputShapes;
    if (requestedOutputs.size() == 0)
    {
        throw ParseException("requestedOutputs must have at least one entry");
    }
    m_RequestedOutputs = requestedOutputs;

    try
    {
        LoadNetParam(netParam);
    }
    catch (const ParseException& e)
    {
        Cleanup();
        throw e;
    }

    Cleanup();

    return move(m_Network);
}

void CaffeParserBase::Cleanup() {
    // cleanup, in case we reuse this parser
    m_InputShapes.clear();
    m_RequestedOutputs.clear();
    m_ArmnnOutputSlotForCaffeTop.clear();
    // NOTE: when we get the text/string format
    //       optimised for memory then this data structure can
    //       also move to the CaffeParser class
    m_CaffeLayersByTopName.clear();
}

}
