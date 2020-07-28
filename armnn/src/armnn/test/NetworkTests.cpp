//
// Copyright © 2017 Arm Ltd. All rights reserved.
// SPDX-License-Identifier: MIT
//

#include "GraphUtils.hpp"

#include <armnn/ArmNN.hpp>
#include <armnn/LayerVisitorBase.hpp>
#include <Network.hpp>

#include <boost/test/unit_test.hpp>

namespace
{

bool AreAllLayerInputSlotsConnected(const armnn::IConnectableLayer& layer)
{
    bool allConnected = true;
    for (unsigned int i = 0; i < layer.GetNumInputSlots(); ++i)
    {
        const bool inputConnected = layer.GetInputSlot(i).GetConnection() != nullptr;
        allConnected &= inputConnected;
    }
    return allConnected;
}

}

BOOST_AUTO_TEST_SUITE(Network)

BOOST_AUTO_TEST_CASE(LayerGuids)
{
    armnn::Network net;
    armnn::LayerGuid inputId = net.AddInputLayer(0)->GetGuid();
    armnn::LayerGuid addId = net.AddAdditionLayer()->GetGuid();
    armnn::LayerGuid outputId = net.AddOutputLayer(0)->GetGuid();

    BOOST_TEST(inputId != addId);
    BOOST_TEST(addId != outputId);
    BOOST_TEST(inputId != outputId);
}

BOOST_AUTO_TEST_CASE(NetworkBasic)
{
    armnn::Network net;
    BOOST_TEST(net.PrintGraph() == armnn::Status::Success);
}

BOOST_AUTO_TEST_CASE(LayerNamesAreOptionalForINetwork)
{
    armnn::Network net;
    armnn::INetwork& inet = net;
    inet.AddInputLayer(0);
    inet.AddAdditionLayer();
    inet.AddActivationLayer(armnn::ActivationDescriptor());
    inet.AddOutputLayer(0);
}

BOOST_AUTO_TEST_CASE(LayerNamesAreOptionalForNetwork)
{
    armnn::Network net;
    net.AddInputLayer(0);
    net.AddAdditionLayer();
    net.AddActivationLayer(armnn::ActivationDescriptor());
    net.AddOutputLayer(0);
}

