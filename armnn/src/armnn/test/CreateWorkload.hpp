//
// Copyright © 2017 Arm Ltd. All rights reserved.
// SPDX-License-Identifier: MIT
//
#pragma once

#include <boost/test/unit_test.hpp>

#include <boost/cast.hpp>

#include <backendsCommon/WorkloadData.hpp>
#include <backendsCommon/WorkloadFactory.hpp>
#include <backendsCommon/CpuTensorHandle.hpp>

#include <Graph.hpp>
#include <DataLayoutIndexed.hpp>
#include <Network.hpp>
#include <ResolveType.hpp>

#include <utility>


using namespace armnn;

namespace
{

using namespace std;

// Calls CreateWorkload for a layer, and checks the returned pointer is of the correct type.
template<typename Workload>
std::unique_ptr<Workload> MakeAndCheckWorkload(Layer& layer, Graph& graph, const IWorkloadFactory& factory)
{
    std::unique_ptr<IWorkload> workload = layer.CreateWorkload(graph, factory);
    BOOST_TEST(workload.get() == boost::polymorphic_downcast<Workload*>(workload.get()),
               "Cannot convert to derived class");
    std::string reasonIfUnsupported;
    layer.SetBackendId(factory.GetBackendId());
    BOOST_TEST(factory.IsLayerSupported(layer, layer.GetDataType(), reasonIfUnsupported));
    return std::unique_ptr<Workload>(static_cast<Workload*>(workload.release()));
}

// Connects two layers.
void Connect(Layer* from, Layer* to, const TensorInfo& tensorInfo, unsigned int fromIndex = 0, unsigned int toIndex = 0)
{
    from->GetOutputSlot(fromIndex).Connect(to->GetInputSlot(toIndex));
    from->GetOutputHandler(fromIndex).SetTensorInfo(tensorInfo);
}

// Helper function to create tensor handlers for workloads, assuming they all use the same factory.
void CreateTensorHandles(armnn::Graph& graph, armnn::IWorkloadFactory& factory)
{
    for (auto&& layer : graph.TopologicalSort())
    {
        layer->CreateTensorHandles(graph, factory);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////
// The following functions are called by backendsCommon/test/CreateWorkload*.cpp
// They build very simple graphs, and then create a workload.
// Some checks are performed on the workload to ensure parameters have been passed correctly.
// They return the created workloads so that backend-specific checks can be performed.
/////////////////////////////////////////////////////////////////////////////////////////////

template <typename ActivationWorkload, armnn::DataType DataType>
std::unique_ptr<ActivationWorkload> CreateActivationWorkloadTest(armnn::IWorkloadFactory& factory,
                                                                 armnn::Graph&            graph)
{
    // Creates the layer we're testing.
    ActivationDescriptor layerDesc;
    layerDesc.m_Function = ActivationFunction::Abs;
    layerDesc.m_A        = 3.5f;
    layerDesc.m_B        = -10.0f;

    ActivationLayer* const layer = graph.AddLayer<ActivationLayer>(layerDesc, "layer");

    // Creates extra layers.
    Layer* const input = graph.AddLayer<InputLayer>(0, "input");
    Layer* const output = graph.AddLayer<OutputLayer>(0, "output");

    // Connects up.
    armnn::TensorInfo tensorInfo({1, 1}, DataType);

    Connect(input, layer, tensorInfo);
    Connect(layer, output, tensorInfo);

    CreateTensorHandles(graph, factory);

    // Makes the workload and checks it.
    auto workload = MakeAndCheckWorkload<ActivationWorkload>(*layer, graph, factory);

    ActivationQueueDescriptor queueDescriptor = workload->GetData();
    BOOST_TEST(queueDescriptor.m_Inputs.size() == 1);
    BOOST_TEST(queueDescriptor.m_Outputs.size() == 1);
    BOOST_TEST(queueDescriptor.m_Parameters.m_A == 3.5f);
    BOOST_TEST(queueDescriptor.m_Parameters.m_B == -10.0f);
    BOOST_TEST((queueDescriptor.m_Parameters.m_Function == ActivationFunction::Abs));

    // Returns so we can do extra, backend-specific tests.
    return workload;
}

template <typename WorkloadType,
          typename DescriptorType,
          typename LayerType,
          armnn::DataType DataType>
std::unique_ptr<WorkloadType> CreateElementwiseWorkloadTest(armnn::IWorkloadFactory & factory,
                                                            armnn::Graph & graph)
{
    // Creates the layer we're testing.
    Layer* const layer = graph.AddLayer<LayerType>("layer");

    // Creates extra layers.
    Layer* const input1 = graph.AddLayer<InputLayer>(1, "input1");
    Layer* const input2 = graph.AddLayer<InputLayer>(2, "input2");
    Layer* const output = graph.AddLayer<OutputLayer>(0, "output");

    // Connects up.
    armnn::TensorInfo tensorInfo({2, 3}, DataType);
    Connect(input1, layer, tensorInfo, 0, 0);
    Connect(input2, layer, tensorInfo, 0, 1);
    Connect(layer, output, tensorInfo);
    CreateTensorHandles(graph, factory);

    // Makes the workload and checks it.
    auto workload = MakeAndCheckWorkload<WorkloadType>(*layer, graph, factory);

    DescriptorType queueDescriptor = workload->GetData();
    BOOST_TEST(queueDescriptor.m_Inputs.size() == 2);
    BOOST_TEST(queueDescriptor.m_Outputs.size() == 1);

    // Returns so we can do extra, backend-specific tests.
    return workload;
}

template <typename BatchNormalizationFloat32Workload, armnn::DataType DataType>
std::unique_ptr<BatchNormalizationFloat32Workload> CreateBatchNormalizationWorkloadTest(
    armnn::IWorkloadFactory& factory, armnn::Graph& graph, DataLayout dataLayout = DataLayout::NCHW)
{

    TensorShape tensorShape;
    switch (dataLayout)
    {
        case DataLayout::NHWC:
            tensorShape = { 2, 4, 4, 3 };
            break;
        case DataLayout::NCHW:
        default:
            tensorShape = { 2, 3, 4, 4 };
    }

    // Creates the layer we're testing.
    BatchNormalizationDescriptor layerDesc;
    layerDesc.m_Eps = 0.05f;
    layerDesc.m_DataLayout = dataLayout;

    BatchNormalizationLayer* const layer = graph.AddLayer<BatchNormalizationLayer>(layerDesc, "layer");

    armnn::TensorInfo weightInfo({3}, DataType);
    layer->m_Mean     = std::make_unique<ScopedCpuTensorHandle>(weightInfo);
    layer->m_Variance = std::make_unique<ScopedCpuTensorHandle>(weightInfo);
    layer->m_Beta     = std::make_unique<ScopedCpuTensorHandle>(weightInfo);
    layer->m_Gamma    = std::make_unique<ScopedCpuTensorHandle>(weightInfo);
    layer->m_Mean->Allocate();
    layer->m_Variance->Allocate();
    layer->m_Beta->Allocate();
    layer->m_Gamma->Allocate();

    // Creates extra layers.
    Layer* const input = graph.AddLayer<InputLayer>(0, "input");
    Layer* const output = graph.AddLayer<OutputLayer>(0, "output");

    //Connects up.
    armnn::TensorInfo tensorInfo(tensorShape, DataType);
    Connect(input, layer, tensorInfo);
    Connect(layer, output, tensorInfo);
    CreateTensorHandles(graph, factory);

    // Makes the workload and checks it.
    auto workload = MakeAndCheckWorkload<BatchNormalizationFloat32Workload>(*layer, graph, factory);
    BatchNormalizationQueueDescriptor queueDescriptor = workload->GetData();
    BOOST_TEST(queueDescriptor.m_Parameters.m_Eps == 0.05f);
    BOOST_TEST(queueDescriptor.m_Inputs.size() == 1);
    BOOST_TEST(queueDescriptor.m_Outputs.size() == 1);
    BOOST_TEST((queueDescriptor.m_Mean->GetTensorInfo() == TensorInfo({3}, DataType)));
    BOOST_TEST((queueDescriptor.m_Variance->GetTensorInfo() == TensorInfo({3}, DataType)));
    BOOST_TEST((queueDescriptor.m_Gamma->GetTensorInfo() == TensorInfo({3}, DataType)));
    BOOST_TEST((queueDescriptor.m_Beta->GetTensorInfo() == TensorInfo({3}, DataType)));
    BOOST_TEST((queueDescriptor.m_Parameters.m_DataLayout == dataLayout));

    // Returns so we can do extra, backend-specific tests.
    return workload;
}

template <typename Convolution2dWorkload, armnn::DataType DataType>
std::unique_ptr<Convolution2dWorkload> CreateConvolution2dWorkloadTest(armnn::IWorkloadFactory& factory,
                                                                       armnn::Graph&            graph,
                                                                       DataLayout dataLayout = DataLayout::NCHW)
{
    // Creates the layer we're testing.
    Convolution2dDescriptor layerDesc;
    layerDesc.m_PadLeft = 3;
    layerDesc.m_PadRight = 3;
    layerDesc.m_PadTop = 1;
    layerDesc.m_PadBottom = 1;
    layerDesc.m_StrideX = 2;
    layerDesc.m_StrideY = 4;
    layerDesc.m_BiasEnabled = true;
    layerDesc.m_DataLayout = dataLayout;

    Convolution2dLayer* const layer = graph.AddLayer<Convolution2dLayer>(layerDesc, "layer");

    TensorShape weightShape = (dataLayout == DataLayout::NCHW) ? TensorShape{2, 3, 5, 3} : TensorShape{2, 5, 3, 3};
    TensorShape inputShape  = (dataLayout == DataLayout::NCHW) ? TensorShape{2, 3, 8, 16} : TensorShape{2, 8, 16, 3};
    TensorShape outputShape = (dataLayout == DataLayout::NCHW) ? TensorShape{2, 2, 2, 10} : TensorShape{2, 2, 10, 2};

    layer->m_Weight = std::make_unique<ScopedCpuTensorHandle>(TensorInfo(weightShape, DataType));
    layer->m_Bias   = std::make_unique<ScopedCpuTensorHandle>(TensorInfo({2}, GetBiasDataType(DataType)));

    layer->m_Weight->Allocate();
    layer->m_Bias->Allocate();

    // Creates extra layers.
    Layer* const input = graph.AddLayer<InputLayer>(0, "input");
    Layer* const output = graph.AddLayer<OutputLayer>(0, "output");

    // Connects up.
    Connect(input, layer, TensorInfo(inputShape, DataType));
    Connect(layer, output, TensorInfo(outputShape, DataType));
    CreateTensorHandles(graph, factory);

    // Makes the workload and checks it.
    auto workload = MakeAndCheckWorkload<Convolution2dWorkload>(*layer, graph, factory);

    Convolution2dQueueDescriptor queueDescriptor = workload->GetData();
    BOOST_TEST(queueDescriptor.m_Parameters.m_StrideX == 2);
    BOOST_TEST(queueDescriptor.m_Parameters.m_StrideY == 4);
    BOOST_TEST(queueDescriptor.m_Parameters.m_PadLeft == 3);
    BOOST_TEST(queueDescriptor.m_Parameters.m_PadRight == 3);
    BOOST_TEST(queueDescriptor.m_Parameters.m_PadTop == 1);
    BOOST_TEST(queueDescriptor.m_Parameters.m_PadBottom == 1);
    BOOST_TEST(queueDescriptor.m_Parameters.m_BiasEnabled);
    BOOST_TEST((queueDescriptor.m_Parameters.m_DataLayout == dataLayout));

    BOOST_TEST(queueDescriptor.m_Inputs.size() == 1);
    BOOST_TEST(queueDescriptor.m_Outputs.size() == 1);
    BOOST_TEST((queueDescriptor.m_Weight->GetTensorInfo() == TensorInfo(weightShape, DataType)));
    BOOST_TEST((queueDescriptor.m_Bias->GetTensorInfo() ==
        TensorInfo({2}, GetBiasDataType(DataType))));

    // Returns so we can do extra, backend-specific tests.
    return workload;
}

template <typename LstmWorkload>
std::unique_ptr<LstmWorkload> CreateLstmWorkloadTest(armnn::IWorkloadFactory& factory, armnn::Graph& graph)
{
    // This parameter setting is for withCifgWithPeepholeNoProjection
    LstmDescriptor layerDesc;
    layerDesc.m_ActivationFunc = 4;
    layerDesc.m_ClippingThresCell = 0.0f;
    layerDesc.m_ClippingThresProj = 0.0f;
    layerDesc.m_CifgEnabled = true;
    layerDesc.m_PeepholeEnabled = true;
    layerDesc.m_ProjectionEnabled = false;

    LstmLayer* const layer = graph.AddLayer<LstmLayer>(layerDesc, "layer");
    unsigned int batchSize = 2;
    unsigned int inputSize = 2;
    unsigned int numUnits = 4;
    unsigned int outputSize = 4;

    layer->m_BasicParameters.m_InputToForgetWeights = std::make_unique<ScopedCpuTensorHandle>
            (TensorInfo({ numUnits, inputSize }, DataType::Float32));
    layer->m_BasicParameters.m_InputToCellWeights = std::make_unique<ScopedCpuTensorHandle>
            (TensorInfo({ numUnits, inputSize }, DataType::Float32));
    layer->m_BasicParameters.m_InputToOutputWeights = std::make_unique<ScopedCpuTensorHandle>
            (TensorInfo({ numUnits, inputSize }, DataType::Float32));
    layer->m_BasicParameters.m_RecurrentToForgetWeights = std::make_unique<ScopedCpuTensorHandle>
            (TensorInfo({ numUnits, outputSize }, DataType::Float32));
    layer->m_BasicParameters.m_RecurrentToCellWeights = std::make_unique<ScopedCpuTensorHandle>
            (TensorInfo({ numUnits, outputSize }, DataType::Float32));
    layer->m_BasicParameters.m_RecurrentToOutputWeights = std::make_unique<ScopedCpuTensorHandle>
            (TensorInfo({ numUnits, outputSize }, DataType::Float32));
    layer->m_BasicParameters.m_ForgetGateBias = std::make_unique<ScopedCpuTensorHandle>
            (TensorInfo({ numUnits }, DataType::Float32));
    layer->m_BasicParameters.m_CellBias = std::make_unique<ScopedCpuTensorHandle>
            (TensorInfo({ numUnits }, DataType::Float32));
    layer->m_BasicParameters.m_OutputGateBias = std::make_unique<ScopedCpuTensorHandle>
            (TensorInfo({ numUnits }, DataType::Float32));

    layer->m_BasicParameters.m_InputToForgetWeights->Allocate();
    layer->m_BasicParameters.m_InputToCellWeights->Allocate();
    layer->m_BasicParameters.m_InputToOutputWeights->Allocate();
    layer->m_BasicParameters.m_RecurrentToForgetWeights->Allocate();
    layer->m_BasicParameters.m_RecurrentToCellWeights->Allocate();
    layer->m_BasicParameters.m_RecurrentToOutputWeights->Allocate();
    layer->m_BasicParameters.m_ForgetGateBias->Allocate();
    layer->m_BasicParameters.m_CellBias->Allocate();
    layer->m_BasicParameters.m_OutputGateBias->Allocate();


    if (layerDesc.m_PeepholeEnabled)
    {
        layer->m_PeepholeParameters.m_CellToForgetWeights = std::make_unique<ScopedCpuTensorHandle>
                (TensorInfo({ numUnits }, DataType::Float32));
        layer->m_PeepholeParameters.m_CellToOutputWeights = std::make_unique<ScopedCpuTensorHandle>
                (TensorInfo({ numUnits }, DataType::Float32));
        layer->m_PeepholeParameters.m_CellToForgetWeights->Allocate();
        layer->m_PeepholeParameters.m_CellToOutputWeights->Allocate();
    }

    // create input and output layers
    Layer* const input = graph.AddLayer<InputLayer>(0, "input");
    Layer* const outputStateIn = graph.AddLayer<InputLayer>(1, "outputStateIn");
    Layer* const cellStateIn = graph.AddLayer<InputLayer>(2, "cellStateIn");
    Layer* const scratchBuffer = graph.AddLayer<OutputLayer>(0, "scratchBuffer");
    Layer* const outputStateOut = graph.AddLayer<OutputLayer>(1, "outputStateOut");
    Layer* const cellStateOut = graph.AddLayer<OutputLayer>(2, "cellStateOut");
    Layer* const output = graph.AddLayer<OutputLayer>(3, "output");

    // connect up
    armnn::TensorInfo lstmTensorInfo1({ batchSize, inputSize }, DataType::Float32);
    armnn::TensorInfo lstmTensorInfo2({ batchSize, numUnits}, DataType::Float32);
    armnn::TensorInfo lstmTensorInfo3({ batchSize, outputSize }, DataType::Float32);
    armnn::TensorInfo lstmTensorInfoScratchBuff({ batchSize, numUnits * (layerDesc.m_CifgEnabled ? 3 : 4) },
                                                DataType::Float32);
    Connect(input, layer, lstmTensorInfo1, 0, 0);
    Connect(cellStateIn, layer, lstmTensorInfo2, 0, 1);
    Connect(outputStateIn, layer, lstmTensorInfo3, 0, 2);
    Connect(layer, scratchBuffer, lstmTensorInfoScratchBuff, 0, 0);
    Connect(layer, outputStateOut, lstmTensorInfo3, 1, 0);
    Connect(layer, cellStateOut, lstmTensorInfo2, 2, 0);
    Connect(layer, output, lstmTensorInfo3, 3, 0);

    CreateTensorHandles(graph, factory);

    // make the workload and check it
    auto workload = MakeAndCheckWorkload<LstmWorkload>(*layer, graph, factory);
    LstmQueueDescriptor queueDescriptor = workload->GetData();
    BOOST_TEST(queueDescriptor.m_Parameters.m_ActivationFunc == 4);
    BOOST_TEST(queueDescriptor.m_Parameters.m_ClippingThresCell == 0.0f);
    BOOST_TEST(queueDescriptor.m_Parameters.m_ClippingThresProj == 0.0f);
    BOOST_TEST(queueDescriptor.m_Inputs.size() == 3);
    BOOST_TEST(queueDescriptor.m_Outputs.size() == 4);

    BOOST_TEST((queueDescriptor.m_InputToForgetWeights->GetTensorInfo() == TensorInfo({ numUnits, inputSize },
                                                                                     DataType::Float32)));
    BOOST_TEST((queueDescriptor.m_OutputGateBias->GetTensorInfo() == TensorInfo({ numUnits },
                                                                                     DataType::Float32)));
    BOOST_TEST((queueDescriptor.m_CellBias->GetTensorInfo() == TensorInfo({ numUnits }, DataType::Float32)));
    return workload;
}

template <typename Convolution2dWorkload, armnn::DataType DataType>
std::unique_ptr<Convolution2dWorkload> CreateDirectConvolution2dWorkloadTest(armnn::IWorkloadFactory& factory,
                                                                       armnn::Graph&            graph)
{
    // Creates the layer we're testing.
    Convolution2dDescriptor layerDesc;
    layerDesc.m_PadLeft = 1;
    layerDesc.m_PadRight = 1;
    layerDesc.m_PadTop = 1;
    layerDesc.m_PadBottom = 1;
    layerDesc.m_StrideX = 1;
    layerDesc.m_StrideY = 1;
    layerDesc.m_BiasEnabled = true;

    Convolution2dLayer* const layer = graph.AddLayer<Convolution2dLayer>(layerDesc, "layer");

    float inputsQScale = DataType == armnn::DataType::QuantisedAsymm8 ? 1.0f : 0.0;
    float outputQScale = DataType == armnn::DataType::QuantisedAsymm8 ? 2.0f : 0.0;

    layer->m_Weight = std::make_unique<ScopedCpuTensorHandle>(TensorInfo({ 2, 3, 3, 3 }, DataType, inputsQScale));
    layer->m_Bias   = std::make_unique<ScopedCpuTensorHandle>
        (TensorInfo({2},  GetBiasDataType(DataType), inputsQScale));
    layer->m_Weight->Allocate();
    layer->m_Bias->Allocate();

    // Creates extra layers.
    Layer* const input = graph.AddLayer<InputLayer>(0, "input");
    Layer* const output = graph.AddLayer<OutputLayer>(0, "output");

    // Connects up.
    Connect(input, layer, TensorInfo({2, 3, 6, 6}, DataType, inputsQScale));
    Connect(layer, output, TensorInfo({2, 2, 6, 6}, DataType, outputQScale));
    CreateTensorHandles(graph, factory);

    // Makes the workload and checks it.
    auto workload = MakeAndCheckWorkload<Convolution2dWorkload>(*layer, graph, factory);

    Convolution2dQueueDescriptor queueDescriptor = workload->GetData();
    BOOST_TEST(queueDescriptor.m_Parameters.m_StrideX == 1);
    BOOST_TEST(queueDescriptor.m_Parameters.m_StrideY == 1);
    BOOST_TEST(queueDescriptor.m_Parameters.m_PadLeft == 1);
    BOOST_TEST(queueDescriptor.m_Parameters.m_PadRight == 1);
    BOOST_TEST(queueDescriptor.m_Parameters.m_PadTop == 1);
    BOOST_TEST(queueDescriptor.m_Parameters.m_PadBottom == 1);
    BOOST_TEST(queueDescriptor.m_Parameters.m_BiasEnabled == true);

    BOOST_TEST(queueDescriptor.m_Inputs.size() == 1);
    BOOST_TEST(queueDescriptor.m_Outputs.size() == 1);
    BOOST_TEST((queueDescriptor.m_Weight->GetTensorInfo() == TensorInfo({2, 3, 3, 3},
        DataType, inputsQScale)));
    BOOST_TEST((queueDescriptor.m_Bias->GetTensorInfo()
                == TensorInfo({2},  GetBiasDataType(DataType), inputsQScale)));

    // Returns so we can do extra, backend-specific tests.
    return workload;
}

template <typename DepthwiseConvolution2dFloat32Workload, armnn::DataType DataType>
std::unique_ptr<DepthwiseConvolution2dFloat32Workload> CreateDepthwiseConvolution2dWorkloadTest(
    armnn::IWorkloadFactory& factory, armnn::Graph& graph, DataLayout dataLayout = DataLayout::NCHW)
{
    // Creates the layer we're testing.
    DepthwiseConvolution2dDescriptor layerDesc;
    layerDesc.m_PadLeft     = 1;
    layerDesc.m_PadRight    = 2;
    layerDesc.m_PadTop      = 1;
    layerDesc.m_PadBottom   = 2;
    layerDesc.m_StrideX     = 1;
    layerDesc.m_StrideY     = 1;
    layerDesc.m_BiasEnabled = false;
    layerDesc.m_DataLayout  = dataLayout;

    DepthwiseConvolution2dLayer* const layer = graph.AddLayer<DepthwiseConvolution2dLayer>(layerDesc, "layer");

    layer->m_Weight = std::make_unique<ScopedCpuTensorHandle>(TensorInfo({1, 2, 4, 4}, DataType)); // [ M, I, H, W ]
    layer->m_Weight->Allocate();

    // Creates extra layers.
    Layer* const input = graph.AddLayer<InputLayer>(0, "input");
    Layer* const output = graph.AddLayer<OutputLayer>(0, "output");

    TensorShape inputShape = (dataLayout == DataLayout::NCHW) ?
                TensorShape{ 2, 2, 5, 5 } : TensorShape{ 2, 5, 5, 2 };
    TensorShape outputShape = (dataLayout == DataLayout::NCHW) ?
                TensorShape{ 2, 2, 5, 5 } : TensorShape{ 2, 5, 5, 2 };

    // Connects up.
    Connect(input, layer, TensorInfo(inputShape, DataType));
    Connect(layer, output, TensorInfo(outputShape, DataType));
    CreateTensorHandles(graph, factory);

    // Makes the workload and checks it.
    auto workload = MakeAndCheckWorkload<DepthwiseConvolution2dFloat32Workload>(*layer, graph, factory);

    DepthwiseConvolution2dQueueDescriptor queueDescriptor = workload->GetData();
    BOOST_TEST(queueDescriptor.m_Parameters.m_StrideX == 1);
    BOOST_TEST(queueDescriptor.m_Parameters.m_StrideY == 1);
    BOOST_TEST(queueDescriptor.m_Parameters.m_PadLeft == 1);
    BOOST_TEST(queueDescriptor.m_Parameters.m_PadRight == 2);
    BOOST_TEST(queueDescriptor.m_Parameters.m_PadTop == 1);
    BOOST_TEST(queueDescriptor.m_Parameters.m_PadBottom == 2);
    BOOST_TEST(queueDescriptor.m_Parameters.m_BiasEnabled == false);
    BOOST_TEST((queueDescriptor.m_Parameters.m_DataLayout == dataLayout));

    BOOST_TEST(queueDescriptor.m_Inputs.size() == 1);
    BOOST_TEST(queueDescriptor.m_Outputs.size() == 1);
    BOOST_TEST((queueDescriptor.m_Weight->GetTensorInfo() == TensorInfo({1, 2, 4, 4}, DataType)));

    // Returns so we can do extra, backend-specific tests.
    return workload;
}

template <typename FullyConnectedWorkload, armnn::DataType DataType>
std::unique_ptr<FullyConnectedWorkload> CreateFullyConnectedWorkloadTest(armnn::IWorkloadFactory& factory,
                                                                         armnn::Graph&            graph)
{
    // Creates the layer we're testing.
    FullyConnectedDescriptor layerDesc;
    layerDesc.m_BiasEnabled = true;
    layerDesc.m_TransposeWeightMatrix = true;

    FullyConnectedLayer* const layer = graph.AddLayer<FullyConnectedLayer>(layerDesc, "layer");

    float inputsQScale = DataType == armnn::DataType::QuantisedAsymm8 ? 1.0f : 0.0;
    float outputQScale = DataType == armnn::DataType::QuantisedAsymm8 ? 2.0f : 0.0;

    layer->m_Weight = std::make_unique<ScopedCpuTensorHandle>(TensorInfo({7, 20}, DataType, inputsQScale, 0));
    layer->m_Bias   = std::make_unique<ScopedCpuTensorHandle>(TensorInfo({7}, GetBiasDataType(DataType), inputsQScale));
    layer->m_Weight->Allocate();
    layer->m_Bias->Allocate();

    // Creates extra layers.
    Layer* const input = graph.AddLayer<InputLayer>(0, "input");
    Layer* const output = graph.AddLayer<OutputLayer>(0, "output");

    // Connects up.
    Connect(input, layer, TensorInfo({3, 1, 4, 5}, DataType, inputsQScale));
    Connect(layer, output, TensorInfo({3, 7}, DataType, outputQScale));
    CreateTensorHandles(graph, factory);

    // Makes the workload and checks it.
    auto workload = MakeAndCheckWorkload<FullyConnectedWorkload>(*layer, graph, factory);

    FullyConnectedQueueDescriptor queueDescriptor = workload->GetData();
    BOOST_TEST(queueDescriptor.m_Parameters.m_BiasEnabled == true);
    BOOST_TEST(queueDescriptor.m_Parameters.m_TransposeWeightMatrix == true);

    BOOST_TEST(queueDescriptor.m_Inputs.size() == 1);
    BOOST_TEST(queueDescriptor.m_Outputs.size() == 1);
    BOOST_TEST((queueDescriptor.m_Weight->GetTensorInfo() == TensorInfo({7, 20}, DataType, inputsQScale)));
    BOOST_TEST((queueDescriptor.m_Bias->GetTensorInfo() == TensorInfo({7}, GetBiasDataType(DataType), inputsQScale)));

    // Returns so we can do extra, backend-specific tests.
    return workload;
}

template <typename NormalizationWorkload, armnn::DataType DataType>
std::unique_ptr<NormalizationWorkload> CreateNormalizationWorkloadTest(armnn::IWorkloadFactory& factory,
                                                                       armnn::Graph& graph,
                                                                       DataLayout dataLayout = DataLayout::NCHW)
{
    // Creates the layer we're testing.
    NormalizationDescriptor layerDesc;
    layerDesc.m_NormChannelType = NormalizationAlgorithmChannel::Across;
    layerDesc.m_NormMethodType = NormalizationAlgorithmMethod::LocalBrightness;
    layerDesc.m_NormSize = 3;
    layerDesc.m_Alpha = 0.5f;
    layerDesc.m_Beta = -1.0f;
    layerDesc.m_K = 0.2f;
    layerDesc.m_DataLayout = dataLayout;

    NormalizationLayer* layer = graph.AddLayer<NormalizationLayer>(layerDesc, "layer");

    // Creates extra layers.
    Layer* const input = graph.AddLayer<InputLayer>(0, "input");
    Layer* const output = graph.AddLayer<OutputLayer>(0, "output");

    TensorShape inputShape = (dataLayout == DataLayout::NCHW) ?
                TensorShape{ 3, 5, 5, 1 } : TensorShape{ 3, 1, 5, 5 };
    TensorShape outputShape = (dataLayout == DataLayout::NCHW) ?
                TensorShape{ 3, 5, 5, 1 } : TensorShape{ 3, 1, 5, 5 };

    // Connects up.
    armnn::TensorInfo inputTensorInfo(inputShape, DataType);
    armnn::TensorInfo outputTensorInfo(outputShape, DataType);
    Connect(input, layer, inputTensorInfo);
    Connect(layer, output, outputTensorInfo);
    CreateTensorHandles(graph, factory);

    // Makes the workload and checks it.
    auto workload = MakeAndCheckWorkload<NormalizationWorkload>(*layer, graph, factory);

    NormalizationQueueDescriptor queueDescriptor = workload->GetData();
    BOOST_TEST((queueDescriptor.m_Parameters.m_NormChannelType == NormalizationAlgorithmChannel::Across));
    BOOST_TEST((queueDescriptor.m_Parameters.m_NormMethodType == NormalizationAlgorithmMethod::LocalBrightness));
    BOOST_TEST(queueDescriptor.m_Parameters.m_NormSize == 3);
    BOOST_TEST(queueDescriptor.m_Parameters.m_Alpha == 0.5f);
    BOOST_TEST(queueDescriptor.m_Parameters.m_Beta == -1.0f);
    BOOST_TEST(queueDescriptor.m_Parameters.m_K == 0.2f);
    BOOST_TEST((queueDescriptor.m_Parameters.m_DataLayout == dataLayout));

    BOOST_TEST(queueDescriptor.m_Inputs.size() == 1);
    BOOST_TEST(queueDescriptor.m_Outputs.size() == 1);

    // Returns so we can do extra, backend-specific tests.
    return workload;
}

template <typename Pooling2dWorkload, armnn::DataType DataType>
std::unique_ptr<Pooling2dWorkload> CreatePooling2dWorkloadTest(armnn::IWorkloadFactory& factory,
                                                               armnn::Graph&            graph,
                                                               DataLayout dataLayout = DataLayout::NCHW)
{
    // Creates the layer we're testing.
    Pooling2dDescriptor layerDesc;
    layerDesc.m_PoolType = PoolingAlgorithm::Average;
    layerDesc.m_PoolWidth = 3;
    layerDesc.m_PoolHeight = 3;
    layerDesc.m_PadLeft = 2;
    layerDesc.m_PadRight = 2;
    layerDesc.m_PadTop = 1;
    layerDesc.m_PadBottom = 1;
    layerDesc.m_StrideX = 2;
    layerDesc.m_StrideY = 3;
    layerDesc.m_OutputShapeRounding = OutputShapeRounding::Floor;
    layerDesc.m_DataLayout = dataLayout;

    Pooling2dLayer* const layer = graph.AddLayer<Pooling2dLayer>(layerDesc, "layer");

    // Create extra layers
    Layer* const input = graph.AddLayer<InputLayer>(0, "input");
    Layer* const output = graph.AddLayer<OutputLayer>(0, "output");

    TensorShape inputShape  = (dataLayout == DataLayout::NCHW) ? TensorShape{3, 2, 5, 5} : TensorShape{3, 5, 5, 2};
    TensorShape outputShape = (dataLayout == DataLayout::NCHW) ? TensorShape{3, 2, 2, 4} : TensorShape{3, 2, 4, 2};

    // Connect up
    Connect(input, layer, TensorInfo(inputShape, DataType));
    Connect(layer, output, TensorInfo(outputShape, DataType));
    CreateTensorHandles(graph, factory);

    // Make the workload and checks it
    auto workload = MakeAndCheckWorkload<Pooling2dWorkload>(*layer, graph, factory);

    Pooling2dQueueDescriptor queueDescriptor = workload->GetData();
    BOOST_TEST((queueDescriptor.m_Parameters.m_PoolType == PoolingAlgorithm::Average));
    BOOST_TEST((queueDescriptor.m_Parameters.m_OutputShapeRounding == OutputShapeRounding::Floor));
    BOOST_TEST(queueDescriptor.m_Parameters.m_PoolWidth == 3);
    BOOST_TEST(queueDescriptor.m_Parameters.m_PoolHeight == 3);
    BOOST_TEST(queueDescriptor.m_Parameters.m_StrideX == 2);
    BOOST_TEST(queueDescriptor.m_Parameters.m_StrideY == 3);
    BOOST_TEST(queueDescriptor.m_Parameters.m_PadLeft == 2);
    BOOST_TEST(queueDescriptor.m_Parameters.m_PadRight == 2);
    BOOST_TEST(queueDescriptor.m_Parameters.m_PadTop == 1);
    BOOST_TEST(queueDescriptor.m_Parameters.m_PadBottom == 1);
    BOOST_TEST((queueDescriptor.m_Parameters.m_DataLayout == dataLayout));

    BOOST_TEST(queueDescriptor.m_Inputs.size() == 1);
    BOOST_TEST(queueDescriptor.m_Outputs.size() == 1);

    // Return so we can do extra, backend-specific tests
    return workload;
}

template <typename SoftmaxWorkload, armnn::DataType DataType>
std::unique_ptr<SoftmaxWorkload> CreateSoftmaxWorkloadTest(armnn::IWorkloadFactory& factory,
                                                           armnn::Graph&            graph)
{
    // Create the layer we're testing.
    SoftmaxDescriptor softmaxDescriptor;
    Layer* const layer = graph.AddLayer<SoftmaxLayer>(softmaxDescriptor, "layer");

    // Create extra layers.
    Layer* const input = graph.AddLayer<InputLayer>(0, "input");
    Layer* const output = graph.AddLayer<OutputLayer>(0, "output");

    // Connect up
    armnn::TensorInfo tensorInfo({4, 1}, DataType);
    Connect(input, layer, tensorInfo);
    Connect(layer, output, tensorInfo);
    CreateTensorHandles(graph, factory);

    // Make the workload and checks it.
    auto workload = MakeAndCheckWorkload<SoftmaxWorkload>(*layer, graph, factory);

    SoftmaxQueueDescriptor queueDescriptor = workload->GetData();
    BOOST_TEST(queueDescriptor.m_Inputs.size() == 1);
    BOOST_TEST(queueDescriptor.m_Outputs.size() == 1);

    // Return so we can do extra, backend-specific tests.
    return workload;
}

template<typename SplitterWorkload, armnn::DataType DataType>
std::unique_ptr<SplitterWorkload>
    CreateSplitterWorkloadTest(armnn::IWorkloadFactory& factory, armnn::Graph& graph)
{
    // Create the layer we're testing.
    // NOTE: need three dimensions channels, height/y, width/x because the Compute
    //       library restricts subtensors to have the same x and y dimensions as
    //       their parent tensors, and therefore the origin on the x and y dimension
    //       has to be zero for any view. So we need a third dimension to split...
    // NOTE: arguments are: number of views, number of dimensions.
    ViewsDescriptor layerDesc(3, 3);
    // NOTE: arguments are: view, dimension, value.
    layerDesc.SetViewOriginCoord(0, 0, 0);
    layerDesc.SetViewOriginCoord(1, 0, 1);
    layerDesc.SetViewOriginCoord(2, 0, 3);

    Layer* const layer = graph.AddLayer<SplitterLayer>(layerDesc, "layer");

    // Adds extra layers.
    Layer* const input = graph.AddLayer<InputLayer>(0, "input");
    Layer* const output0 = graph.AddLayer<OutputLayer>(0, "output0");
    Layer* const output1 = graph.AddLayer<OutputLayer>(1, "output1");
    Layer* const output2 = graph.AddLayer<OutputLayer>(2, "output2");

    // Connects up.
    armnn::TensorInfo tensorInfo({5, 7, 7}, DataType);
    Connect(input, layer, tensorInfo);

    armnn::TensorInfo output0Info({1, 7, 7}, DataType);
    armnn::TensorInfo output1Info({2, 7, 7}, DataType);
    armnn::TensorInfo output2Info({2, 7, 7}, DataType);

    Connect(layer, output0, output0Info, 0, 0);
    Connect(layer, output1, output1Info, 1, 0);
    Connect(layer, output2, output2Info, 2, 0);

    CreateTensorHandles(graph, factory);

    // Makes the workload and checks it.
    auto workload = MakeAndCheckWorkload<SplitterWorkload>(*layer, graph, factory);

    SplitterQueueDescriptor queueDescriptor = workload->GetData();
    BOOST_TEST(queueDescriptor.m_Inputs.size() == 1);
    BOOST_TEST(queueDescriptor.m_Outputs.size() == 3);
    BOOST_TEST(queueDescriptor.m_ViewOrigins.size() == 3);

    BOOST_TEST(queueDescriptor.m_ViewOrigins[0].m_Origin[0] == 0);
    BOOST_TEST(queueDescriptor.m_ViewOrigins[1].m_Origin[0] == 1);
    BOOST_TEST(queueDescriptor.m_ViewOrigins[2].m_Origin[0] == 3);
    BOOST_TEST(queueDescriptor.m_ViewOrigins[0].m_Origin[1] == 0);
    BOOST_TEST(queueDescriptor.m_ViewOrigins[1].m_Origin[1] == 0);
    BOOST_TEST(queueDescriptor.m_ViewOrigins[2].m_Origin[1] == 0);
    BOOST_TEST(queueDescriptor.m_ViewOrigins[0].m_Origin[2] == 0);
    BOOST_TEST(queueDescriptor.m_ViewOrigins[1].m_Origin[2] == 0);
    BOOST_TEST(queueDescriptor.m_ViewOrigins[2].m_Origin[2] == 0);

    // Returns so we can do extra, backend-specific tests.
    return workload;
}

/// This function constructs a graph with both a splitter and a merger, and returns a pair of the workloads.
template<typename SplitterWorkload, typename MergerWorkload, armnn::DataType DataType>
std::pair<std::unique_ptr<SplitterWorkload>, std::unique_ptr<MergerWorkload>>
    CreateSplitterMergerWorkloadTest(armnn::IWorkloadFactory& factory, armnn::Graph& graph)
{
    armnn::TensorInfo inputTensorInfo({ 1, 2, 100, 10 }, DataType);

    armnn::TensorInfo splitTensorInfo1({ 1, 1, 100, 10 }, DataType);
    armnn::TensorInfo splitTensorInfo2({ 1, 1, 100, 10 }, DataType);

    //Constructs the graph.
    Layer* const input = graph.AddLayer<InputLayer>(0, "input");

    armnn::ViewsDescriptor splitterViews(2);
    splitterViews.SetViewOriginCoord(0, 0, 0);
    splitterViews.SetViewOriginCoord(0, 1, 0);
    splitterViews.SetViewOriginCoord(0, 2, 0);
    splitterViews.SetViewOriginCoord(0, 3, 0);

    splitterViews.SetViewOriginCoord(1, 0, 0);
    splitterViews.SetViewOriginCoord(1, 1, 1);
    splitterViews.SetViewOriginCoord(1, 2, 0);
    splitterViews.SetViewOriginCoord(1, 3, 0);

    Layer* const splitter = graph.AddLayer<SplitterLayer>(splitterViews, "splitter");
    BOOST_TEST_CHECKPOINT("created splitter layer");

    armnn::OriginsDescriptor mergerViews(2);
    mergerViews.SetViewOriginCoord(0, 0, 0);
    mergerViews.SetViewOriginCoord(0, 1, 1);
    mergerViews.SetViewOriginCoord(0, 2, 0);
    mergerViews.SetViewOriginCoord(0, 3, 0);

    mergerViews.SetViewOriginCoord(1, 0, 0);
    mergerViews.SetViewOriginCoord(1, 1, 0);
    mergerViews.SetViewOriginCoord(1, 2, 0);
    mergerViews.SetViewOriginCoord(1, 3, 0);

    Layer* const merger = graph.AddLayer<MergerLayer>(mergerViews, "merger");
    BOOST_TEST_CHECKPOINT("created merger layer");

    Layer* const output = graph.AddLayer<OutputLayer>(0, "output");

    // Adds connections.
    Connect(input, splitter, inputTensorInfo, 0, 0);
    BOOST_TEST_CHECKPOINT("connect input to splitter");
    Connect(splitter, merger, splitTensorInfo1, 0, 1); // The splitter & merger are connected up.
    BOOST_TEST_CHECKPOINT("connect splitter[0] to merger[1]");
    Connect(splitter, merger, splitTensorInfo2, 1, 0); // So that the outputs are flipped round.
    BOOST_TEST_CHECKPOINT("connect splitter[1] to merger[0]");
    Connect(merger, output, inputTensorInfo, 0, 0);
    BOOST_TEST_CHECKPOINT("connect merger to output");

    CreateTensorHandles(graph, factory);
    BOOST_TEST_CHECKPOINT("created tensor handles");

    auto workloadSplitter = MakeAndCheckWorkload<SplitterWorkload>(*splitter, graph, factory);
    BOOST_TEST_CHECKPOINT("created splitter workload");
    auto workloadMerger = MakeAndCheckWorkload<MergerWorkload>(*merger, graph, factory);
    BOOST_TEST_CHECKPOINT("created merger workload");

    return {std::move(workloadSplitter), std::move(workloadMerger)};
}


/// This function constructs a graph with a splitter with two outputs. Each of the outputs is then
/// connected to two different activation layers
template<typename SplitterWorkload, typename ActivationWorkload, armnn::DataType DataType>
void CreateSplitterMultipleInputsOneOutputWorkloadTest(armnn::IWorkloadFactory& factory, armnn::Graph& graph,
                                 std::unique_ptr<SplitterWorkload>& wlSplitter,
                                 std::unique_ptr<ActivationWorkload>& wlActiv0_0,
                                 std::unique_ptr<ActivationWorkload>& wlActiv0_1,
                                 std::unique_ptr<ActivationWorkload>& wlActiv1_0,
                                 std::unique_ptr<ActivationWorkload>& wlActiv1_1)
{
    armnn::TensorInfo inputTensorInfo ({ 1, 3, 100, 50 }, DataType);
    armnn::TensorInfo splitTensorInfo1({ 1, 1, 100, 50 }, DataType);
    armnn::TensorInfo splitTensorInfo2({ 1, 2, 100, 50 }, DataType);

    //Constructs the graph.
    Layer* const input = graph.AddLayer<InputLayer>(0, "input");

    armnn::ViewsDescriptor splitterViews(2);

    splitterViews.SetViewOriginCoord(0, 0, 0);
    splitterViews.SetViewOriginCoord(0, 1, 0);
    splitterViews.SetViewOriginCoord(0, 2, 0);
    splitterViews.SetViewOriginCoord(0, 3, 0);

    splitterViews.SetViewOriginCoord(1, 0, 0);
    splitterViews.SetViewOriginCoord(1, 1, 1);
    splitterViews.SetViewOriginCoord(1, 2, 0);
    splitterViews.SetViewOriginCoord(1, 3, 0);

    Layer* const splitter = graph.AddLayer<SplitterLayer>(splitterViews, "splitter");

    armnn::ActivationDescriptor activationDesc;

    Layer* const activ0_0 = graph.AddLayer<ActivationLayer>(activationDesc, "activ0_0");
    Layer* const activ0_1 = graph.AddLayer<ActivationLayer>(activationDesc, "activ0_1");
    Layer* const activ1_0 = graph.AddLayer<ActivationLayer>(activationDesc, "activ1_0");
    Layer* const activ1_1 = graph.AddLayer<ActivationLayer>(activationDesc, "activ1_1");

    Layer* const output1 = graph.AddLayer<OutputLayer>(1, "output1");
    Layer* const output2 = graph.AddLayer<OutputLayer>(2, "output2");
    Layer* const output3 = graph.AddLayer<OutputLayer>(3, "output3");
    Layer* const output4 = graph.AddLayer<OutputLayer>(4, "output4");

    // Adds connections.
    Connect(input, splitter, inputTensorInfo, 0, 0);
    Connect(splitter, activ0_0, splitTensorInfo1, 0, 0);
    Connect(splitter, activ0_1, splitTensorInfo1, 0, 0);

    Connect(splitter, activ1_0, splitTensorInfo2, 1, 0);
    Connect(splitter, activ1_1, splitTensorInfo2, 1, 0);

    Connect(activ0_0, output1, splitTensorInfo1, 0, 0);
    Connect(activ0_1, output2, splitTensorInfo1, 0, 0);
    Connect(activ1_0, output3, splitTensorInfo2, 0, 0);
    Connect(activ1_1, output4, splitTensorInfo2, 0, 0);

    CreateTensorHandles(graph, factory);

    auto workloadSplitter = MakeAndCheckWorkload<SplitterWorkload>(*splitter, graph, factory);
    auto workloadActiv0_0 = MakeAndCheckWorkload<ActivationWorkload>(*activ0_0, graph, factory);
    auto workloadActiv0_1 = MakeAndCheckWorkload<ActivationWorkload>(*activ0_1, graph, factory);
    auto workloadActiv1_0 = MakeAndCheckWorkload<ActivationWorkload>(*activ1_0, graph, factory);
    auto workloadActiv1_1 = MakeAndCheckWorkload<ActivationWorkload>(*activ1_1, graph, factory);

    wlSplitter = std::move(workloadSplitter);
    wlActiv0_0 = std::move(workloadActiv0_0);
    wlActiv0_1 = std::move(workloadActiv0_1);
    wlActiv1_0 = std::move(workloadActiv1_0);
    wlActiv1_1 = std::move(workloadActiv1_1);
}

template <typename ResizeBilinearWorkload, armnn::DataType DataType>
std::unique_ptr<ResizeBilinearWorkload> CreateResizeBilinearWorkloadTest(armnn::IWorkloadFactory& factory,
                                                                         armnn::Graph& graph,
                                                                         DataLayout dataLayout = DataLayout::NCHW)
{
    TensorShape inputShape;
    TensorShape outputShape;

    switch (dataLayout) {
        case DataLayout::NHWC:
            inputShape =  { 2, 4, 4, 3 };
            outputShape = { 2, 2, 2, 3 };
            break;
        case DataLayout::NCHW:
        default:
            inputShape =  { 2, 3, 4, 4 };
            outputShape = { 2, 3, 2, 2 };
    }

    // Creates the layer we're testing.
    ResizeBilinearDescriptor resizeDesc;
    armnnUtils::DataLayoutIndexed dimensionIndices = dataLayout;
    resizeDesc.m_TargetWidth = outputShape[dimensionIndices.GetWidthIndex()];
    resizeDesc.m_TargetHeight = outputShape[dimensionIndices.GetHeightIndex()];
    resizeDesc.m_DataLayout = dataLayout;
    Layer* const layer = graph.AddLayer<ResizeBilinearLayer>(resizeDesc, "layer");

    // Creates extra layers.
    Layer* const input = graph.AddLayer<InputLayer>(0, "input");
    Layer* const output = graph.AddLayer<OutputLayer>(0, "output");

    // Connects up.
    armnn::TensorInfo inputTensorInfo(inputShape, DataType);
    armnn::TensorInfo outputTensorInfo(outputShape, DataType);
    Connect(input, layer, inputTensorInfo);
    Connect(layer, output, outputTensorInfo);
    CreateTensorHandles(graph, factory);

    // Makes the workload and checks it.
    auto workload = MakeAndCheckWorkload<ResizeBilinearWorkload>(*layer, graph, factory);

    ResizeBilinearQueueDescriptor queueDescriptor = workload->GetData();
    BOOST_TEST(queueDescriptor.m_Inputs.size() == 1);
    BOOST_TEST(queueDescriptor.m_Outputs.size() == 1);
    BOOST_TEST((queueDescriptor.m_Parameters.m_DataLayout == dataLayout));

    // Returns so we can do extra, backend-specific tests.
    return workload;
}

template <typename L2NormalizationWorkload, armnn::DataType DataType>
std::unique_ptr<L2NormalizationWorkload> CreateL2NormalizationWorkloadTest(armnn::IWorkloadFactory& factory,
    armnn::Graph& graph, DataLayout dataLayout = DataLayout::NCHW)
{
    // Creates the layer we're testing.
    L2NormalizationDescriptor layerDesc;
    layerDesc.m_DataLayout = dataLayout;

    Layer* const layer = graph.AddLayer<L2NormalizationLayer>(layerDesc, "l2norm");

    // Creates extra layers.
    Layer* const input = graph.AddLayer<InputLayer>(0, "input");
    Layer* const output = graph.AddLayer<OutputLayer>(0, "output");

    TensorShape inputShape = (dataLayout == DataLayout::NCHW) ?
                TensorShape{ 5, 20, 50, 67 } : TensorShape{ 5, 50, 67, 20 };
    TensorShape outputShape = (dataLayout == DataLayout::NCHW) ?
                TensorShape{ 5, 20, 50, 67 } : TensorShape{ 5, 50, 67, 20 };

    // Connects up.
    armnn::TensorInfo inputTensorInfo(inputShape, DataType);
    armnn::TensorInfo outputTensorInfo(outputShape, DataType);
    Connect(input, layer, inputTensorInfo);
    Connect(layer, output, outputTensorInfo);
    CreateTensorHandles(graph, factory);

    // Makes the workload and checks it.
    auto workload = MakeAndCheckWorkload<L2NormalizationWorkload>(*layer, graph, factory);

    L2NormalizationQueueDescriptor queueDescriptor = workload->GetData();
    BOOST_TEST((queueDescriptor.m_Parameters.m_DataLayout == dataLayout));
    BOOST_TEST(queueDescriptor.m_Inputs.size() == 1);
    BOOST_TEST(queueDescriptor.m_Outputs.size() == 1);

    // Returns so we can do extra, backend-specific tests.
    return workload;
}

template <typename ReshapeWorkload, armnn::DataType DataType>
std::unique_ptr<ReshapeWorkload> CreateReshapeWorkloadTest(armnn::IWorkloadFactory& factory,
    armnn::Graph& graph)
{
    // Creates the layer we're testing.
    TensorShape outputShape({ 1, 4 });
    ReshapeDescriptor reshapeDesc;
    reshapeDesc.m_TargetShape = outputShape;
    Layer* const layer = graph.AddLayer<ReshapeLayer>(reshapeDesc, "layer");

    // Creates extra layers.
    Layer* const input = graph.AddLayer<InputLayer>(0, "input");
    Layer* const output = graph.AddLayer<OutputLayer>(0, "output");

    // Connects up.
    armnn::TensorInfo inputTensorInfo({ 4, 1 }, DataType);
    armnn::TensorInfo outputTensorInfo(outputShape, DataType);
    Connect(input, layer, inputTensorInfo);
    Connect(layer, output, outputTensorInfo);
    CreateTensorHandles(graph, factory);

    // Makes the workload and checks it.
    auto workload = MakeAndCheckWorkload<ReshapeWorkload>(*layer, graph, factory);

    ReshapeQueueDescriptor queueDescriptor = workload->GetData();
    BOOST_TEST(queueDescriptor.m_Inputs.size() == 1);
    BOOST_TEST(queueDescriptor.m_Outputs.size() == 1);

    // Returns so we can do extra, backend-specific tests.
    return workload;
}

template <typename ConvertFp16ToFp32Float32Workload>
std::unique_ptr<ConvertFp16ToFp32Float32Workload> CreateConvertFp16ToFp32WorkloadTest(
    armnn::IWorkloadFactory& factory, armnn::Graph& graph)
{
    // Creates the layer we're testing.
    ConvertFp16ToFp32Layer* const layer = graph.AddLayer<ConvertFp16ToFp32Layer>("Fp16ToFp32Converter");

    // Creates extra layers.
    Layer* const input = graph.AddLayer<InputLayer>(0, "input");
    Layer* const output = graph.AddLayer<OutputLayer>(0, "output");

    // Connects up.
    armnn::TensorInfo inputTensorInfo({1, 3, 2, 3}, armnn::DataType::Float16);
    armnn::TensorInfo outputTensorInfo({1, 3, 2, 3}, armnn::DataType::Float32);
    Connect(input, layer, inputTensorInfo);
    Connect(layer, output, outputTensorInfo);
    CreateTensorHandles(graph, factory);

    // Makes the workload and checks it.
    auto workload = MakeAndCheckWorkload<ConvertFp16ToFp32Float32Workload>(*layer, graph, factory);

    ConvertFp16ToFp32QueueDescriptor queueDescriptor = workload->GetData();
    BOOST_TEST(queueDescriptor.m_Inputs.size() == 1);
    BOOST_TEST(queueDescriptor.m_Outputs.size() == 1);

    // Returns so we can do extra, backend-specific tests.
    return workload;
}

template <typename ConvertFp32ToFp16Float16Workload>
std::unique_ptr<ConvertFp32ToFp16Float16Workload> CreateConvertFp32ToFp16WorkloadTest(
    armnn::IWorkloadFactory& factory, armnn::Graph& graph)
{
    // Creates the layer we're testing.
    ConvertFp32ToFp16Layer* const layer = graph.AddLayer<ConvertFp32ToFp16Layer>("Fp32ToFp16Converter");

    // Creates extra layers.
    Layer* const input = graph.AddLayer<InputLayer>(0, "input");
    Layer* const output = graph.AddLayer<OutputLayer>(0, "output");

    // Connects up.
    armnn::TensorInfo inputTensorInfo({1, 3, 2, 3}, armnn::DataType::Float32);
    armnn::TensorInfo outputTensorInfo({1, 3, 2, 3}, armnn::DataType::Float16);
    Connect(input, layer, inputTensorInfo);
    Connect(layer, output, outputTensorInfo);
    CreateTensorHandles(graph, factory);

    // Makes the workload and checks it.
    auto workload = MakeAndCheckWorkload<ConvertFp32ToFp16Float16Workload>(*layer, graph, factory);

    ConvertFp32ToFp16QueueDescriptor queueDescriptor = workload->GetData();
    BOOST_TEST(queueDescriptor.m_Inputs.size() == 1);
    BOOST_TEST(queueDescriptor.m_Outputs.size() == 1);

    // Returns so we can do extra, backend-specific tests.
    return workload;
}

template <typename MeanWorkload, armnn::DataType DataType>
std::unique_ptr<MeanWorkload> CreateMeanWorkloadTest(armnn::IWorkloadFactory& factory, armnn::Graph& graph)
{
    // Reduce along the first and second dimensions, and do not keep the reduced dimensions.
    MeanDescriptor descriptor({ 1, 2 }, false);

    // Creates the layer we're testing.
    Layer* const layer = graph.AddLayer<MeanLayer>(descriptor, "mean");

    // Creates extra layers.
    Layer* const input = graph.AddLayer<InputLayer>(0, "input");
    Layer* const output = graph.AddLayer<OutputLayer>(0, "output");

    // Connects up.
    armnn::TensorInfo inputTensorInfo({ 1, 3, 7, 4 }, DataType);
    armnn::TensorInfo outputTensorInfo({ 1, 4 }, DataType);
    Connect(input, layer, inputTensorInfo);
    Connect(layer, output, outputTensorInfo);
    CreateTensorHandles(graph, factory);

    // Makes the workload and checks it.
    auto workload = MakeAndCheckWorkload<MeanWorkload>(*layer, graph, factory);

    MeanQueueDescriptor queueDescriptor = workload->GetData();
    BOOST_TEST(queueDescriptor.m_Parameters.m_Axis == descriptor.m_Axis);
    BOOST_TEST(queueDescriptor.m_Parameters.m_KeepDims == descriptor.m_KeepDims);
    BOOST_TEST(queueDescriptor.m_Inputs.size() == 1);
    BOOST_TEST(queueDescriptor.m_Outputs.size() == 1);

    // Returns so we can do extra, backend-specific tests.
    return workload;
}

template<typename MergerWorkload, armnn::DataType DataType>
std::unique_ptr<MergerWorkload> CreateMergerWorkloadTest(armnn::IWorkloadFactory& factory,
                                                         armnn::Graph& graph,
                                                         const armnn::TensorShape& outputShape,
                                                         unsigned int concatAxis)
{
    armnn::TensorInfo inputTensorInfo({ 2, 3, 2, 5 }, DataType);
    armnn::TensorInfo outputTensorInfo(outputShape, DataType);

    // Constructs the graph.
    Layer* const input0 = graph.AddLayer<InputLayer>(0, "input0");
    Layer* const input1 = graph.AddLayer<InputLayer>(1, "input1");
    armnn::OriginsDescriptor descriptor;

    std::vector<armnn::TensorShape> inputShapes{{ 2, 3, 2, 5 }, { 2, 3, 2, 5 }};

    descriptor = CreateDescriptorForConcatenation(inputShapes.begin(),
                                                  inputShapes.end(),
                                                  concatAxis);

    Layer* const merger = graph.AddLayer<MergerLayer>(descriptor, "merger");
    BOOST_TEST_CHECKPOINT("created merger layer");

    Layer* const output = graph.AddLayer<OutputLayer>(0, "output");

    // Adds connections.
    Connect(input0, merger, inputTensorInfo, 0, 0);
    BOOST_TEST_CHECKPOINT("connect input0 to merger");
    Connect(input1, merger, inputTensorInfo, 0, 1);
    BOOST_TEST_CHECKPOINT("connect input1 to merger");
    Connect(merger, output, outputTensorInfo, 0, 0);
    BOOST_TEST_CHECKPOINT("connect merger to output");

    CreateTensorHandles(graph, factory);
    BOOST_TEST_CHECKPOINT("created tensor handles");

    auto workloadMerger = MakeAndCheckWorkload<MergerWorkload>(*merger, graph, factory);
    BOOST_TEST_CHECKPOINT("created merger workload");

    return std::move(workloadMerger);
}

template <typename PreCompiledWorkload, armnn::DataType dataType>
std::pair<armnn::IOptimizedNetworkPtr, std::unique_ptr<PreCompiledWorkload>> CreatePreCompiledWorkloadTest(
    armnn::IWorkloadFactory& factory,
    armnn::Graph& graph,
    bool biasEnabled = false)
{
    // To create a PreCompiled layer, create a network and Optimize it.
    armnn::Network net;

    // Add an input layer
    armnn::IConnectableLayer* const inputLayer = net.AddInputLayer(0, "input layer");
    BOOST_TEST(inputLayer);

    // ArmNN weights tensor shape is OIHW (out channels, in channels, height, width) for NCHW
    // ArmNN weights tensor shape is OHWI (out channels, height, width, in channels) for NHWC
    // this test is using NHWC, so the weights shape is OHWI
    TensorInfo weightsTensorInfo(TensorShape({16, 1, 1, 16}), dataType, 0.9f, 0);
    unsigned int weightsLength = weightsTensorInfo.GetNumElements();

    using WeightType = armnn::ResolveType<dataType>;
    std::vector<WeightType> convWeightsData(weightsLength);
    for (unsigned int i = 0; i < weightsLength; ++i)
    {
        convWeightsData[i] = static_cast<WeightType>(i);
    }

    armnn::ConstTensor weights(weightsTensorInfo, convWeightsData);

    // Add a layer that can be used in the PreCompiled layer
    armnn::Convolution2dDescriptor convDesc2d;
    convDesc2d.m_StrideX = 1;
    convDesc2d.m_StrideY = 1;
    convDesc2d.m_BiasEnabled = biasEnabled;
    convDesc2d.m_DataLayout = armnn::DataLayout::NHWC;

    armnn::IConnectableLayer* convLayer = nullptr;
    const std::string convLayerName("conv layer");

    if (biasEnabled)
    {
        constexpr armnn::DataType biasDataType = ( dataType == armnn::DataType::QuantisedAsymm8) ?
            armnn::DataType::Signed32 : armnn::DataType::Float32;

        TensorInfo biasTensorInfo(TensorShape({1, 1, 1, 16}), biasDataType, 0.9f * 0.9f, 0);
        unsigned int biasLength = biasTensorInfo.GetNumElements();

        using BiasType = armnn::ResolveType<biasDataType>;
        std::vector<BiasType> biasData(biasLength);
        std::fill(biasData.begin(), biasData.end(), static_cast<BiasType>(0));

        armnn::ConstTensor biases(biasTensorInfo, biasData);

        // Create convolution layer with biases
        convLayer = net.AddConvolution2dLayer(convDesc2d,
                                              weights,
                                              Optional<ConstTensor>(biases),
                                              convLayerName.c_str());
    }
    else
    {
        // Create convolution layer without biases
        convLayer = net.AddConvolution2dLayer(convDesc2d,
                                              weights,
                                              EmptyOptional(),
                                              convLayerName.c_str());
    }

    BOOST_TEST(convLayer);

    // Add an output layer
    armnn::IConnectableLayer* const outputLayer = net.AddOutputLayer(0, "output layer");
    BOOST_TEST(outputLayer);

    // set the tensors in the network (NHWC format)
    TensorInfo inputTensorInfo(TensorShape({ 1, 16, 16, 16 }), dataType);
    if (dataType == armnn::DataType::QuantisedAsymm8)
    {
        inputTensorInfo.SetQuantizationOffset(0);
        inputTensorInfo.SetQuantizationScale(0.9f);
    }

    TensorInfo outputTensorInfo(TensorShape({1, 16, 16, 16}), dataType);
    if (dataType == armnn::DataType::QuantisedAsymm8)
    {
        outputTensorInfo.SetQuantizationOffset(0);
        outputTensorInfo.SetQuantizationScale(0.9f);
    }

    // Connect the layers
    inputLayer->GetOutputSlot(0).Connect(convLayer->GetInputSlot(0));
    inputLayer->GetOutputSlot(0).SetTensorInfo(inputTensorInfo);

    convLayer->GetOutputSlot(0).Connect(outputLayer->GetInputSlot(0));
    convLayer->GetOutputSlot(0).SetTensorInfo(outputTensorInfo);

    // Optimize the network for the backend supported by the factory
    std::vector<armnn::BackendId> backends = {factory.GetBackendId()};
    armnn::IRuntime::CreationOptions options;
    armnn::IRuntimePtr runtime(armnn::IRuntime::Create(options));
    armnn::OptimizerOptions optimizerOptions;
    armnn::IOptimizedNetworkPtr optimizedNet = armnn::Optimize(net, backends, runtime->GetDeviceSpec(),
                                                               optimizerOptions);
    BOOST_CHECK(optimizedNet != nullptr);

    // Find the PreCompiled layer in the optimised graph
    armnn::Graph& optimisedGraph = static_cast<armnn::OptimizedNetwork*>(optimizedNet.get())->GetGraph();
    Layer* preCompiledLayer = nullptr;
    for (auto& layer : optimisedGraph)
    {
        if (layer->GetType() == LayerType::PreCompiled)
        {
            preCompiledLayer = layer;
        }
    }
    BOOST_CHECK(preCompiledLayer != nullptr);

    // Create the TensorHandles.
    CreateTensorHandles(optimisedGraph, factory);

    // Make the workload and check it.
    auto workload = MakeAndCheckWorkload<PreCompiledWorkload>(*preCompiledLayer, optimisedGraph, factory);

    PreCompiledQueueDescriptor queueDescriptor = workload->GetData();
    BOOST_TEST(queueDescriptor.m_Inputs.size()  == 1);
    BOOST_TEST(queueDescriptor.m_Outputs.size() == 1);

    // Returns the workload so we can do extra, backend-specific tests.
    // NOTE: We need to return the optimised network as well, otherwise it gets
    // out of scope and the tensor handles get destructed
    return std::make_pair(std::move(optimizedNet), std::move(workload));
}

template<typename ConstantWorkload, armnn::DataType DataType>
std::unique_ptr<ConstantWorkload> CreateConstantWorkloadTest(armnn::IWorkloadFactory& factory,
                                                             armnn::Graph& graph,
                                                             const armnn::TensorShape& outputShape)
{
    armnn::TensorInfo outputTensorInfo(outputShape, DataType);

    auto constant = graph.AddLayer<ConstantLayer>("constant");
    constant->m_LayerOutput = std::make_unique<ScopedCpuTensorHandle>(outputTensorInfo);
    BOOST_TEST_CHECKPOINT("created constant layer");

    Layer* const output = graph.AddLayer<OutputLayer>(0, "output");

    // Adds connections.
    Connect(constant, output, outputTensorInfo, 0, 0);
    BOOST_TEST_CHECKPOINT("connect constant to output");

    CreateTensorHandles(graph, factory);
    BOOST_TEST_CHECKPOINT("created tensor handles");

    auto workloadConstant = MakeAndCheckWorkload<ConstantWorkload>(*constant, graph, factory);
    BOOST_TEST_CHECKPOINT("created Constant workload");

    return std::move(workloadConstant);
}

}