BOOST_AUTO_TEST_CASE(NetworkModification)
{
    armnn::Network net;

    armnn::IConnectableLayer* const inputLayer = net.AddInputLayer(0, "input layer");
    BOOST_TEST(inputLayer);

    unsigned int dims[] = { 10,1,1,1 };
    std::vector<float> convWeightsData(10);
    armnn::ConstTensor weights(armnn::TensorInfo(4, dims, armnn::DataType::Float32), convWeightsData);

    armnn::Convolution2dDescriptor convDesc2d;
    armnn::IConnectableLayer* const convLayer = net.AddConvolution2dLayer(convDesc2d,
                                                                          weights,
                                                                          armnn::EmptyOptional(),
                                                                          "conv layer");
    BOOST_TEST(convLayer);

    inputLayer->GetOutputSlot(0).Connect(convLayer->GetInputSlot(0));

    armnn::FullyConnectedDescriptor fullyConnectedDesc;
    armnn::IConnectableLayer* const fullyConnectedLayer = net.AddFullyConnectedLayer(fullyConnectedDesc,
                                                                                     weights,
                                                                                     armnn::EmptyOptional(),
                                                                                     "fully connected");
    BOOST_TEST(fullyConnectedLayer);

    convLayer->GetOutputSlot(0).Connect(fullyConnectedLayer->GetInputSlot(0));

    armnn::Pooling2dDescriptor pooling2dDesc;
    armnn::IConnectableLayer* const poolingLayer = net.AddPooling2dLayer(pooling2dDesc, "pooling2d");
    BOOST_TEST(poolingLayer);

    fullyConnectedLayer->GetOutputSlot(0).Connect(poolingLayer->GetInputSlot(0));

    armnn::ActivationDescriptor activationDesc;
    armnn::IConnectableLayer* const activationLayer = net.AddActivationLayer(activationDesc, "activation");
    BOOST_TEST(activationLayer);

    poolingLayer->GetOutputSlot(0).Connect(activationLayer->GetInputSlot(0));

    armnn::NormalizationDescriptor normalizationDesc;
    armnn::IConnectableLayer* const normalizationLayer = net.AddNormalizationLayer(normalizationDesc, "normalization");
    BOOST_TEST(normalizationLayer);

    activationLayer->GetOutputSlot(0).Connect(normalizationLayer->GetInputSlot(0));

    armnn::SoftmaxDescriptor softmaxDesc;
    armnn::IConnectableLayer* const softmaxLayer = net.AddSoftmaxLayer(softmaxDesc, "softmax");
    BOOST_TEST(softmaxLayer);

    normalizationLayer->GetOutputSlot(0).Connect(softmaxLayer->GetInputSlot(0));

    armnn::BatchNormalizationDescriptor batchNormDesc;

    armnn::TensorInfo tensorInfo({ 1 }, armnn::DataType::Float32);
    std::vector<float> data(tensorInfo.GetNumBytes() / sizeof(float));
    armnn::ConstTensor invalidTensor(tensorInfo, data);

    armnn::IConnectableLayer* const batchNormalizationLayer = net.AddBatchNormalizationLayer(batchNormDesc,
        invalidTensor,
        invalidTensor,
        invalidTensor,
        invalidTensor,
        "batch norm");
    BOOST_TEST(batchNormalizationLayer);

    softmaxLayer->GetOutputSlot(0).Connect(batchNormalizationLayer->GetInputSlot(0));

    armnn::IConnectableLayer* const additionLayer = net.AddAdditionLayer("addition");
    BOOST_TEST(additionLayer);

    batchNormalizationLayer->GetOutputSlot(0).Connect(additionLayer->GetInputSlot(0));
    batchNormalizationLayer->GetOutputSlot(0).Connect(additionLayer->GetInputSlot(1));

    armnn::IConnectableLayer* const multiplicationLayer = net.AddMultiplicationLayer("multiplication");
    BOOST_TEST(multiplicationLayer);

    additionLayer->GetOutputSlot(0).Connect(multiplicationLayer->GetInputSlot(0));
    additionLayer->GetOutputSlot(0).Connect(multiplicationLayer->GetInputSlot(1));

    armnn::IConnectableLayer* const outputLayer = net.AddOutputLayer(0, "output layer");
    BOOST_TEST(outputLayer);

    multiplicationLayer->GetOutputSlot(0).Connect(outputLayer->GetInputSlot(0));

    //Tests that all layers are present in the graph.
    BOOST_TEST(net.GetGraph().GetNumLayers() == 11);

    //Tests that the vertices exist and have correct names.
    BOOST_TEST(GraphHasNamedLayer(net.GetGraph(), "input layer"));
    BOOST_TEST(GraphHasNamedLayer(net.GetGraph(), "conv layer"));
    BOOST_TEST(GraphHasNamedLayer(net.GetGraph(), "fully connected"));
    BOOST_TEST(GraphHasNamedLayer(net.GetGraph(), "pooling2d"));
    BOOST_TEST(GraphHasNamedLayer(net.GetGraph(), "activation"));
    BOOST_TEST(GraphHasNamedLayer(net.GetGraph(), "normalization"));
    BOOST_TEST(GraphHasNamedLayer(net.GetGraph(), "softmax"));
    BOOST_TEST(GraphHasNamedLayer(net.GetGraph(), "batch norm"));
    BOOST_TEST(GraphHasNamedLayer(net.GetGraph(), "addition"));
    BOOST_TEST(GraphHasNamedLayer(net.GetGraph(), "multiplication"));
    BOOST_TEST(GraphHasNamedLayer(net.GetGraph(), "output layer"));

    auto checkOneOutputToOneInputConnection = []
        (const armnn::IConnectableLayer* const srcLayer,
         const armnn::IConnectableLayer* const tgtLayer,
         int expectedSrcNumInputs = 1,
         int expectedDstNumOutputs = 1)
        {
            BOOST_TEST(srcLayer->GetNumInputSlots() == expectedSrcNumInputs);
            BOOST_TEST(srcLayer->GetNumOutputSlots() == 1);
            BOOST_TEST(tgtLayer->GetNumInputSlots() == 1);
            BOOST_TEST(tgtLayer->GetNumOutputSlots() == expectedDstNumOutputs);

            BOOST_TEST(srcLayer->GetOutputSlot(0).GetNumConnections() == 1);
            BOOST_TEST(srcLayer->GetOutputSlot(0).GetConnection(0) == &tgtLayer->GetInputSlot(0));
            BOOST_TEST(&srcLayer->GetOutputSlot(0) == tgtLayer->GetInputSlot(0).GetConnection());
        };
    auto checkOneOutputToTwoInputsConnections = []
        (const armnn::IConnectableLayer* const srcLayer,
         const armnn::IConnectableLayer* const tgtLayer,
         int expectedSrcNumInputs,
         int expectedDstNumOutputs = 1)
        {
            BOOST_TEST(srcLayer->GetNumInputSlots() == expectedSrcNumInputs);
            BOOST_TEST(srcLayer->GetNumOutputSlots() == 1);
            BOOST_TEST(tgtLayer->GetNumInputSlots() == 2);
            BOOST_TEST(tgtLayer->GetNumOutputSlots() == expectedDstNumOutputs);

            BOOST_TEST(srcLayer->GetOutputSlot(0).GetNumConnections() == 2);
            for (unsigned int i = 0; i < srcLayer->GetOutputSlot(0).GetNumConnections(); ++i)
            {
                BOOST_TEST(srcLayer->GetOutputSlot(0).GetConnection(i) == &tgtLayer->GetInputSlot(i));
                BOOST_TEST(&srcLayer->GetOutputSlot(0) == tgtLayer->GetInputSlot(i).GetConnection());
            }
        };

    BOOST_TEST(AreAllLayerInputSlotsConnected(*convLayer));
    BOOST_TEST(AreAllLayerInputSlotsConnected(*fullyConnectedLayer));
    BOOST_TEST(AreAllLayerInputSlotsConnected(*poolingLayer));
    BOOST_TEST(AreAllLayerInputSlotsConnected(*activationLayer));
    BOOST_TEST(AreAllLayerInputSlotsConnected(*normalizationLayer));
    BOOST_TEST(AreAllLayerInputSlotsConnected(*softmaxLayer));
    BOOST_TEST(AreAllLayerInputSlotsConnected(*batchNormalizationLayer));
    BOOST_TEST(AreAllLayerInputSlotsConnected(*additionLayer));
    BOOST_TEST(AreAllLayerInputSlotsConnected(*multiplicationLayer));
    BOOST_TEST(AreAllLayerInputSlotsConnected(*outputLayer));

    // Checks connectivity.
    checkOneOutputToOneInputConnection(inputLayer, convLayer, 0);
    checkOneOutputToOneInputConnection(convLayer, fullyConnectedLayer);
    checkOneOutputToOneInputConnection(fullyConnectedLayer, poolingLayer);
    checkOneOutputToOneInputConnection(poolingLayer, activationLayer);
    checkOneOutputToOneInputConnection(activationLayer, normalizationLayer);
    checkOneOutputToOneInputConnection(normalizationLayer, softmaxLayer);
    checkOneOutputToOneInputConnection(softmaxLayer, batchNormalizationLayer);
    checkOneOutputToTwoInputsConnections(batchNormalizationLayer, additionLayer, 1);
    checkOneOutputToTwoInputsConnections(additionLayer, multiplicationLayer, 2);
    checkOneOutputToOneInputConnection(multiplicationLayer, outputLayer, 2, 0);
}

BOOST_AUTO_TEST_CASE(NetworkModification_SplitterMerger)
{
    armnn::Network net;

    // Adds an input layer and an input tensor descriptor.
    armnn::IConnectableLayer* inputLayer = net.AddInputLayer(0, "input layer");
    BOOST_TEST(inputLayer);

    // Adds a splitter layer.
    armnn::ViewsDescriptor splitterDesc(2,4);

    armnn::IConnectableLayer* splitterLayer = net.AddSplitterLayer(splitterDesc, "splitter layer");
    BOOST_TEST(splitterLayer);

    inputLayer->GetOutputSlot(0).Connect(splitterLayer->GetInputSlot(0));

    // Adds a softmax layer 1.
    armnn::SoftmaxDescriptor softmaxDescriptor;
    armnn::IConnectableLayer* softmaxLayer1 = net.AddSoftmaxLayer(softmaxDescriptor, "softmax_1");
    BOOST_TEST(softmaxLayer1);

    splitterLayer->GetOutputSlot(0).Connect(softmaxLayer1->GetInputSlot(0));

    // Adds a softmax layer 2.
    armnn::IConnectableLayer* softmaxLayer2 = net.AddSoftmaxLayer(softmaxDescriptor, "softmax_2");
    BOOST_TEST(softmaxLayer2);

    splitterLayer->GetOutputSlot(1).Connect(softmaxLayer2->GetInputSlot(0));

    // Adds a merger layer.
    armnn::OriginsDescriptor mergerDesc(2, 4);

    ARMNN_NO_DEPRECATE_WARN_BEGIN
    armnn::IConnectableLayer* mergerLayer = net.AddMergerLayer(mergerDesc, "merger layer");
    ARMNN_NO_DEPRECATE_WARN_END
    BOOST_TEST(mergerLayer);

    softmaxLayer1->GetOutputSlot(0).Connect(mergerLayer->GetInputSlot(0));
    softmaxLayer2->GetOutputSlot(0).Connect(mergerLayer->GetInputSlot(1));

    // Adds an output layer.
    armnn::IConnectableLayer* outputLayer = net.AddOutputLayer(0, "output layer");
    BOOST_TEST(outputLayer);

    mergerLayer->GetOutputSlot(0).Connect(outputLayer->GetInputSlot(0));

    BOOST_TEST(splitterLayer->GetNumOutputSlots() == 2);
    BOOST_TEST(splitterLayer->GetOutputSlot(0).GetConnection(0) == &softmaxLayer1->GetInputSlot(0));
    BOOST_TEST(&splitterLayer->GetOutputSlot(0) == softmaxLayer1->GetInputSlot(0).GetConnection());
    BOOST_TEST(splitterLayer->GetOutputSlot(1).GetConnection(0) == &softmaxLayer2->GetInputSlot(0));
    BOOST_TEST(&splitterLayer->GetOutputSlot(1) == softmaxLayer2->GetInputSlot(0).GetConnection());

    BOOST_TEST(mergerLayer->GetNumInputSlots() == 2);
    BOOST_TEST(softmaxLayer1->GetOutputSlot(0).GetConnection(0) == &mergerLayer->GetInputSlot(0));
    BOOST_TEST(&softmaxLayer1->GetOutputSlot(0) == mergerLayer->GetInputSlot(0).GetConnection());
    BOOST_TEST(softmaxLayer2->GetOutputSlot(0).GetConnection(0) == &mergerLayer->GetInputSlot(1));
    BOOST_TEST(&softmaxLayer2->GetOutputSlot(0) == mergerLayer->GetInputSlot(1).GetConnection());
}

BOOST_AUTO_TEST_CASE(NetworkModification_SplitterAddition)
{
    armnn::Network net;

    // Adds an input layer and an input tensor descriptor.
    armnn::IConnectableLayer* layer = net.AddInputLayer(0, "input layer");
    BOOST_TEST(layer);

    // Adds a splitter layer.
    armnn::ViewsDescriptor splitterDesc(2,4);

    armnn::IConnectableLayer* const splitterLayer = net.AddSplitterLayer(splitterDesc, "splitter layer");
    BOOST_TEST(splitterLayer);

    layer->GetOutputSlot(0).Connect(splitterLayer->GetInputSlot(0));

    // Adds a softmax layer 1.
    armnn::SoftmaxDescriptor softmaxDescriptor;
    armnn::IConnectableLayer* const softmax1Layer = net.AddSoftmaxLayer(softmaxDescriptor, "softmax_1");
    BOOST_TEST(softmax1Layer);

    splitterLayer->GetOutputSlot(0).Connect(softmax1Layer->GetInputSlot(0));

    // Adds a softmax layer 2.
    armnn::IConnectableLayer* const softmax2Layer = net.AddSoftmaxLayer(softmaxDescriptor, "softmax_2");
    BOOST_TEST(softmax2Layer);

    splitterLayer->GetOutputSlot(1).Connect(softmax2Layer->GetInputSlot(0));

    // Adds addition layer.
    layer = net.AddAdditionLayer("add layer");
    BOOST_TEST(layer);

    softmax1Layer->GetOutputSlot(0).Connect(layer->GetInputSlot(0));
    softmax2Layer->GetOutputSlot(0).Connect(layer->GetInputSlot(1));

    // Adds an output layer.
    armnn::IConnectableLayer* prevLayer = layer;
    layer = net.AddOutputLayer(0, "output layer");

    prevLayer->GetOutputSlot(0).Connect(layer->GetInputSlot(0));

    BOOST_TEST(layer);
}

BOOST_AUTO_TEST_CASE(NetworkModification_SplitterMultiplication)
{
    armnn::Network net;

    // Adds an input layer and an input tensor descriptor.
    armnn::IConnectableLayer* layer = net.AddInputLayer(0, "input layer");
    BOOST_TEST(layer);

    // Adds a splitter layer.
    armnn::ViewsDescriptor splitterDesc(2,4);
    armnn::IConnectableLayer* const splitterLayer = net.AddSplitterLayer(splitterDesc, "splitter layer");
    BOOST_TEST(splitterLayer);

    layer->GetOutputSlot(0).Connect(splitterLayer->GetInputSlot(0));

    // Adds a softmax layer 1.
    armnn::SoftmaxDescriptor softmaxDescriptor;
    armnn::IConnectableLayer* const softmax1Layer = net.AddSoftmaxLayer(softmaxDescriptor, "softmax_1");
    BOOST_TEST(softmax1Layer);

    splitterLayer->GetOutputSlot(0).Connect(softmax1Layer->GetInputSlot(0));

    // Adds a softmax layer 2.
    armnn::IConnectableLayer* const softmax2Layer = net.AddSoftmaxLayer(softmaxDescriptor, "softmax_2");
    BOOST_TEST(softmax2Layer);

    splitterLayer->GetOutputSlot(1).Connect(softmax2Layer->GetInputSlot(0));

    // Adds multiplication layer.
    layer = net.AddMultiplicationLayer("multiplication layer");
    BOOST_TEST(layer);

    softmax1Layer->GetOutputSlot(0).Connect(layer->GetInputSlot(0));
    softmax2Layer->GetOutputSlot(0).Connect(layer->GetInputSlot(1));

    // Adds an output layer.
    armnn::IConnectableLayer* prevLayer = layer;
    layer = net.AddOutputLayer(0, "output layer");
    BOOST_TEST(layer);

    prevLayer->GetOutputSlot(0).Connect(layer->GetInputSlot(0));
}

BOOST_AUTO_TEST_CASE(Network_AddQuantize)
{
    struct Test : public armnn::LayerVisitorBase<armnn::VisitorNoThrowPolicy>
    {
        void VisitQuantizeLayer(const armnn::IConnectableLayer* layer, const char* name) override
        {
            m_Visited = true;

            BOOST_TEST(layer);

            std::string expectedName = std::string("quantize");
            BOOST_TEST(std::string(layer->GetName()) == expectedName);
            BOOST_TEST(std::string(name) == expectedName);

            BOOST_TEST(layer->GetNumInputSlots() == 1);
            BOOST_TEST(layer->GetNumOutputSlots() == 1);

            const armnn::TensorInfo& infoIn = layer->GetInputSlot(0).GetConnection()->GetTensorInfo();
            BOOST_TEST((infoIn.GetDataType() == armnn::DataType::Float32));

            const armnn::TensorInfo& infoOut = layer->GetOutputSlot(0).GetTensorInfo();
            BOOST_TEST((infoOut.GetDataType() == armnn::DataType::QuantisedAsymm8));
        }

        bool m_Visited = false;
    };


    auto graph = armnn::INetwork::Create();

    auto input = graph->AddInputLayer(0, "input");
    auto quantize = graph->AddQuantizeLayer("quantize");
    auto output = graph->AddOutputLayer(1, "output");

    input->GetOutputSlot(0).Connect(quantize->GetInputSlot(0));
    quantize->GetOutputSlot(0).Connect(output->GetInputSlot(0));

    armnn::TensorInfo infoIn({3,1}, armnn::DataType::Float32);
    input->GetOutputSlot(0).SetTensorInfo(infoIn);

    armnn::TensorInfo infoOut({3,1}, armnn::DataType::QuantisedAsymm8);
    quantize->GetOutputSlot(0).SetTensorInfo(infoOut);

    Test testQuantize;
    graph->Accept(testQuantize);

    BOOST_TEST(testQuantize.m_Visited == true);

}

BOOST_AUTO_TEST_CASE(Network_AddMerge)
{
    struct Test : public armnn::LayerVisitorBase<armnn::VisitorNoThrowPolicy>
    {
        void VisitMergeLayer(const armnn::IConnectableLayer* layer, const char* name) override
        {
            m_Visited = true;

            BOOST_TEST(layer);

            std::string expectedName = std::string("merge");
            BOOST_TEST(std::string(layer->GetName()) == expectedName);
            BOOST_TEST(std::string(name) == expectedName);

            BOOST_TEST(layer->GetNumInputSlots() == 2);
            BOOST_TEST(layer->GetNumOutputSlots() == 1);

            const armnn::TensorInfo& infoIn0 = layer->GetInputSlot(0).GetConnection()->GetTensorInfo();
            BOOST_TEST((infoIn0.GetDataType() == armnn::DataType::Float32));

            const armnn::TensorInfo& infoIn1 = layer->GetInputSlot(1).GetConnection()->GetTensorInfo();
            BOOST_TEST((infoIn1.GetDataType() == armnn::DataType::Float32));

            const armnn::TensorInfo& infoOut = layer->GetOutputSlot(0).GetTensorInfo();
            BOOST_TEST((infoOut.GetDataType() == armnn::DataType::Float32));
        }

        bool m_Visited = false;
    };

    armnn::INetworkPtr network = armnn::INetwork::Create();

    armnn::IConnectableLayer* input0 = network->AddInputLayer(0);
    armnn::IConnectableLayer* input1 = network->AddInputLayer(1);
    armnn::IConnectableLayer* merge = network->AddMergeLayer("merge");
    armnn::IConnectableLayer* output = network->AddOutputLayer(0);

    input0->GetOutputSlot(0).Connect(merge->GetInputSlot(0));
    input1->GetOutputSlot(0).Connect(merge->GetInputSlot(1));
    merge->GetOutputSlot(0).Connect(output->GetInputSlot(0));

    const armnn::TensorInfo info({3,1}, armnn::DataType::Float32);
    input0->GetOutputSlot(0).SetTensorInfo(info);
    input1->GetOutputSlot(0).SetTensorInfo(info);
    merge->GetOutputSlot(0).SetTensorInfo(info);

    Test testMerge;
    network->Accept(testMerge);

    BOOST_TEST(testMerge.m_Visited == true);
}

BOOST_AUTO_TEST_SUITE_END()
