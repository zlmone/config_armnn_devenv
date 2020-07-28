//
// Copyright © 2017 Arm Ltd. All rights reserved.
// SPDX-License-Identifier: MIT
//

#include "../Serializer.hpp"

#include <armnn/ArmNN.hpp>
#include <armnn/INetwork.hpp>
#include <armnnDeserializer/IDeserializer.hpp>

#include <random>
#include <vector>

#include <boost/test/unit_test.hpp>

using armnnDeserializer::IDeserializer;

namespace
{

struct DefaultLayerVerifierPolicy
{
    static void Apply(const std::string s = "")
    {
        BOOST_TEST_MESSAGE("Unexpected layer found in network");
        BOOST_TEST(false);
    }
};

class LayerVerifierBase : public armnn::LayerVisitorBase<DefaultLayerVerifierPolicy>
{
public:
    LayerVerifierBase(const std::string& layerName,
                      const std::vector<armnn::TensorInfo>& inputInfos,
                      const std::vector<armnn::TensorInfo>& outputInfos)
    : m_LayerName(layerName)
    , m_InputTensorInfos(inputInfos)
    , m_OutputTensorInfos(outputInfos) {}

    void VisitInputLayer(const armnn::IConnectableLayer*, armnn::LayerBindingId, const char*) override {}

    void VisitOutputLayer(const armnn::IConnectableLayer*, armnn::LayerBindingId id, const char*) override {}

protected:
    void VerifyNameAndConnections(const armnn::IConnectableLayer* layer, const char* name)
    {
        BOOST_TEST(name == m_LayerName.c_str());

        BOOST_TEST(layer->GetNumInputSlots() == m_InputTensorInfos.size());
        BOOST_TEST(layer->GetNumOutputSlots() == m_OutputTensorInfos.size());

        for (unsigned int i = 0; i < m_InputTensorInfos.size(); i++)
        {
            const armnn::IOutputSlot* connectedOutput = layer->GetInputSlot(i).GetConnection();
            BOOST_CHECK(connectedOutput);

            const armnn::TensorInfo& connectedInfo = connectedOutput->GetTensorInfo();
            BOOST_TEST(connectedInfo.GetShape() == m_InputTensorInfos[i].GetShape());
            BOOST_TEST(
                GetDataTypeName(connectedInfo.GetDataType()) == GetDataTypeName(m_InputTensorInfos[i].GetDataType()));

            BOOST_TEST(connectedInfo.GetQuantizationScale() == m_InputTensorInfos[i].GetQuantizationScale());
            BOOST_TEST(connectedInfo.GetQuantizationOffset() == m_InputTensorInfos[i].GetQuantizationOffset());
        }

        for (unsigned int i = 0; i < m_OutputTensorInfos.size(); i++)
        {
            const armnn::TensorInfo& outputInfo = layer->GetOutputSlot(i).GetTensorInfo();
            BOOST_TEST(outputInfo.GetShape() == m_OutputTensorInfos[i].GetShape());
            BOOST_TEST(
                GetDataTypeName(outputInfo.GetDataType()) == GetDataTypeName(m_OutputTensorInfos[i].GetDataType()));

            BOOST_TEST(outputInfo.GetQuantizationScale() == m_OutputTensorInfos[i].GetQuantizationScale());
            BOOST_TEST(outputInfo.GetQuantizationOffset() == m_OutputTensorInfos[i].GetQuantizationOffset());
        }
    }

private:
    std::string m_LayerName;
    std::vector<armnn::TensorInfo> m_InputTensorInfos;
    std::vector<armnn::TensorInfo> m_OutputTensorInfos;
};

template<typename T>
void CompareConstTensorData(const void* data1, const void* data2, unsigned int numElements)
{
    T typedData1 = static_cast<T>(data1);
    T typedData2 = static_cast<T>(data2);
    BOOST_CHECK(typedData1);
    BOOST_CHECK(typedData2);

    for (unsigned int i = 0; i < numElements; i++)
    {
        BOOST_TEST(typedData1[i] == typedData2[i]);
    }
}

void CompareConstTensor(const armnn::ConstTensor& tensor1, const armnn::ConstTensor& tensor2)
{
    BOOST_TEST(tensor1.GetShape() == tensor2.GetShape());
    BOOST_TEST(GetDataTypeName(tensor1.GetDataType()) == GetDataTypeName(tensor2.GetDataType()));

    switch (tensor1.GetDataType())
    {
        case armnn::DataType::Float32:
            CompareConstTensorData<const float*>(
                tensor1.GetMemoryArea(), tensor2.GetMemoryArea(), tensor1.GetNumElements());
            break;
        case armnn::DataType::QuantisedAsymm8:
        case armnn::DataType::Boolean:
            CompareConstTensorData<const uint8_t*>(
                tensor1.GetMemoryArea(), tensor2.GetMemoryArea(), tensor1.GetNumElements());
            break;
        case armnn::DataType::Signed32:
            CompareConstTensorData<const int32_t*>(
                tensor1.GetMemoryArea(), tensor2.GetMemoryArea(), tensor1.GetNumElements());
            break;
        default:
            // Note that Float16 is not yet implemented
            BOOST_TEST_MESSAGE("Unexpected datatype");
            BOOST_TEST(false);
    }
}

armnn::INetworkPtr DeserializeNetwork(const std::string& serializerString)
{
    std::vector<std::uint8_t> const serializerVector{serializerString.begin(), serializerString.end()};
    return IDeserializer::Create()->CreateNetworkFromBinary(serializerVector);
}

std::string SerializeNetwork(const armnn::INetwork& network)
{
    armnnSerializer::Serializer serializer;
    serializer.Serialize(network);

    std::stringstream stream;
    serializer.SaveSerializedToStream(stream);

    std::string serializerString{stream.str()};
    return serializerString;
}

template<typename DataType>
static std::vector<DataType> GenerateRandomData(size_t size)
{
    constexpr bool isIntegerType = std::is_integral<DataType>::value;
    using Distribution =
        typename std::conditional<isIntegerType,
                                  std::uniform_int_distribution<DataType>,
                                  std::uniform_real_distribution<DataType>>::type;

    static constexpr DataType lowerLimit = std::numeric_limits<DataType>::min();
    static constexpr DataType upperLimit = std::numeric_limits<DataType>::max();

    static Distribution distribution(lowerLimit, upperLimit);
    static std::default_random_engine generator;

    std::vector<DataType> randomData(size);
    std::generate(randomData.begin(), randomData.end(), []() { return distribution(generator); });

    return randomData;
}

} // anonymous namespace

BOOST_AUTO_TEST_SUITE(SerializerTests)

BOOST_AUTO_TEST_CASE(SerializeAddition)
{
    class AdditionLayerVerifier : public LayerVerifierBase
    {
    public:
        AdditionLayerVerifier(const std::string& layerName,
                              const std::vector<armnn::TensorInfo>& inputInfos,
                              const std::vector<armnn::TensorInfo>& outputInfos)
        : LayerVerifierBase(layerName, inputInfos, outputInfos) {}

        void VisitAdditionLayer(const armnn::IConnectableLayer* layer, const char* name) override
        {
            VerifyNameAndConnections(layer, name);
        }
    };

    const std::string layerName("addition");
    const armnn::TensorInfo tensorInfo({1, 2, 3}, armnn::DataType::Float32);

    armnn::INetworkPtr network = armnn::INetwork::Create();
    armnn::IConnectableLayer* const inputLayer0 = network->AddInputLayer(0);
    armnn::IConnectableLayer* const inputLayer1 = network->AddInputLayer(1);
    armnn::IConnectableLayer* const additionLayer = network->AddAdditionLayer(layerName.c_str());
    armnn::IConnectableLayer* const outputLayer = network->AddOutputLayer(0);

    inputLayer0->GetOutputSlot(0).Connect(additionLayer->GetInputSlot(0));
    inputLayer1->GetOutputSlot(0).Connect(additionLayer->GetInputSlot(1));
    additionLayer->GetOutputSlot(0).Connect(outputLayer->GetInputSlot(0));

    inputLayer0->GetOutputSlot(0).SetTensorInfo(tensorInfo);
    inputLayer1->GetOutputSlot(0).SetTensorInfo(tensorInfo);
    additionLayer->GetOutputSlot(0).SetTensorInfo(tensorInfo);

    armnn::INetworkPtr deserializedNetwork = DeserializeNetwork(SerializeNetwork(*network));
    BOOST_CHECK(deserializedNetwork);

    AdditionLayerVerifier verifier(layerName, {tensorInfo, tensorInfo}, {tensorInfo});
    deserializedNetwork->Accept(verifier);
}

BOOST_AUTO_TEST_CASE(SerializeBatchNormalization)
{
    class BatchNormalizationLayerVerifier : public LayerVerifierBase
    {
    public:
        BatchNormalizationLayerVerifier(const std::string& layerName,
                                        const std::vector<armnn::TensorInfo>& inputInfos,
                                        const std::vector<armnn::TensorInfo>& outputInfos,
                                        const armnn::BatchNormalizationDescriptor& descriptor,
                                        const armnn::ConstTensor& mean,
                                        const armnn::ConstTensor& variance,
                                        const armnn::ConstTensor& beta,
                                        const armnn::ConstTensor& gamma)
        : LayerVerifierBase(layerName, inputInfos, outputInfos)
        , m_Descriptor(descriptor)
        , m_Mean(mean)
        , m_Variance(variance)
        , m_Beta(beta)
        , m_Gamma(gamma) {}

        void VisitBatchNormalizationLayer(const armnn::IConnectableLayer* layer,
                                          const armnn::BatchNormalizationDescriptor& descriptor,
                                          const armnn::ConstTensor& mean,
                                          const armnn::ConstTensor& variance,
                                          const armnn::ConstTensor& beta,
                                          const armnn::ConstTensor& gamma,
                                          const char* name) override
        {
            VerifyNameAndConnections(layer, name);
            VerifyDescriptor(descriptor);

            CompareConstTensor(mean, m_Mean);
            CompareConstTensor(variance, m_Variance);
            CompareConstTensor(beta, m_Beta);
            CompareConstTensor(gamma, m_Gamma);
        }

    private:
        void VerifyDescriptor(const armnn::BatchNormalizationDescriptor& descriptor)
        {
            BOOST_TEST(descriptor.m_Eps == m_Descriptor.m_Eps);
            BOOST_TEST(static_cast<int>(descriptor.m_DataLayout) == static_cast<int>(m_Descriptor.m_DataLayout));
        }

        armnn::BatchNormalizationDescriptor m_Descriptor;
        armnn::ConstTensor m_Mean;
        armnn::ConstTensor m_Variance;
        armnn::ConstTensor m_Beta;
        armnn::ConstTensor m_Gamma;
    };

    const std::string layerName("batchNormalization");
    const armnn::TensorInfo inputInfo ({ 1, 3, 3, 1 }, armnn::DataType::Float32);
    const armnn::TensorInfo outputInfo({ 1, 3, 3, 1 }, armnn::DataType::Float32);

    const armnn::TensorInfo meanInfo({1}, armnn::DataType::Float32);
    const armnn::TensorInfo varianceInfo({1}, armnn::DataType::Float32);
    const armnn::TensorInfo betaInfo({1}, armnn::DataType::Float32);
    const armnn::TensorInfo gammaInfo({1}, armnn::DataType::Float32);

    armnn::BatchNormalizationDescriptor descriptor;
    descriptor.m_Eps = 0.0010000000475f;
    descriptor.m_DataLayout = armnn::DataLayout::NHWC;

    std::vector<float> meanData({5.0});
    std::vector<float> varianceData({2.0});
    std::vector<float> betaData({1.0});
    std::vector<float> gammaData({0.0});

    armnn::ConstTensor mean(meanInfo, meanData);
    armnn::ConstTensor variance(varianceInfo, varianceData);
    armnn::ConstTensor beta(betaInfo, betaData);
    armnn::ConstTensor gamma(gammaInfo, gammaData);

    armnn::INetworkPtr network = armnn::INetwork::Create();
    armnn::IConnectableLayer* const inputLayer = network->AddInputLayer(0);
    armnn::IConnectableLayer* const batchNormalizationLayer =
        network->AddBatchNormalizationLayer(descriptor, mean, variance, beta, gamma, layerName.c_str());
    armnn::IConnectableLayer* const outputLayer = network->AddOutputLayer(0);

    inputLayer->GetOutputSlot(0).Connect(batchNormalizationLayer->GetInputSlot(0));
    batchNormalizationLayer->GetOutputSlot(0).Connect(outputLayer->GetInputSlot(0));

    inputLayer->GetOutputSlot(0).SetTensorInfo(inputInfo);
    batchNormalizationLayer->GetOutputSlot(0).SetTensorInfo(outputInfo);

    armnn::INetworkPtr deserializedNetwork = DeserializeNetwork(SerializeNetwork(*network));
    BOOST_CHECK(deserializedNetwork);

    BatchNormalizationLayerVerifier verifier(
        layerName, {inputInfo}, {outputInfo}, descriptor, mean, variance, beta, gamma);
    deserializedNetwork->Accept(verifier);
}

BOOST_AUTO_TEST_CASE(SerializeBatchToSpaceNd)
{
    class BatchToSpaceNdLayerVerifier : public LayerVerifierBase
    {
    public:
        BatchToSpaceNdLayerVerifier(const std::string& layerName,
                                    const std::vector<armnn::TensorInfo>& inputInfos,
                                    const std::vector<armnn::TensorInfo>& outputInfos,
                                    const armnn::BatchToSpaceNdDescriptor& descriptor)
        : LayerVerifierBase(layerName, inputInfos, outputInfos)
        , m_Descriptor(descriptor) {}

        void VisitBatchToSpaceNdLayer(const armnn::IConnectableLayer* layer,
                                      const armnn::BatchToSpaceNdDescriptor& descriptor,
                                      const char* name) override
        {
            VerifyNameAndConnections(layer, name);
            VerifyDescriptor(descriptor);
        }

    private:
        void VerifyDescriptor(const armnn::BatchToSpaceNdDescriptor& descriptor)
        {
            BOOST_TEST(descriptor.m_Crops == m_Descriptor.m_Crops);
            BOOST_TEST(descriptor.m_BlockShape == m_Descriptor.m_BlockShape);
            BOOST_TEST(GetDataLayoutName(descriptor.m_DataLayout) == GetDataLayoutName(m_Descriptor.m_DataLayout));
        }

        armnn::BatchToSpaceNdDescriptor m_Descriptor;
    };

    const std::string layerName("spaceToBatchNd");
    const armnn::TensorInfo inputInfo({4, 1, 2, 2}, armnn::DataType::Float32);
    const armnn::TensorInfo outputInfo({1, 1, 4, 4}, armnn::DataType::Float32);

    armnn::BatchToSpaceNdDescriptor desc;
    desc.m_DataLayout = armnn::DataLayout::NCHW;
    desc.m_BlockShape = {2, 2};
    desc.m_Crops = {{0, 0}, {0, 0}};

    armnn::INetworkPtr network = armnn::INetwork::Create();
    armnn::IConnectableLayer* const inputLayer = network->AddInputLayer(0);
    armnn::IConnectableLayer* const batchToSpaceNdLayer = network->AddBatchToSpaceNdLayer(desc, layerName.c_str());
    armnn::IConnectableLayer* const outputLayer = network->AddOutputLayer(0);

    inputLayer->GetOutputSlot(0).Connect(batchToSpaceNdLayer->GetInputSlot(0));
    batchToSpaceNdLayer->GetOutputSlot(0).Connect(outputLayer->GetInputSlot(0));

    inputLayer->GetOutputSlot(0).SetTensorInfo(inputInfo);
    batchToSpaceNdLayer->GetOutputSlot(0).SetTensorInfo(outputInfo);

    armnn::INetworkPtr deserializedNetwork = DeserializeNetwork(SerializeNetwork(*network));
    BOOST_CHECK(deserializedNetwork);

    BatchToSpaceNdLayerVerifier verifier(layerName, {inputInfo}, {outputInfo}, desc);
    deserializedNetwork->Accept(verifier);
}

BOOST_AUTO_TEST_CASE(SerializeConstant)
{
    class ConstantLayerVerifier : public LayerVerifierBase
    {
    public:
        ConstantLayerVerifier(const std::string& layerName,
                              const std::vector<armnn::TensorInfo>& inputInfos,
                              const std::vector<armnn::TensorInfo>& outputInfos,
                              const armnn::ConstTensor& layerInput)
        : LayerVerifierBase(layerName, inputInfos, outputInfos)
        , m_LayerInput(layerInput) {}

        void VisitConstantLayer(const armnn::IConnectableLayer* layer,
                                const armnn::ConstTensor& input,
                                const char* name) override
        {
            VerifyNameAndConnections(layer, name);

            CompareConstTensor(input, m_LayerInput);
        }

        void VisitAdditionLayer(const armnn::IConnectableLayer* layer, const char* name = nullptr) override {}

    private:
        armnn::ConstTensor m_LayerInput;
    };

    const std::string layerName("constant");
    const armnn::TensorInfo info({ 2, 3 }, armnn::DataType::Float32);

    std::vector<float> constantData = GenerateRandomData<float>(info.GetNumElements());
    armnn::ConstTensor constTensor(info, constantData);

    armnn::INetworkPtr network(armnn::INetwork::Create());
    armnn::IConnectableLayer* input = network->AddInputLayer(0);
    armnn::IConnectableLayer* constant = network->AddConstantLayer(constTensor, layerName.c_str());
    armnn::IConnectableLayer* add = network->AddAdditionLayer();
    armnn::IConnectableLayer* output = network->AddOutputLayer(0);

    input->GetOutputSlot(0).Connect(add->GetInputSlot(0));
    constant->GetOutputSlot(0).Connect(add->GetInputSlot(1));
    add->GetOutputSlot(0).Connect(output->GetInputSlot(0));

    input->GetOutputSlot(0).SetTensorInfo(info);
    constant->GetOutputSlot(0).SetTensorInfo(info);
    add->GetOutputSlot(0).SetTensorInfo(info);

    armnn::INetworkPtr deserializedNetwork = DeserializeNetwork(SerializeNetwork(*network));
    BOOST_CHECK(deserializedNetwork);

    ConstantLayerVerifier verifier(layerName, {}, {info}, constTensor);
    deserializedNetwork->Accept(verifier);
}

BOOST_AUTO_TEST_CASE(SerializeConvolution2d)
{
    class Convolution2dLayerVerifier : public LayerVerifierBase
    {
    public:
        Convolution2dLayerVerifier(const std::string& layerName,
                                   const std::vector<armnn::TensorInfo>& inputInfos,
                                   const std::vector<armnn::TensorInfo>& outputInfos,
                                   const armnn::Convolution2dDescriptor& descriptor,
                                   const armnn::ConstTensor& weight,
                                   const armnn::Optional<armnn::ConstTensor>& bias)
        : LayerVerifierBase(layerName, inputInfos, outputInfos)
        , m_Descriptor(descriptor)
        , m_Weight(weight)
        , m_Bias(bias) {}

        void VisitConvolution2dLayer(const armnn::IConnectableLayer* layer,
                                     const armnn::Convolution2dDescriptor& descriptor,
                                     const armnn::ConstTensor& weight,
                                     const armnn::Optional<armnn::ConstTensor>& bias,
                                     const char* name) override
        {
            VerifyNameAndConnections(layer, name);
            VerifyDescriptor(descriptor);

            CompareConstTensor(weight, m_Weight);

            BOOST_TEST(bias.has_value() == m_Bias.has_value());
            if (bias.has_value() && m_Bias.has_value())
            {
                CompareConstTensor(bias.value(), m_Bias.value());
            }
        }

    private:
        void VerifyDescriptor(const armnn::Convolution2dDescriptor& descriptor)
        {
            BOOST_TEST(descriptor.m_PadLeft == m_Descriptor.m_PadLeft);
            BOOST_TEST(descriptor.m_PadRight == m_Descriptor.m_PadRight);
            BOOST_TEST(descriptor.m_PadTop == m_Descriptor.m_PadTop);
            BOOST_TEST(descriptor.m_PadBottom == m_Descriptor.m_PadBottom);
            BOOST_TEST(descriptor.m_StrideX == m_Descriptor.m_StrideX);
            BOOST_TEST(descriptor.m_StrideY == m_Descriptor.m_StrideY);
            BOOST_TEST(descriptor.m_BiasEnabled == m_Descriptor.m_BiasEnabled);
            BOOST_TEST(GetDataLayoutName(descriptor.m_DataLayout) == GetDataLayoutName(m_Descriptor.m_DataLayout));
        }

        armnn::Convolution2dDescriptor m_Descriptor;
        armnn::ConstTensor m_Weight;
        armnn::Optional<armnn::ConstTensor> m_Bias;
    };

    const std::string layerName("convolution2d");
    const armnn::TensorInfo inputInfo ({ 1, 5, 5, 1 }, armnn::DataType::Float32);
    const armnn::TensorInfo outputInfo({ 1, 3, 3, 1 }, armnn::DataType::Float32);

    const armnn::TensorInfo weightsInfo({ 1, 3, 3, 1 }, armnn::DataType::Float32);
    const armnn::TensorInfo biasesInfo ({ 1 }, armnn::DataType::Float32);

    std::vector<float> weightsData = GenerateRandomData<float>(weightsInfo.GetNumElements());
    armnn::ConstTensor weights(weightsInfo, weightsData);

    std::vector<float> biasesData = GenerateRandomData<float>(biasesInfo.GetNumElements());
    armnn::ConstTensor biases(biasesInfo, biasesData);

    armnn::Convolution2dDescriptor descriptor;
    descriptor.m_PadLeft     = 1;
    descriptor.m_PadRight    = 1;
    descriptor.m_PadTop      = 1;
    descriptor.m_PadBottom   = 1;
    descriptor.m_StrideX     = 2;
    descriptor.m_StrideY     = 2;
    descriptor.m_BiasEnabled = true;
    descriptor.m_DataLayout  = armnn::DataLayout::NHWC;

    armnn::INetworkPtr network = armnn::INetwork::Create();
    armnn::IConnectableLayer* const inputLayer  = network->AddInputLayer(0);
    armnn::IConnectableLayer* const convLayer   =
            network->AddConvolution2dLayer(descriptor,
                                           weights,
                                           armnn::Optional<armnn::ConstTensor>(biases),
                                           layerName.c_str());
    armnn::IConnectableLayer* const outputLayer = network->AddOutputLayer(0);

    inputLayer->GetOutputSlot(0).Connect(convLayer->GetInputSlot(0));
    convLayer->GetOutputSlot(0).Connect(outputLayer->GetInputSlot(0));

    inputLayer->GetOutputSlot(0).SetTensorInfo(inputInfo);
    convLayer->GetOutputSlot(0).SetTensorInfo(outputInfo);

    armnn::INetworkPtr deserializedNetwork = DeserializeNetwork(SerializeNetwork(*network));
    BOOST_CHECK(deserializedNetwork);

    Convolution2dLayerVerifier verifier(layerName, {inputInfo}, {outputInfo}, descriptor, weights, biases);
    deserializedNetwork->Accept(verifier);
}

BOOST_AUTO_TEST_CASE(SerializeDepthwiseConvolution2d)
{
    class DepthwiseConvolution2dLayerVerifier : public LayerVerifierBase
    {
    public:
        DepthwiseConvolution2dLayerVerifier(const std::string& layerName,
                                            const std::vector<armnn::TensorInfo>& inputInfos,
                                            const std::vector<armnn::TensorInfo>& outputInfos,
                                            const armnn::DepthwiseConvolution2dDescriptor& descriptor,
                                            const armnn::ConstTensor& weight,
                                            const armnn::Optional<armnn::ConstTensor>& bias)
        : LayerVerifierBase(layerName, inputInfos, outputInfos)
        , m_Descriptor(descriptor)
        , m_Weight(weight)
        , m_Bias(bias) {}

        void VisitDepthwiseConvolution2dLayer(const armnn::IConnectableLayer* layer,
                                              const armnn::DepthwiseConvolution2dDescriptor& descriptor,
                                              const armnn::ConstTensor& weight,
                                              const armnn::Optional<armnn::ConstTensor>& bias,
                                              const char* name) override
        {
            VerifyNameAndConnections(layer, name);
            VerifyDescriptor(descriptor);

            CompareConstTensor(weight, m_Weight);

            BOOST_TEST(bias.has_value() == m_Bias.has_value());
            if (bias.has_value() && m_Bias.has_value())
            {
                CompareConstTensor(bias.value(), m_Bias.value());
            }
        }

    private:
        void VerifyDescriptor(const armnn::DepthwiseConvolution2dDescriptor& descriptor)
        {
            BOOST_TEST(descriptor.m_PadLeft == m_Descriptor.m_PadLeft);
            BOOST_TEST(descriptor.m_PadRight == m_Descriptor.m_PadRight);
            BOOST_TEST(descriptor.m_PadTop == m_Descriptor.m_PadTop);
            BOOST_TEST(descriptor.m_PadBottom == m_Descriptor.m_PadBottom);
            BOOST_TEST(descriptor.m_StrideX == m_Descriptor.m_StrideX);
            BOOST_TEST(descriptor.m_StrideY == m_Descriptor.m_StrideY);
            BOOST_TEST(descriptor.m_BiasEnabled == m_Descriptor.m_BiasEnabled);
            BOOST_TEST(GetDataLayoutName(descriptor.m_DataLayout) == GetDataLayoutName(m_Descriptor.m_DataLayout));
        }

        armnn::DepthwiseConvolution2dDescriptor m_Descriptor;
        armnn::ConstTensor m_Weight;
        armnn::Optional<armnn::ConstTensor> m_Bias;
    };

    const std::string layerName("depwiseConvolution2d");
    const armnn::TensorInfo inputInfo ({ 1, 5, 5, 3 }, armnn::DataType::Float32);
    const armnn::TensorInfo outputInfo({ 1, 3, 3, 3 }, armnn::DataType::Float32);

    const armnn::TensorInfo weightsInfo({ 1, 3, 3, 3 }, armnn::DataType::Float32);
    const armnn::TensorInfo biasesInfo ({ 3 }, armnn::DataType::Float32);

    std::vector<float> weightsData = GenerateRandomData<float>(weightsInfo.GetNumElements());
    armnn::ConstTensor weights(weightsInfo, weightsData);

    std::vector<int32_t> biasesData = GenerateRandomData<int32_t>(biasesInfo.GetNumElements());
    armnn::ConstTensor biases(biasesInfo, biasesData);

    armnn::DepthwiseConvolution2dDescriptor descriptor;
    descriptor.m_StrideX     = 1;
    descriptor.m_StrideY     = 1;
    descriptor.m_BiasEnabled = true;
    descriptor.m_DataLayout  = armnn::DataLayout::NHWC;

    armnn::INetworkPtr network = armnn::INetwork::Create();
    armnn::IConnectableLayer* const inputLayer = network->AddInputLayer(0);
    armnn::IConnectableLayer* const depthwiseConvLayer =
        network->AddDepthwiseConvolution2dLayer(descriptor,
                                                weights,
                                                armnn::Optional<armnn::ConstTensor>(biases),
                                                layerName.c_str());
    armnn::IConnectableLayer* const outputLayer = network->AddOutputLayer(0);

    inputLayer->GetOutputSlot(0).Connect(depthwiseConvLayer->GetInputSlot(0));
    depthwiseConvLayer->GetOutputSlot(0).Connect(outputLayer->GetInputSlot(0));

    inputLayer->GetOutputSlot(0).SetTensorInfo(inputInfo);
    depthwiseConvLayer->GetOutputSlot(0).SetTensorInfo(outputInfo);

    armnn::INetworkPtr deserializedNetwork = DeserializeNetwork(SerializeNetwork(*network));
    BOOST_CHECK(deserializedNetwork);

    DepthwiseConvolution2dLayerVerifier verifier(layerName, {inputInfo}, {outputInfo}, descriptor, weights, biases);
    deserializedNetwork->Accept(verifier);
}

BOOST_AUTO_TEST_CASE(SerializeDequantize)
{
    class DequantizeLayerVerifier : public LayerVerifierBase
    {
    public:
        DequantizeLayerVerifier(const std::string& layerName,
                                const std::vector<armnn::TensorInfo>& inputInfos,
                                const std::vector<armnn::TensorInfo>& outputInfos)
        : LayerVerifierBase(layerName, inputInfos, outputInfos) {}

        void VisitDequantizeLayer(const armnn::IConnectableLayer* layer, const char* name) override
        {
            VerifyNameAndConnections(layer, name);
        }
    };

    const std::string layerName("dequantize");
    const armnn::TensorInfo inputInfo({ 1, 5, 2, 3 }, armnn::DataType::QuantisedAsymm8, 0.5f, 1);
    const armnn::TensorInfo outputInfo({ 1, 5, 2, 3 }, armnn::DataType::Float32);

    armnn::INetworkPtr network = armnn::INetwork::Create();
    armnn::IConnectableLayer* const inputLayer = network->AddInputLayer(0);
    armnn::IConnectableLayer* const dequantizeLayer = network->AddDequantizeLayer(layerName.c_str());
    armnn::IConnectableLayer* const outputLayer = network->AddOutputLayer(0);

    inputLayer->GetOutputSlot(0).Connect(dequantizeLayer->GetInputSlot(0));
    dequantizeLayer->GetOutputSlot(0).Connect(outputLayer->GetInputSlot(0));

    inputLayer->GetOutputSlot(0).SetTensorInfo(inputInfo);
    dequantizeLayer->GetOutputSlot(0).SetTensorInfo(outputInfo);

    armnn::INetworkPtr deserializedNetwork = DeserializeNetwork(SerializeNetwork(*network));
    BOOST_CHECK(deserializedNetwork);

    DequantizeLayerVerifier verifier(layerName, {inputInfo}, {outputInfo});
    deserializedNetwork->Accept(verifier);
}

BOOST_AUTO_TEST_CASE(SerializeDeserializeDetectionPostProcess)
{
    class DetectionPostProcessLayerVerifier : public LayerVerifierBase
    {
    public:
        DetectionPostProcessLayerVerifier(const std::string& layerName,
                                          const std::vector<armnn::TensorInfo>& inputInfos,
                                          const std::vector<armnn::TensorInfo>& outputInfos,
                                          const armnn::DetectionPostProcessDescriptor& descriptor,
                                          const armnn::ConstTensor& anchors)
        : LayerVerifierBase(layerName, inputInfos, outputInfos)
        , m_Descriptor(descriptor)
        , m_Anchors(anchors) {}

        void VisitDetectionPostProcessLayer(const armnn::IConnectableLayer* layer,
                                            const armnn::DetectionPostProcessDescriptor& descriptor,
                                            const armnn::ConstTensor& anchors,
                                            const char* name) override
        {
            VerifyNameAndConnections(layer, name);
            VerifyDescriptor(descriptor);

            CompareConstTensor(anchors, m_Anchors);
        }

    private:
        void VerifyDescriptor(const armnn::DetectionPostProcessDescriptor& descriptor)
        {
            BOOST_TEST(descriptor.m_UseRegularNms == m_Descriptor.m_UseRegularNms);
            BOOST_TEST(descriptor.m_MaxDetections == m_Descriptor.m_MaxDetections);
            BOOST_TEST(descriptor.m_MaxClassesPerDetection == m_Descriptor.m_MaxClassesPerDetection);
            BOOST_TEST(descriptor.m_DetectionsPerClass == m_Descriptor.m_DetectionsPerClass);
            BOOST_TEST(descriptor.m_NmsScoreThreshold == m_Descriptor.m_NmsScoreThreshold);
            BOOST_TEST(descriptor.m_NmsIouThreshold == m_Descriptor.m_NmsIouThreshold);
            BOOST_TEST(descriptor.m_NumClasses == m_Descriptor.m_NumClasses);
            BOOST_TEST(descriptor.m_ScaleY == m_Descriptor.m_ScaleY);
            BOOST_TEST(descriptor.m_ScaleX == m_Descriptor.m_ScaleX);
            BOOST_TEST(descriptor.m_ScaleH == m_Descriptor.m_ScaleH);
            BOOST_TEST(descriptor.m_ScaleW == m_Descriptor.m_ScaleW);
        }

        armnn::DetectionPostProcessDescriptor m_Descriptor;
        armnn::ConstTensor m_Anchors;
    };

    const std::string layerName("detectionPostProcess");

    const std::vector<armnn::TensorInfo> inputInfos({
        armnn::TensorInfo({ 1, 6, 4 }, armnn::DataType::Float32),
        armnn::TensorInfo({ 1, 6, 3}, armnn::DataType::Float32)
    });

    const std::vector<armnn::TensorInfo> outputInfos({
        armnn::TensorInfo({ 1, 3, 4 }, armnn::DataType::Float32),
        armnn::TensorInfo({ 1, 3 }, armnn::DataType::Float32),
        armnn::TensorInfo({ 1, 3 }, armnn::DataType::Float32),
        armnn::TensorInfo({ 1 }, armnn::DataType::Float32)
    });

    armnn::DetectionPostProcessDescriptor descriptor;
    descriptor.m_UseRegularNms = true;
    descriptor.m_MaxDetections = 3;
    descriptor.m_MaxClassesPerDetection = 1;
    descriptor.m_DetectionsPerClass =1;
    descriptor.m_NmsScoreThreshold = 0.0;
    descriptor.m_NmsIouThreshold = 0.5;
    descriptor.m_NumClasses = 2;
    descriptor.m_ScaleY = 10.0;
    descriptor.m_ScaleX = 10.0;
    descriptor.m_ScaleH = 5.0;
    descriptor.m_ScaleW = 5.0;

    const armnn::TensorInfo anchorsInfo({ 6, 4 }, armnn::DataType::Float32);
    const std::vector<float> anchorsData({
        0.5f, 0.5f, 1.0f, 1.0f,
        0.5f, 0.5f, 1.0f, 1.0f,
        0.5f, 0.5f, 1.0f, 1.0f,
        0.5f, 10.5f, 1.0f, 1.0f,
        0.5f, 10.5f, 1.0f, 1.0f,
        0.5f, 100.5f, 1.0f, 1.0f
    });
    armnn::ConstTensor anchors(anchorsInfo, anchorsData);

    armnn::INetworkPtr network = armnn::INetwork::Create();
    armnn::IConnectableLayer* const detectionLayer =
        network->AddDetectionPostProcessLayer(descriptor, anchors, layerName.c_str());

    for (unsigned int i = 0; i < 2; i++)
    {
        armnn::IConnectableLayer* const inputLayer = network->AddInputLayer(static_cast<int>(i));
        inputLayer->GetOutputSlot(0).Connect(detectionLayer->GetInputSlot(i));
        inputLayer->GetOutputSlot(0).SetTensorInfo(inputInfos[i]);
    }

    for (unsigned int i = 0; i < 4; i++)
    {
        armnn::IConnectableLayer* const outputLayer = network->AddOutputLayer(static_cast<int>(i));
        detectionLayer->GetOutputSlot(i).Connect(outputLayer->GetInputSlot(0));
        detectionLayer->GetOutputSlot(i).SetTensorInfo(outputInfos[i]);
    }

    armnn::INetworkPtr deserializedNetwork = DeserializeNetwork(SerializeNetwork(*network));
    BOOST_CHECK(deserializedNetwork);

    DetectionPostProcessLayerVerifier verifier(layerName, inputInfos, outputInfos, descriptor, anchors);
    deserializedNetwork->Accept(verifier);
}

BOOST_AUTO_TEST_CASE(SerializeDivision)
{
    class DivisionLayerVerifier : public LayerVerifierBase
    {
    public:
        DivisionLayerVerifier(const std::string& layerName,
                              const std::vector<armnn::TensorInfo>& inputInfos,
                              const std::vector<armnn::TensorInfo>& outputInfos)
        : LayerVerifierBase(layerName, inputInfos, outputInfos) {}

        void VisitDivisionLayer(const armnn::IConnectableLayer* layer, const char* name) override
        {
            VerifyNameAndConnections(layer, name);
        }
    };

    const std::string layerName("division");
    const armnn::TensorInfo info({ 1, 5, 2, 3 }, armnn::DataType::Float32);

    armnn::INetworkPtr network = armnn::INetwork::Create();
    armnn::IConnectableLayer* const inputLayer0 = network->AddInputLayer(0);
    armnn::IConnectableLayer* const inputLayer1 = network->AddInputLayer(1);
    armnn::IConnectableLayer* const divisionLayer = network->AddDivisionLayer(layerName.c_str());
    armnn::IConnectableLayer* const outputLayer = network->AddOutputLayer(0);

    inputLayer0->GetOutputSlot(0).Connect(divisionLayer->GetInputSlot(0));
    inputLayer1->GetOutputSlot(0).Connect(divisionLayer->GetInputSlot(1));
    divisionLayer->GetOutputSlot(0).Connect(outputLayer->GetInputSlot(0));

    inputLayer0->GetOutputSlot(0).SetTensorInfo(info);
    inputLayer1->GetOutputSlot(0).SetTensorInfo(info);
    divisionLayer->GetOutputSlot(0).SetTensorInfo(info);

    armnn::INetworkPtr deserializedNetwork = DeserializeNetwork(SerializeNetwork(*network));
    BOOST_CHECK(deserializedNetwork);

    DivisionLayerVerifier verifier(layerName, {info, info}, {info});
    deserializedNetwork->Accept(verifier);
}

BOOST_AUTO_TEST_CASE(SerializeEqual)
{
    class EqualLayerVerifier : public LayerVerifierBase
    {
    public:
        EqualLayerVerifier(const std::string& layerName,
                           const std::vector<armnn::TensorInfo>& inputInfos,
                           const std::vector<armnn::TensorInfo>& outputInfos)
        : LayerVerifierBase(layerName, inputInfos, outputInfos) {}

        void VisitEqualLayer(const armnn::IConnectableLayer* layer, const char* name) override
        {
            VerifyNameAndConnections(layer, name);
        }
    };

    const std::string layerName("equal");
    const armnn::TensorInfo inputTensorInfo1 = armnn::TensorInfo({2, 1, 2, 4}, armnn::DataType::Float32);
    const armnn::TensorInfo inputTensorInfo2 = armnn::TensorInfo({2, 1, 2, 4}, armnn::DataType::Float32);
    const armnn::TensorInfo outputTensorInfo = armnn::TensorInfo({2, 1, 2, 4}, armnn::DataType::Boolean);

    armnn::INetworkPtr network = armnn::INetwork::Create();
    armnn::IConnectableLayer* const inputLayer1 = network->AddInputLayer(0);
    armnn::IConnectableLayer* const inputLayer2 = network->AddInputLayer(1);
    armnn::IConnectableLayer* const equalLayer = network->AddEqualLayer(layerName.c_str());
    armnn::IConnectableLayer* const outputLayer = network->AddOutputLayer(0);

    inputLayer1->GetOutputSlot(0).Connect(equalLayer->GetInputSlot(0));
    inputLayer2->GetOutputSlot(0).Connect(equalLayer->GetInputSlot(1));
    equalLayer->GetOutputSlot(0).Connect(outputLayer->GetInputSlot(0));

    inputLayer1->GetOutputSlot(0).SetTensorInfo(inputTensorInfo1);
    inputLayer2->GetOutputSlot(0).SetTensorInfo(inputTensorInfo2);
    equalLayer->GetOutputSlot(0).SetTensorInfo(outputTensorInfo);

    armnn::INetworkPtr deserializedNetwork = DeserializeNetwork(SerializeNetwork(*network));
    BOOST_CHECK(deserializedNetwork);

    EqualLayerVerifier verifier(layerName, {inputTensorInfo1, inputTensorInfo2}, {outputTensorInfo});
    deserializedNetwork->Accept(verifier);
}

BOOST_AUTO_TEST_CASE(SerializeFloor)
{
    class FloorLayerVerifier : public LayerVerifierBase
    {
    public:
        FloorLayerVerifier(const std::string& layerName,
                           const std::vector<armnn::TensorInfo>& inputInfos,
                           const std::vector<armnn::TensorInfo>& outputInfos)
        : LayerVerifierBase(layerName, inputInfos, outputInfos) {}

        void VisitFloorLayer(const armnn::IConnectableLayer* layer, const char* name) override
        {
            VerifyNameAndConnections(layer, name);
        }
    };

    const std::string layerName("floor");
    const armnn::TensorInfo info({4,4}, armnn::DataType::Float32);

    armnn::INetworkPtr network = armnn::INetwork::Create();
    armnn::IConnectableLayer* const inputLayer = network->AddInputLayer(0);
    armnn::IConnectableLayer* const floorLayer = network->AddFloorLayer(layerName.c_str());
    armnn::IConnectableLayer* const outputLayer = network->AddOutputLayer(0);

    inputLayer->GetOutputSlot(0).Connect(floorLayer->GetInputSlot(0));
    floorLayer->GetOutputSlot(0).Connect(outputLayer->GetInputSlot(0));

    inputLayer->GetOutputSlot(0).SetTensorInfo(info);
    floorLayer->GetOutputSlot(0).SetTensorInfo(info);

    armnn::INetworkPtr deserializedNetwork = DeserializeNetwork(SerializeNetwork(*network));
    BOOST_CHECK(deserializedNetwork);

    FloorLayerVerifier verifier(layerName, {info}, {info});
    deserializedNetwork->Accept(verifier);
}

BOOST_AUTO_TEST_CASE(SerializeFullyConnected)
{
    class FullyConnectedLayerVerifier : public LayerVerifierBase
    {
    public:
        FullyConnectedLayerVerifier(const std::string& layerName,
                                    const std::vector<armnn::TensorInfo>& inputInfos,
                                    const std::vector<armnn::TensorInfo>& outputInfos,
                                    const armnn::FullyConnectedDescriptor& descriptor,
                                    const armnn::ConstTensor& weight,
                                    const armnn::Optional<armnn::ConstTensor>& bias)
        : LayerVerifierBase(layerName, inputInfos, outputInfos)
        , m_Descriptor(descriptor)
        , m_Weight(weight)
        , m_Bias(bias) {}

        void VisitFullyConnectedLayer(const armnn::IConnectableLayer* layer,
                                      const armnn::FullyConnectedDescriptor& descriptor,
                                      const armnn::ConstTensor& weight,
                                      const armnn::Optional<armnn::ConstTensor>& bias,
                                      const char* name) override
        {
            VerifyNameAndConnections(layer, name);
            VerifyDescriptor(descriptor);

            CompareConstTensor(weight, m_Weight);

            BOOST_TEST(bias.has_value() == m_Bias.has_value());
            if (bias.has_value() && m_Bias.has_value())
            {
                CompareConstTensor(bias.value(), m_Bias.value());
            }
        }

    private:
        void VerifyDescriptor(const armnn::FullyConnectedDescriptor& descriptor)
        {
            BOOST_TEST(descriptor.m_BiasEnabled == m_Descriptor.m_BiasEnabled);
            BOOST_TEST(descriptor.m_TransposeWeightMatrix == m_Descriptor.m_TransposeWeightMatrix);
        }

        armnn::FullyConnectedDescriptor m_Descriptor;
        armnn::ConstTensor m_Weight;
        armnn::Optional<armnn::ConstTensor> m_Bias;
    };

    const std::string layerName("fullyConnected");
    const armnn::TensorInfo inputInfo ({ 2, 5, 1, 1 }, armnn::DataType::Float32);
    const armnn::TensorInfo outputInfo({ 2, 3 }, armnn::DataType::Float32);

    const armnn::TensorInfo weightsInfo({ 5, 3 }, armnn::DataType::Float32);
    const armnn::TensorInfo biasesInfo ({ 3 }, armnn::DataType::Float32);
    std::vector<float> weightsData = GenerateRandomData<float>(weightsInfo.GetNumElements());
    std::vector<float> biasesData  = GenerateRandomData<float>(biasesInfo.GetNumElements());
    armnn::ConstTensor weights(weightsInfo, weightsData);
    armnn::ConstTensor biases(biasesInfo, biasesData);

    armnn::FullyConnectedDescriptor descriptor;
    descriptor.m_BiasEnabled = true;
    descriptor.m_TransposeWeightMatrix = false;

    armnn::INetworkPtr network = armnn::INetwork::Create();
    armnn::IConnectableLayer* const inputLayer = network->AddInputLayer(0);
    armnn::IConnectableLayer* const fullyConnectedLayer =
        network->AddFullyConnectedLayer(descriptor,
                                        weights,
                                        armnn::Optional<armnn::ConstTensor>(biases),
                                        layerName.c_str());
    armnn::IConnectableLayer* const outputLayer = network->AddOutputLayer(0);

    inputLayer->GetOutputSlot(0).Connect(fullyConnectedLayer->GetInputSlot(0));
    fullyConnectedLayer->GetOutputSlot(0).Connect(outputLayer->GetInputSlot(0));

    inputLayer->GetOutputSlot(0).SetTensorInfo(inputInfo);
    fullyConnectedLayer->GetOutputSlot(0).SetTensorInfo(outputInfo);

    armnn::INetworkPtr deserializedNetwork = DeserializeNetwork(SerializeNetwork(*network));
    BOOST_CHECK(deserializedNetwork);

    FullyConnectedLayerVerifier verifier(layerName, {inputInfo}, {outputInfo}, descriptor, weights, biases);
    deserializedNetwork->Accept(verifier);
}

BOOST_AUTO_TEST_CASE(SerializeGather)
{
    class GatherLayerVerifier : public LayerVerifierBase
    {
    public:
        GatherLayerVerifier(const std::string& layerName,
                            const std::vector<armnn::TensorInfo>& inputInfos,
                            const std::vector<armnn::TensorInfo>& outputInfos)
        : LayerVerifierBase(layerName, inputInfos, outputInfos) {}

        void VisitGatherLayer(const armnn::IConnectableLayer* layer, const char *name) override
        {
            VerifyNameAndConnections(layer, name);
        }

        void VisitConstantLayer(const armnn::IConnectableLayer* layer,
                                const armnn::ConstTensor& input,
                                const char *name) override {}
    };

    const std::string layerName("gather");
    armnn::TensorInfo paramsInfo({ 8 }, armnn::DataType::QuantisedAsymm8);
    armnn::TensorInfo outputInfo({ 3 }, armnn::DataType::QuantisedAsymm8);
    const armnn::TensorInfo indicesInfo({ 3 }, armnn::DataType::Signed32);

    paramsInfo.SetQuantizationScale(1.0f);
    paramsInfo.SetQuantizationOffset(0);
    outputInfo.SetQuantizationScale(1.0f);
    outputInfo.SetQuantizationOffset(0);

    const std::vector<int32_t>& indicesData = {7, 6, 5};

    armnn::INetworkPtr network = armnn::INetwork::Create();
    armnn::IConnectableLayer *const inputLayer = network->AddInputLayer(0);
    armnn::IConnectableLayer *const constantLayer =
            network->AddConstantLayer(armnn::ConstTensor(indicesInfo, indicesData));
    armnn::IConnectableLayer *const gatherLayer = network->AddGatherLayer(layerName.c_str());
    armnn::IConnectableLayer *const outputLayer = network->AddOutputLayer(0);

    inputLayer->GetOutputSlot(0).Connect(gatherLayer->GetInputSlot(0));
    constantLayer->GetOutputSlot(0).Connect(gatherLayer->GetInputSlot(1));
    gatherLayer->GetOutputSlot(0).Connect(outputLayer->GetInputSlot(0));

    inputLayer->GetOutputSlot(0).SetTensorInfo(paramsInfo);
    constantLayer->GetOutputSlot(0).SetTensorInfo(indicesInfo);
    gatherLayer->GetOutputSlot(0).SetTensorInfo(outputInfo);

    armnn::INetworkPtr deserializedNetwork = DeserializeNetwork(SerializeNetwork(*network));
    BOOST_CHECK(deserializedNetwork);

    GatherLayerVerifier verifier(layerName, {paramsInfo, indicesInfo}, {outputInfo});
    deserializedNetwork->Accept(verifier);
}

BOOST_AUTO_TEST_CASE(SerializeGreater)
{
    class GreaterLayerVerifier : public LayerVerifierBase
    {
    public:
        GreaterLayerVerifier(const std::string& layerName,
                             const std::vector<armnn::TensorInfo>& inputInfos,
                             const std::vector<armnn::TensorInfo>& outputInfos)
        : LayerVerifierBase(layerName, inputInfos, outputInfos) {}

        void VisitGreaterLayer(const armnn::IConnectableLayer* layer, const char* name) override
        {
            VerifyNameAndConnections(layer, name);
        }
    };

    const std::string layerName("greater");
    const armnn::TensorInfo inputTensorInfo1({ 1, 2, 2, 2 }, armnn::DataType::Float32);
    const armnn::TensorInfo inputTensorInfo2({ 1, 2, 2, 2 }, armnn::DataType::Float32);
    const armnn::TensorInfo outputTensorInfo({ 1, 2, 2, 2 }, armnn::DataType::Boolean);

    armnn::INetworkPtr network = armnn::INetwork::Create();
    armnn::IConnectableLayer* const inputLayer1 = network->AddInputLayer(0);
    armnn::IConnectableLayer* const inputLayer2 = network->AddInputLayer(1);
    armnn::IConnectableLayer* const greaterLayer = network->AddGreaterLayer(layerName.c_str());
    armnn::IConnectableLayer* const outputLayer = network->AddOutputLayer(0);

    inputLayer1->GetOutputSlot(0).Connect(greaterLayer->GetInputSlot(0));
    inputLayer2->GetOutputSlot(0).Connect(greaterLayer->GetInputSlot(1));
    greaterLayer->GetOutputSlot(0).Connect(outputLayer->GetInputSlot(0));

    inputLayer1->GetOutputSlot(0).SetTensorInfo(inputTensorInfo1);
    inputLayer2->GetOutputSlot(0).SetTensorInfo(inputTensorInfo2);
    greaterLayer->GetOutputSlot(0).SetTensorInfo(outputTensorInfo);

    armnn::INetworkPtr deserializedNetwork = DeserializeNetwork(SerializeNetwork(*network));
    BOOST_CHECK(deserializedNetwork);

    GreaterLayerVerifier verifier(layerName, {inputTensorInfo1, inputTensorInfo2}, {outputTensorInfo});
    deserializedNetwork->Accept(verifier);
}

BOOST_AUTO_TEST_CASE(SerializeL2Normalization)
{
    class L2NormalizationLayerVerifier : public LayerVerifierBase
    {
    public:
        L2NormalizationLayerVerifier(const std::string& layerName,
                                     const std::vector<armnn::TensorInfo>& inputInfos,
                                     const std::vector<armnn::TensorInfo>& outputInfos,
                                     const armnn::L2NormalizationDescriptor& descriptor)
        : LayerVerifierBase(layerName, inputInfos, outputInfos)
        , m_Descriptor(descriptor) {}

        void VisitL2NormalizationLayer(const armnn::IConnectableLayer* layer,
                                       const armnn::L2NormalizationDescriptor& descriptor,
                                       const char* name) override
        {
            VerifyNameAndConnections(layer, name);
            VerifyDescriptor(descriptor);
        }
    private:
        void VerifyDescriptor(const armnn::L2NormalizationDescriptor& descriptor)
        {
            BOOST_TEST(GetDataLayoutName(descriptor.m_DataLayout) == GetDataLayoutName(m_Descriptor.m_DataLayout));
        }

        armnn::L2NormalizationDescriptor m_Descriptor;
    };

    const std::string l2NormLayerName("l2Normalization");
    const armnn::TensorInfo info({1, 2, 1, 5}, armnn::DataType::Float32);

    armnn::L2NormalizationDescriptor desc;
    desc.m_DataLayout = armnn::DataLayout::NCHW;

    armnn::INetworkPtr network = armnn::INetwork::Create();
    armnn::IConnectableLayer* const inputLayer0 = network->AddInputLayer(0);
    armnn::IConnectableLayer* const l2NormLayer = network->AddL2NormalizationLayer(desc, l2NormLayerName.c_str());
    armnn::IConnectableLayer* const outputLayer = network->AddOutputLayer(0);

    inputLayer0->GetOutputSlot(0).Connect(l2NormLayer->GetInputSlot(0));
    l2NormLayer->GetOutputSlot(0).Connect(outputLayer->GetInputSlot(0));

    inputLayer0->GetOutputSlot(0).SetTensorInfo(info);
    l2NormLayer->GetOutputSlot(0).SetTensorInfo(info);

    armnn::INetworkPtr deserializedNetwork = DeserializeNetwork(SerializeNetwork(*network));
    BOOST_CHECK(deserializedNetwork);

    L2NormalizationLayerVerifier verifier(l2NormLayerName, {info}, {info}, desc);
    deserializedNetwork->Accept(verifier);
}

BOOST_AUTO_TEST_CASE(SerializeMaximum)
{
    class MaximumLayerVerifier : public LayerVerifierBase
    {
    public:
        MaximumLayerVerifier(const std::string& layerName,
                             const std::vector<armnn::TensorInfo>& inputInfos,
                             const std::vector<armnn::TensorInfo>& outputInfos)
        : LayerVerifierBase(layerName, inputInfos, outputInfos) {}

        void VisitMaximumLayer(const armnn::IConnectableLayer* layer, const char* name) override
        {
            VerifyNameAndConnections(layer, name);
        }
    };

    const std::string layerName("maximum");
    const armnn::TensorInfo info({ 1, 2, 2, 3 }, armnn::DataType::Float32);

    armnn::INetworkPtr network = armnn::INetwork::Create();
    armnn::IConnectableLayer* const inputLayer0 = network->AddInputLayer(0);
    armnn::IConnectableLayer* const inputLayer1 = network->AddInputLayer(1);
    armnn::IConnectableLayer* const maximumLayer = network->AddMaximumLayer(layerName.c_str());
    armnn::IConnectableLayer* const outputLayer = network->AddOutputLayer(0);

    inputLayer0->GetOutputSlot(0).Connect(maximumLayer->GetInputSlot(0));
    inputLayer1->GetOutputSlot(0).Connect(maximumLayer->GetInputSlot(1));
    maximumLayer->GetOutputSlot(0).Connect(outputLayer->GetInputSlot(0));

    inputLayer0->GetOutputSlot(0).SetTensorInfo(info);
    inputLayer1->GetOutputSlot(0).SetTensorInfo(info);
    maximumLayer->GetOutputSlot(0).SetTensorInfo(info);

    armnn::INetworkPtr deserializedNetwork = DeserializeNetwork(SerializeNetwork(*network));
    BOOST_CHECK(deserializedNetwork);

    MaximumLayerVerifier verifier(layerName, {info, info}, {info});
    deserializedNetwork->Accept(verifier);
}

BOOST_AUTO_TEST_CASE(SerializeMean)
{
    class MeanLayerVerifier : public LayerVerifierBase
    {
    public:
        MeanLayerVerifier(const std::string& layerName,
                          const std::vector<armnn::TensorInfo>& inputInfos,
                          const std::vector<armnn::TensorInfo>& outputInfos,
                          const armnn::MeanDescriptor& descriptor)
        : LayerVerifierBase(layerName, inputInfos, outputInfos)
        , m_Descriptor(descriptor) {}

        void VisitMeanLayer(const armnn::IConnectableLayer* layer,
                            const armnn::MeanDescriptor& descriptor,
                            const char* name) override
        {
            VerifyNameAndConnections(layer, name);
            VerifyDescriptor(descriptor);
        }

    private:
        void VerifyDescriptor(const armnn::MeanDescriptor& descriptor)
        {
            BOOST_TEST(descriptor.m_Axis == m_Descriptor.m_Axis);
            BOOST_TEST(descriptor.m_KeepDims == m_Descriptor.m_KeepDims);
        }

        armnn::MeanDescriptor m_Descriptor;
    };

    const std::string layerName("mean");
    const armnn::TensorInfo inputInfo({1, 1, 3, 2}, armnn::DataType::Float32);
    const armnn::TensorInfo outputInfo({1, 1, 1, 2}, armnn::DataType::Float32);

    armnn::MeanDescriptor descriptor;
    descriptor.m_Axis = { 2 };
    descriptor.m_KeepDims = true;

    armnn::INetworkPtr network = armnn::INetwork::Create();
    armnn::IConnectableLayer* const inputLayer   = network->AddInputLayer(0);
    armnn::IConnectableLayer* const meanLayer = network->AddMeanLayer(descriptor, layerName.c_str());
    armnn::IConnectableLayer* const outputLayer  = network->AddOutputLayer(0);

    inputLayer->GetOutputSlot(0).Connect(meanLayer->GetInputSlot(0));
    meanLayer->GetOutputSlot(0).Connect(outputLayer->GetInputSlot(0));

    inputLayer->GetOutputSlot(0).SetTensorInfo(inputInfo);
    meanLayer->GetOutputSlot(0).SetTensorInfo(outputInfo);

    armnn::INetworkPtr deserializedNetwork = DeserializeNetwork(SerializeNetwork(*network));
    BOOST_CHECK(deserializedNetwork);

    MeanLayerVerifier verifier(layerName, {inputInfo}, {outputInfo}, descriptor);
    deserializedNetwork->Accept(verifier);
}

BOOST_AUTO_TEST_CASE(SerializeMerge)
{
    class MergeLayerVerifier : public LayerVerifierBase
    {
    public:
        MergeLayerVerifier(const std::string& layerName,
                           const std::vector<armnn::TensorInfo>& inputInfos,
                           const std::vector<armnn::TensorInfo>& outputInfos)
        : LayerVerifierBase(layerName, inputInfos, outputInfos) {}

        void VisitMergeLayer(const armnn::IConnectableLayer* layer, const char* name) override
        {
            VerifyNameAndConnections(layer, name);
        }
    };

    const std::string layerName("merge");
    const armnn::TensorInfo info({ 1, 2, 2, 3 }, armnn::DataType::Float32);

    armnn::INetworkPtr network = armnn::INetwork::Create();
    armnn::IConnectableLayer* const inputLayer0 = network->AddInputLayer(0);
    armnn::IConnectableLayer* const inputLayer1 = network->AddInputLayer(1);
    armnn::IConnectableLayer* const mergeLayer = network->AddMergeLayer(layerName.c_str());
    armnn::IConnectableLayer* const outputLayer = network->AddOutputLayer(0);

    inputLayer0->GetOutputSlot(0).Connect(mergeLayer->GetInputSlot(0));
    inputLayer1->GetOutputSlot(0).Connect(mergeLayer->GetInputSlot(1));
    mergeLayer->GetOutputSlot(0).Connect(outputLayer->GetInputSlot(0));

    inputLayer0->GetOutputSlot(0).SetTensorInfo(info);
    inputLayer1->GetOutputSlot(0).SetTensorInfo(info);
    mergeLayer->GetOutputSlot(0).SetTensorInfo(info);

    armnn::INetworkPtr deserializedNetwork = DeserializeNetwork(SerializeNetwork(*network));
    BOOST_CHECK(deserializedNetwork);

    MergeLayerVerifier verifier(layerName, {info, info}, {info});
    deserializedNetwork->Accept(verifier);
}

class MergerLayerVerifier : public LayerVerifierBase
{
public:
    MergerLayerVerifier(const std::string& layerName,
                        const std::vector<armnn::TensorInfo>& inputInfos,
                        const std::vector<armnn::TensorInfo>& outputInfos,
                        const armnn::OriginsDescriptor& descriptor)
        : LayerVerifierBase(layerName, inputInfos, outputInfos)
        , m_Descriptor(descriptor) {}

    void VisitMergerLayer(const armnn::IConnectableLayer* layer,
                          const armnn::OriginsDescriptor& descriptor,
                          const char* name) override
    {
        VerifyNameAndConnections(layer, name);
        VerifyDescriptor(descriptor);
    }

private:
    void VerifyDescriptor(const armnn::OriginsDescriptor& descriptor)
    {
        BOOST_TEST(descriptor.GetConcatAxis() == m_Descriptor.GetConcatAxis());
        BOOST_TEST(descriptor.GetNumViews() == m_Descriptor.GetNumViews());
        BOOST_TEST(descriptor.GetNumDimensions() == m_Descriptor.GetNumDimensions());

        for (uint32_t i = 0; i < descriptor.GetNumViews(); i++)
        {
            for (uint32_t j = 0; j < descriptor.GetNumDimensions(); j++)
            {
                BOOST_TEST(descriptor.GetViewOrigin(i)[j] == m_Descriptor.GetViewOrigin(i)[j]);
            }
        }
    }

    armnn::OriginsDescriptor m_Descriptor;
};

BOOST_AUTO_TEST_CASE(SerializeMerger)
{
    const std::string layerName("merger");
    const armnn::TensorInfo inputInfo = armnn::TensorInfo({2, 3, 2, 2}, armnn::DataType::Float32);
    const armnn::TensorInfo outputInfo = armnn::TensorInfo({4, 3, 2, 2}, armnn::DataType::Float32);

    const std::vector<armnn::TensorShape> shapes({inputInfo.GetShape(), inputInfo.GetShape()});

    armnn::OriginsDescriptor descriptor =
        armnn::CreateDescriptorForConcatenation(shapes.begin(), shapes.end(), 0);

    armnn::INetworkPtr network = armnn::INetwork::Create();
    armnn::IConnectableLayer* const inputLayerOne = network->AddInputLayer(0);
    armnn::IConnectableLayer* const inputLayerTwo = network->AddInputLayer(1);
    ARMNN_NO_DEPRECATE_WARN_BEGIN
    armnn::IConnectableLayer* const mergerLayer = network->AddMergerLayer(descriptor, layerName.c_str());
    ARMNN_NO_DEPRECATE_WARN_END
    armnn::IConnectableLayer* const outputLayer = network->AddOutputLayer(0);

    inputLayerOne->GetOutputSlot(0).Connect(mergerLayer->GetInputSlot(0));
    inputLayerTwo->GetOutputSlot(0).Connect(mergerLayer->GetInputSlot(1));
    mergerLayer->GetOutputSlot(0).Connect(outputLayer->GetInputSlot(0));

    inputLayerOne->GetOutputSlot(0).SetTensorInfo(inputInfo);
    inputLayerTwo->GetOutputSlot(0).SetTensorInfo(inputInfo);
    mergerLayer->GetOutputSlot(0).SetTensorInfo(outputInfo);

    std::string mergerLayerNetwork = SerializeNetwork(*network);
    armnn::INetworkPtr deserializedNetwork = DeserializeNetwork(mergerLayerNetwork);
    BOOST_CHECK(deserializedNetwork);

    MergerLayerVerifier verifier(layerName, {inputInfo, inputInfo}, {outputInfo}, descriptor);
    deserializedNetwork->Accept(verifier);
}

BOOST_AUTO_TEST_CASE(EnsureMergerLayerBackwardCompatibility)
{
    // The hex array below is a flat buffer containing a simple network with two inputs
    // a merger layer (soon to be a thing of the past) and an output layer with dimensions
    // as per the tensor infos below.
    // The intention is that this test will be repurposed as soon as the MergerLayer
    // is replaced by a ConcatLayer to verify that we can still read back these old style
    // models replacing the MergerLayers with ConcatLayers with the same parameters.
    // To do this the MergerLayerVerifier will be changed to have a VisitConcatLayer
    // which will do the work that the VisitMergerLayer currently does and the VisitMergerLayer
    // so long as it remains (public API will drop Merger Layer at some future point)
    // will throw an error if invoked because none of the graphs we create should contain
    // Merger layers now regardless of whether we attempt to insert the Merger layer via
    // the INetwork.AddMergerLayer call or by deserializing an old style flatbuffer file.
    unsigned int size = 760;
    const unsigned char mergerModel[] = {
            0x10,0x00,0x00,0x00,0x00,0x00,0x0A,0x00,0x10,0x00,0x04,0x00,0x08,0x00,0x0C,0x00,0x0A,0x00,0x00,0x00,
            0x0C,0x00,0x00,0x00,0x1C,0x00,0x00,0x00,0x24,0x00,0x00,0x00,0x04,0x00,0x00,0x00,0x38,0x02,0x00,0x00,
            0x8C,0x01,0x00,0x00,0x70,0x00,0x00,0x00,0x18,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
            0x01,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x03,0x00,0x00,0x00,0xF4,0xFD,0xFF,0xFF,0x00,0x00,0x00,0x0B,
            0x04,0x00,0x00,0x00,0x92,0xFE,0xFF,0xFF,0x04,0x00,0x00,0x00,0x9A,0xFE,0xFF,0xFF,0x04,0x00,0x00,0x00,
            0x7E,0xFE,0xFF,0xFF,0x03,0x00,0x00,0x00,0x10,0x00,0x00,0x00,0x03,0x00,0x00,0x00,0x10,0x00,0x00,0x00,
            0x14,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x08,0x00,0x00,0x00,
            0x00,0x00,0x00,0x00,0xF8,0xFE,0xFF,0xFF,0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x48,0xFE,0xFF,0xFF,
            0x00,0x00,0x00,0x1F,0x0C,0x00,0x00,0x00,0x08,0x00,0x0C,0x00,0x04,0x00,0x08,0x00,0x08,0x00,0x00,0x00,
            0x68,0x00,0x00,0x00,0x10,0x00,0x00,0x00,0x0C,0x00,0x10,0x00,0x00,0x00,0x04,0x00,0x08,0x00,0x0C,0x00,
            0x0C,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x04,0x00,0x00,0x00,0x04,0x00,0x00,0x00,0x02,0x00,0x00,0x00,
            0x24,0x00,0x00,0x00,0x04,0x00,0x00,0x00,0x22,0xFF,0xFF,0xFF,0x04,0x00,0x00,0x00,0x04,0x00,0x00,0x00,
            0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x3E,0xFF,0xFF,0xFF,
            0x04,0x00,0x00,0x00,0x04,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
            0x00,0x00,0x00,0x00,0x36,0xFF,0xFF,0xFF,0x02,0x00,0x00,0x00,0x10,0x00,0x00,0x00,0x1E,0x00,0x00,0x00,
            0x14,0x00,0x00,0x00,0x1C,0x00,0x00,0x00,0x06,0x00,0x00,0x00,0x6D,0x65,0x72,0x67,0x65,0x72,0x00,0x00,
            0x02,0x00,0x00,0x00,0x5C,0x00,0x00,0x00,0x40,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x04,0x00,0x00,0x00,
            0x34,0xFF,0xFF,0xFF,0x04,0x00,0x00,0x00,0x92,0xFE,0xFF,0xFF,0x00,0x00,0x00,0x01,0x08,0x00,0x00,0x00,
            0x00,0x00,0x00,0x00,0x04,0x00,0x00,0x00,0x04,0x00,0x00,0x00,0x03,0x00,0x00,0x00,0x02,0x00,0x00,0x00,
            0x02,0x00,0x00,0x00,0x08,0x00,0x10,0x00,0x04,0x00,0x08,0x00,0x08,0x00,0x00,0x00,0x01,0x00,0x00,0x00,
            0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x08,0x00,0x0C,0x00,0x00,0x00,0x04,0x00,0x08,0x00,0x00,0x00,
            0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x08,0x00,0x0E,0x00,0x07,0x00,0x08,0x00,0x08,0x00,0x00,0x00,
            0x00,0x00,0x00,0x09,0x0C,0x00,0x00,0x00,0x00,0x00,0x06,0x00,0x08,0x00,0x04,0x00,0x06,0x00,0x00,0x00,
            0x0C,0x00,0x00,0x00,0x08,0x00,0x0E,0x00,0x04,0x00,0x08,0x00,0x08,0x00,0x00,0x00,0x18,0x00,0x00,0x00,
            0x01,0x00,0x00,0x00,0x00,0x00,0x0E,0x00,0x18,0x00,0x04,0x00,0x08,0x00,0x0C,0x00,0x10,0x00,0x14,0x00,
            0x0E,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x10,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x10,0x00,0x00,0x00,
            0x10,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,
            0x0C,0x00,0x00,0x00,0x08,0x00,0x08,0x00,0x00,0x00,0x04,0x00,0x08,0x00,0x00,0x00,0x04,0x00,0x00,0x00,
            0x66,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x01,0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x04,0x00,0x00,0x00,
            0x02,0x00,0x00,0x00,0x03,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x08,0x00,0x0C,0x00,
            0x07,0x00,0x08,0x00,0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x09,0x04,0x00,0x00,0x00,0xF6,0xFF,0xFF,0xFF,
            0x0C,0x00,0x00,0x00,0x00,0x00,0x06,0x00,0x0A,0x00,0x04,0x00,0x06,0x00,0x00,0x00,0x14,0x00,0x00,0x00,
            0x00,0x00,0x0E,0x00,0x14,0x00,0x00,0x00,0x04,0x00,0x08,0x00,0x0C,0x00,0x10,0x00,0x0E,0x00,0x00,0x00,
            0x10,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x10,0x00,0x00,0x00,0x10,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
            0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x0C,0x00,0x00,0x00,0x08,0x00,0x0A,0x00,
            0x00,0x00,0x04,0x00,0x08,0x00,0x00,0x00,0x10,0x00,0x00,0x00,0x00,0x00,0x0A,0x00,0x10,0x00,0x08,0x00,
            0x07,0x00,0x0C,0x00,0x0A,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
            0x04,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x03,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x02,0x00,0x00,0x00};
    std::stringstream ss;
    for (unsigned int i = 0; i < size; ++i)
    {
        ss << mergerModel[i];
    }
    std::string mergerLayerNetwork = ss.str();
    armnn::INetworkPtr deserializedNetwork = DeserializeNetwork(mergerLayerNetwork);
    BOOST_CHECK(deserializedNetwork);
    const std::string layerName("merger");
    const armnn::TensorInfo inputInfo = armnn::TensorInfo({2, 3, 2, 2}, armnn::DataType::Float32);
    const armnn::TensorInfo outputInfo = armnn::TensorInfo({4, 3, 2, 2}, armnn::DataType::Float32);

    const std::vector<armnn::TensorShape> shapes({inputInfo.GetShape(), inputInfo.GetShape()});

    armnn::OriginsDescriptor descriptor =
            armnn::CreateDescriptorForConcatenation(shapes.begin(), shapes.end(), 0);

    MergerLayerVerifier verifier(layerName, {inputInfo, inputInfo}, {outputInfo}, descriptor);
    deserializedNetwork->Accept(verifier);
}

BOOST_AUTO_TEST_CASE(SerializeMinimum)
{
    class MinimumLayerVerifier : public LayerVerifierBase
    {
    public:
        MinimumLayerVerifier(const std::string& layerName,
                             const std::vector<armnn::TensorInfo>& inputInfos,
                             const std::vector<armnn::TensorInfo>& outputInfos)
        : LayerVerifierBase(layerName, inputInfos, outputInfos) {}

        void VisitMinimumLayer(const armnn::IConnectableLayer* layer, const char* name) override
        {
            VerifyNameAndConnections(layer, name);
        }
    };

    const std::string layerName("minimum");
    const armnn::TensorInfo info({ 1, 2, 2, 3 }, armnn::DataType::Float32);

    armnn::INetworkPtr network = armnn::INetwork::Create();
    armnn::IConnectableLayer* const inputLayer0 = network->AddInputLayer(0);
    armnn::IConnectableLayer* const inputLayer1 = network->AddInputLayer(1);
    armnn::IConnectableLayer* const minimumLayer = network->AddMinimumLayer(layerName.c_str());
    armnn::IConnectableLayer* const outputLayer = network->AddOutputLayer(0);

    inputLayer0->GetOutputSlot(0).Connect(minimumLayer->GetInputSlot(0));
    inputLayer1->GetOutputSlot(0).Connect(minimumLayer->GetInputSlot(1));
    minimumLayer->GetOutputSlot(0).Connect(outputLayer->GetInputSlot(0));

    inputLayer0->GetOutputSlot(0).SetTensorInfo(info);
    inputLayer1->GetOutputSlot(0).SetTensorInfo(info);
    minimumLayer->GetOutputSlot(0).SetTensorInfo(info);

    armnn::INetworkPtr deserializedNetwork = DeserializeNetwork(SerializeNetwork(*network));
    BOOST_CHECK(deserializedNetwork);

    MinimumLayerVerifier verifier(layerName, {info, info}, {info});
    deserializedNetwork->Accept(verifier);
}

BOOST_AUTO_TEST_CASE(SerializeMultiplication)
{
    class MultiplicationLayerVerifier : public LayerVerifierBase
    {
    public:
        MultiplicationLayerVerifier(const std::string& layerName,
                                    const std::vector<armnn::TensorInfo>& inputInfos,
                                    const std::vector<armnn::TensorInfo>& outputInfos)
        : LayerVerifierBase(layerName, inputInfos, outputInfos) {}

        void VisitMultiplicationLayer(const armnn::IConnectableLayer* layer, const char* name) override
        {
            VerifyNameAndConnections(layer, name);
        }
    };

    const std::string layerName("multiplication");
    const armnn::TensorInfo info({ 1, 5, 2, 3 }, armnn::DataType::Float32);

    armnn::INetworkPtr network = armnn::INetwork::Create();
    armnn::IConnectableLayer* const inputLayer0 = network->AddInputLayer(0);
    armnn::IConnectableLayer* const inputLayer1 = network->AddInputLayer(1);
    armnn::IConnectableLayer* const multiplicationLayer = network->AddMultiplicationLayer(layerName.c_str());
    armnn::IConnectableLayer* const outputLayer = network->AddOutputLayer(0);

    inputLayer0->GetOutputSlot(0).Connect(multiplicationLayer->GetInputSlot(0));
    inputLayer1->GetOutputSlot(0).Connect(multiplicationLayer->GetInputSlot(1));
    multiplicationLayer->GetOutputSlot(0).Connect(outputLayer->GetInputSlot(0));

    inputLayer0->GetOutputSlot(0).SetTensorInfo(info);
    inputLayer1->GetOutputSlot(0).SetTensorInfo(info);
    multiplicationLayer->GetOutputSlot(0).SetTensorInfo(info);

    armnn::INetworkPtr deserializedNetwork = DeserializeNetwork(SerializeNetwork(*network));
    BOOST_CHECK(deserializedNetwork);

    MultiplicationLayerVerifier verifier(layerName, {info, info}, {info});
    deserializedNetwork->Accept(verifier);
}

BOOST_AUTO_TEST_CASE(SerializeNormalization)
{
    class NormalizationLayerVerifier : public LayerVerifierBase
    {
    public:
        NormalizationLayerVerifier(const std::string& layerName,
                                   const std::vector<armnn::TensorInfo>& inputInfos,
                                   const std::vector<armnn::TensorInfo>& outputInfos,
                                   const armnn::NormalizationDescriptor& descriptor)
        : LayerVerifierBase(layerName, inputInfos, outputInfos)
        , m_Descriptor(descriptor) {}

        void VisitNormalizationLayer(const armnn::IConnectableLayer* layer,
                                     const armnn::NormalizationDescriptor& descriptor,
                                     const char* name) override
        {
            VerifyNameAndConnections(layer, name);
            VerifyDescriptor(descriptor);
        }

    private:
        void VerifyDescriptor(const armnn::NormalizationDescriptor& descriptor)
        {
            BOOST_TEST(GetDataLayoutName(descriptor.m_DataLayout) == GetDataLayoutName(m_Descriptor.m_DataLayout));
            BOOST_TEST(descriptor.m_NormSize == m_Descriptor.m_NormSize);
            BOOST_TEST(descriptor.m_Alpha == m_Descriptor.m_Alpha);
            BOOST_TEST(descriptor.m_Beta == m_Descriptor.m_Beta);
            BOOST_TEST(descriptor.m_K == m_Descriptor.m_K);
            BOOST_TEST(
                static_cast<int>(descriptor.m_NormChannelType) == static_cast<int>(m_Descriptor.m_NormChannelType));
            BOOST_TEST(
                static_cast<int>(descriptor.m_NormMethodType) == static_cast<int>(m_Descriptor.m_NormMethodType));
        }

        armnn::NormalizationDescriptor m_Descriptor;
    };

    const std::string layerName("normalization");
    const armnn::TensorInfo info({2, 1, 2, 2}, armnn::DataType::Float32);

    armnn::NormalizationDescriptor desc;
    desc.m_DataLayout = armnn::DataLayout::NCHW;
    desc.m_NormSize = 3;
    desc.m_Alpha = 1;
    desc.m_Beta = 1;
    desc.m_K = 1;

    armnn::INetworkPtr network = armnn::INetwork::Create();
    armnn::IConnectableLayer* const inputLayer = network->AddInputLayer(0);
    armnn::IConnectableLayer* const normalizationLayer = network->AddNormalizationLayer(desc, layerName.c_str());
    armnn::IConnectableLayer* const outputLayer = network->AddOutputLayer(0);

    inputLayer->GetOutputSlot(0).Connect(normalizationLayer->GetInputSlot(0));
    normalizationLayer->GetOutputSlot(0).Connect(outputLayer->GetInputSlot(0));

    inputLayer->GetOutputSlot(0).SetTensorInfo(info);
    normalizationLayer->GetOutputSlot(0).SetTensorInfo(info);

    armnn::INetworkPtr deserializedNetwork = DeserializeNetwork(SerializeNetwork(*network));
    BOOST_CHECK(deserializedNetwork);

    NormalizationLayerVerifier verifier(layerName, {info}, {info}, desc);
    deserializedNetwork->Accept(verifier);
}

BOOST_AUTO_TEST_CASE(SerializePad)
{
    class PadLayerVerifier : public LayerVerifierBase
    {
    public:
        PadLayerVerifier(const std::string& layerName,
                         const std::vector<armnn::TensorInfo>& inputInfos,
                         const std::vector<armnn::TensorInfo>& outputInfos,
                         const armnn::PadDescriptor& descriptor)
        : LayerVerifierBase(layerName, inputInfos, outputInfos)
        , m_Descriptor(descriptor) {}

        void VisitPadLayer(const armnn::IConnectableLayer* layer,
                           const armnn::PadDescriptor& descriptor,
                           const char* name) override
        {
            VerifyNameAndConnections(layer, name);
            VerifyDescriptor(descriptor);
        }

    private:
        void VerifyDescriptor(const armnn::PadDescriptor& descriptor)
        {
            BOOST_TEST(descriptor.m_PadList == m_Descriptor.m_PadList);
        }

        armnn::PadDescriptor m_Descriptor;
    };

    const std::string layerName("pad");
    const armnn::TensorInfo inputTensorInfo = armnn::TensorInfo({1, 2, 3, 4}, armnn::DataType::Float32);
    const armnn::TensorInfo outputTensorInfo = armnn::TensorInfo({1, 3, 5, 7}, armnn::DataType::Float32);

    armnn::PadDescriptor desc({{0, 0}, {1, 0}, {1, 1}, {1, 2}});

    armnn::INetworkPtr network = armnn::INetwork::Create();
    armnn::IConnectableLayer* const inputLayer = network->AddInputLayer(0);
    armnn::IConnectableLayer* const padLayer = network->AddPadLayer(desc, layerName.c_str());
    armnn::IConnectableLayer* const outputLayer = network->AddOutputLayer(0);

    inputLayer->GetOutputSlot(0).Connect(padLayer->GetInputSlot(0));
    padLayer->GetOutputSlot(0).Connect(outputLayer->GetInputSlot(0));

    inputLayer->GetOutputSlot(0).SetTensorInfo(inputTensorInfo);
    padLayer->GetOutputSlot(0).SetTensorInfo(outputTensorInfo);

    armnn::INetworkPtr deserializedNetwork = DeserializeNetwork(SerializeNetwork(*network));
    BOOST_CHECK(deserializedNetwork);

    PadLayerVerifier verifier(layerName, {inputTensorInfo}, {outputTensorInfo}, desc);
    deserializedNetwork->Accept(verifier);
}

BOOST_AUTO_TEST_CASE(SerializePermute)
{
    class PermuteLayerVerifier : public LayerVerifierBase
    {
    public:
        PermuteLayerVerifier(const std::string& layerName,
                             const std::vector<armnn::TensorInfo>& inputInfos,
                             const std::vector<armnn::TensorInfo>& outputInfos,
                             const armnn::PermuteDescriptor& descriptor)
        : LayerVerifierBase(layerName, inputInfos, outputInfos)
        , m_Descriptor(descriptor) {}

        void VisitPermuteLayer(const armnn::IConnectableLayer* layer,
                               const armnn::PermuteDescriptor& descriptor,
                               const char* name) override
        {
            VerifyNameAndConnections(layer, name);
            VerifyDescriptor(descriptor);
        }

    private:
        void VerifyDescriptor(const armnn::PermuteDescriptor& descriptor)
        {
            BOOST_TEST(descriptor.m_DimMappings.IsEqual(m_Descriptor.m_DimMappings));
        }

        armnn::PermuteDescriptor m_Descriptor;
    };

    const std::string layerName("permute");
    const armnn::TensorInfo inputTensorInfo({4, 3, 2, 1}, armnn::DataType::Float32);
    const armnn::TensorInfo outputTensorInfo({1, 2, 3, 4}, armnn::DataType::Float32);

    armnn::PermuteDescriptor descriptor(armnn::PermutationVector({3, 2, 1, 0}));

    armnn::INetworkPtr network = armnn::INetwork::Create();
    armnn::IConnectableLayer* const inputLayer = network->AddInputLayer(0);
    armnn::IConnectableLayer* const permuteLayer = network->AddPermuteLayer(descriptor, layerName.c_str());
    armnn::IConnectableLayer* const outputLayer = network->AddOutputLayer(0);

    inputLayer->GetOutputSlot(0).Connect(permuteLayer->GetInputSlot(0));
    permuteLayer->GetOutputSlot(0).Connect(outputLayer->GetInputSlot(0));

    inputLayer->GetOutputSlot(0).SetTensorInfo(inputTensorInfo);
    permuteLayer->GetOutputSlot(0).SetTensorInfo(outputTensorInfo);

    armnn::INetworkPtr deserializedNetwork = DeserializeNetwork(SerializeNetwork(*network));
    BOOST_CHECK(deserializedNetwork);

    PermuteLayerVerifier verifier(layerName, {inputTensorInfo}, {outputTensorInfo}, descriptor);
    deserializedNetwork->Accept(verifier);
}

BOOST_AUTO_TEST_CASE(SerializePooling2d)
{
    class Pooling2dLayerVerifier : public LayerVerifierBase
    {
    public:
        Pooling2dLayerVerifier(const std::string& layerName,
                               const std::vector<armnn::TensorInfo>& inputInfos,
                               const std::vector<armnn::TensorInfo>& outputInfos,
                               const armnn::Pooling2dDescriptor& descriptor)
        : LayerVerifierBase(layerName, inputInfos, outputInfos)
        , m_Descriptor(descriptor) {}

        void VisitPooling2dLayer(const armnn::IConnectableLayer* layer,
                                 const armnn::Pooling2dDescriptor& descriptor,
                                 const char* name) override
        {
            VerifyNameAndConnections(layer, name);
            VerifyDescriptor(descriptor);
        }

    private:
        void VerifyDescriptor(const armnn::Pooling2dDescriptor& descriptor)
        {
            BOOST_TEST(GetDataLayoutName(descriptor.m_DataLayout) == GetDataLayoutName(m_Descriptor.m_DataLayout));
            BOOST_TEST(descriptor.m_PadLeft == m_Descriptor.m_PadLeft);
            BOOST_TEST(descriptor.m_PadRight == m_Descriptor.m_PadRight);
            BOOST_TEST(descriptor.m_PadTop == m_Descriptor.m_PadTop);
            BOOST_TEST(descriptor.m_PadBottom == m_Descriptor.m_PadBottom);
            BOOST_TEST(descriptor.m_PoolWidth == m_Descriptor.m_PoolWidth);
            BOOST_TEST(descriptor.m_PoolHeight == m_Descriptor.m_PoolHeight);
            BOOST_TEST(descriptor.m_StrideX == m_Descriptor.m_StrideX);
            BOOST_TEST(descriptor.m_StrideY == m_Descriptor.m_StrideY);

            BOOST_TEST(
                static_cast<int>(descriptor.m_PaddingMethod) == static_cast<int>(m_Descriptor.m_PaddingMethod));
            BOOST_TEST(
                static_cast<int>(descriptor.m_PoolType) == static_cast<int>(m_Descriptor.m_PoolType));
            BOOST_TEST(
                static_cast<int>(descriptor.m_OutputShapeRounding) ==
                static_cast<int>(m_Descriptor.m_OutputShapeRounding));
        }

        armnn::Pooling2dDescriptor m_Descriptor;
    };

    const std::string layerName("pooling2d");
    const armnn::TensorInfo inputInfo({1, 2, 2, 1}, armnn::DataType::Float32);
    const armnn::TensorInfo outputInfo({1, 1, 1, 1}, armnn::DataType::Float32);

    armnn::Pooling2dDescriptor desc;
    desc.m_DataLayout          = armnn::DataLayout::NHWC;
    desc.m_PadTop              = 0;
    desc.m_PadBottom           = 0;
    desc.m_PadLeft             = 0;
    desc.m_PadRight            = 0;
    desc.m_PoolType            = armnn::PoolingAlgorithm::Average;
    desc.m_OutputShapeRounding = armnn::OutputShapeRounding::Floor;
    desc.m_PaddingMethod       = armnn::PaddingMethod::Exclude;
    desc.m_PoolHeight          = 2;
    desc.m_PoolWidth           = 2;
    desc.m_StrideX             = 2;
    desc.m_StrideY             = 2;

    armnn::INetworkPtr network = armnn::INetwork::Create();
    armnn::IConnectableLayer* const inputLayer = network->AddInputLayer(0);
    armnn::IConnectableLayer* const pooling2dLayer = network->AddPooling2dLayer(desc, layerName.c_str());
    armnn::IConnectableLayer* const outputLayer = network->AddOutputLayer(0);

    inputLayer->GetOutputSlot(0).Connect(pooling2dLayer->GetInputSlot(0));
    pooling2dLayer->GetOutputSlot(0).Connect(outputLayer->GetInputSlot(0));

    inputLayer->GetOutputSlot(0).SetTensorInfo(inputInfo);
    pooling2dLayer->GetOutputSlot(0).SetTensorInfo(outputInfo);

    armnn::INetworkPtr deserializedNetwork = DeserializeNetwork(SerializeNetwork(*network));
    BOOST_CHECK(deserializedNetwork);

    Pooling2dLayerVerifier verifier(layerName, {inputInfo}, {outputInfo}, desc);
    deserializedNetwork->Accept(verifier);
}

BOOST_AUTO_TEST_CASE(SerializeQuantize)
{
    class QuantizeLayerVerifier : public LayerVerifierBase
    {
    public:
        QuantizeLayerVerifier(const std::string& layerName,
                             const std::vector<armnn::TensorInfo>& inputInfos,
                             const std::vector<armnn::TensorInfo>& outputInfos)
            : LayerVerifierBase(layerName, inputInfos, outputInfos) {}

        void VisitQuantizeLayer(const armnn::IConnectableLayer* layer, const char* name) override
        {
            VerifyNameAndConnections(layer, name);
        }
    };

    const std::string layerName("quantize");
    const armnn::TensorInfo info({ 1, 2, 2, 3 }, armnn::DataType::Float32);

    armnn::INetworkPtr network = armnn::INetwork::Create();
    armnn::IConnectableLayer* const inputLayer = network->AddInputLayer(0);
    armnn::IConnectableLayer* const quantizeLayer = network->AddQuantizeLayer(layerName.c_str());
    armnn::IConnectableLayer* const outputLayer = network->AddOutputLayer(0);

    inputLayer->GetOutputSlot(0).Connect(quantizeLayer->GetInputSlot(0));
    quantizeLayer->GetOutputSlot(0).Connect(outputLayer->GetInputSlot(0));

    inputLayer->GetOutputSlot(0).SetTensorInfo(info);
    quantizeLayer->GetOutputSlot(0).SetTensorInfo(info);

    armnn::INetworkPtr deserializedNetwork = DeserializeNetwork(SerializeNetwork(*network));
    BOOST_CHECK(deserializedNetwork);

    QuantizeLayerVerifier verifier(layerName, {info}, {info});
    deserializedNetwork->Accept(verifier);
}
BOOST_AUTO_TEST_CASE(SerializeReshape)
{
    class ReshapeLayerVerifier : public LayerVerifierBase
    {
    public:
        ReshapeLayerVerifier(const std::string& layerName,
                             const std::vector<armnn::TensorInfo>& inputInfos,
                             const std::vector<armnn::TensorInfo>& outputInfos,
                             const armnn::ReshapeDescriptor& descriptor)
        : LayerVerifierBase(layerName, inputInfos, outputInfos)
        , m_Descriptor(descriptor) {}

        void VisitReshapeLayer(const armnn::IConnectableLayer* layer,
                               const armnn::ReshapeDescriptor& descriptor,
                               const char* name) override
        {
            VerifyNameAndConnections(layer, name);
            VerifyDescriptor(descriptor);
        }

    private:
        void VerifyDescriptor(const armnn::ReshapeDescriptor& descriptor)
        {
            BOOST_TEST(descriptor.m_TargetShape == m_Descriptor.m_TargetShape);
        }

        armnn::ReshapeDescriptor m_Descriptor;
    };

    const std::string layerName("reshape");
    const armnn::TensorInfo inputInfo({1, 9}, armnn::DataType::Float32);
    const armnn::TensorInfo outputInfo({3, 3}, armnn::DataType::Float32);

    armnn::ReshapeDescriptor descriptor({3, 3});

    armnn::INetworkPtr network = armnn::INetwork::Create();
    armnn::IConnectableLayer* const inputLayer = network->AddInputLayer(0);
    armnn::IConnectableLayer* const reshapeLayer = network->AddReshapeLayer(descriptor, layerName.c_str());
    armnn::IConnectableLayer* const outputLayer = network->AddOutputLayer(0);

    inputLayer->GetOutputSlot(0).Connect(reshapeLayer->GetInputSlot(0));
    reshapeLayer->GetOutputSlot(0).Connect(outputLayer->GetInputSlot(0));

    inputLayer->GetOutputSlot(0).SetTensorInfo(inputInfo);
    reshapeLayer->GetOutputSlot(0).SetTensorInfo(outputInfo);

    armnn::INetworkPtr deserializedNetwork = DeserializeNetwork(SerializeNetwork(*network));
    BOOST_CHECK(deserializedNetwork);

    ReshapeLayerVerifier verifier(layerName, {inputInfo}, {outputInfo}, descriptor);
    deserializedNetwork->Accept(verifier);
}

BOOST_AUTO_TEST_CASE(SerializeResizeBilinear)
{
    class ResizeBilinearLayerVerifier : public LayerVerifierBase
    {
    public:
        ResizeBilinearLayerVerifier(const std::string& layerName,
                                    const std::vector<armnn::TensorInfo>& inputInfos,
                                    const std::vector<armnn::TensorInfo>& outputInfos,
                                    const armnn::ResizeBilinearDescriptor& descriptor)
        : LayerVerifierBase(layerName, inputInfos, outputInfos)
        , m_Descriptor(descriptor) {}

        void VisitResizeBilinearLayer(const armnn::IConnectableLayer* layer,
                                      const armnn::ResizeBilinearDescriptor& descriptor,
                                      const char* name) override
        {
            VerifyNameAndConnections(layer, name);
            VerifyDescriptor(descriptor);
        }

    private:
        void VerifyDescriptor(const armnn::ResizeBilinearDescriptor& descriptor)
        {
            BOOST_TEST(GetDataLayoutName(descriptor.m_DataLayout) == GetDataLayoutName(m_Descriptor.m_DataLayout));
            BOOST_TEST(descriptor.m_TargetWidth == m_Descriptor.m_TargetWidth);
            BOOST_TEST(descriptor.m_TargetHeight == m_Descriptor.m_TargetHeight);
        }

        armnn::ResizeBilinearDescriptor m_Descriptor;
    };

    const std::string layerName("resizeBilinear");
    const armnn::TensorInfo inputInfo = armnn::TensorInfo({1, 3, 5, 5}, armnn::DataType::Float32);
    const armnn::TensorInfo outputInfo = armnn::TensorInfo({1, 3, 2, 4}, armnn::DataType::Float32);

    armnn::ResizeBilinearDescriptor desc;
    desc.m_TargetWidth = 4;
    desc.m_TargetHeight = 2;

    armnn::INetworkPtr network = armnn::INetwork::Create();
    armnn::IConnectableLayer* const inputLayer = network->AddInputLayer(0);
    armnn::IConnectableLayer* const resizeLayer = network->AddResizeBilinearLayer(desc, layerName.c_str());
    armnn::IConnectableLayer* const outputLayer = network->AddOutputLayer(0);

    inputLayer->GetOutputSlot(0).Connect(resizeLayer->GetInputSlot(0));
    resizeLayer->GetOutputSlot(0).Connect(outputLayer->GetInputSlot(0));

    inputLayer->GetOutputSlot(0).SetTensorInfo(inputInfo);
    resizeLayer->GetOutputSlot(0).SetTensorInfo(outputInfo);

    armnn::INetworkPtr deserializedNetwork = DeserializeNetwork(SerializeNetwork(*network));
    BOOST_CHECK(deserializedNetwork);

    ResizeBilinearLayerVerifier verifier(layerName, {inputInfo}, {outputInfo}, desc);
    deserializedNetwork->Accept(verifier);
}

BOOST_AUTO_TEST_CASE(SerializeRsqrt)
{
    class RsqrtLayerVerifier : public LayerVerifierBase
    {
    public:
        RsqrtLayerVerifier(const std::string& layerName,
                           const std::vector<armnn::TensorInfo>& inputInfos,
                           const std::vector<armnn::TensorInfo>& outputInfos)
        : LayerVerifierBase(layerName, inputInfos, outputInfos) {}

        void VisitRsqrtLayer(const armnn::IConnectableLayer* layer, const char* name) override
        {
            VerifyNameAndConnections(layer, name);
        }
    };

    const std::string layerName("rsqrt");
    const armnn::TensorInfo tensorInfo({ 3, 1, 2 }, armnn::DataType::Float32);

    armnn::INetworkPtr network = armnn::INetwork::Create();
    armnn::IConnectableLayer* const inputLayer  = network->AddInputLayer(0);
    armnn::IConnectableLayer* const rsqrtLayer  = network->AddRsqrtLayer(layerName.c_str());
    armnn::IConnectableLayer* const outputLayer = network->AddOutputLayer(0);

    inputLayer->GetOutputSlot(0).Connect(rsqrtLayer->GetInputSlot(0));
    rsqrtLayer->GetOutputSlot(0).Connect(outputLayer->GetInputSlot(0));

    inputLayer->GetOutputSlot(0).SetTensorInfo(tensorInfo);
    rsqrtLayer->GetOutputSlot(0).SetTensorInfo(tensorInfo);

    armnn::INetworkPtr deserializedNetwork = DeserializeNetwork(SerializeNetwork(*network));
    BOOST_CHECK(deserializedNetwork);

    RsqrtLayerVerifier verifier(layerName, {tensorInfo}, {tensorInfo});
    deserializedNetwork->Accept(verifier);
}

BOOST_AUTO_TEST_CASE(SerializeSoftmax)
{
    class SoftmaxLayerVerifier : public LayerVerifierBase
    {
    public:
        SoftmaxLayerVerifier(const std::string& layerName,
                             const std::vector<armnn::TensorInfo>& inputInfos,
                             const std::vector<armnn::TensorInfo>& outputInfos,
                             const armnn::SoftmaxDescriptor& descriptor)
        : LayerVerifierBase(layerName, inputInfos, outputInfos)
        , m_Descriptor(descriptor) {}

        void VisitSoftmaxLayer(const armnn::IConnectableLayer* layer,
                               const armnn::SoftmaxDescriptor& descriptor,
                               const char* name) override
        {
            VerifyNameAndConnections(layer, name);
            VerifyDescriptor(descriptor);
        }

    private:
        void VerifyDescriptor(const armnn::SoftmaxDescriptor& descriptor)
        {
            BOOST_TEST(descriptor.m_Beta == m_Descriptor.m_Beta);
        }

        armnn::SoftmaxDescriptor m_Descriptor;
    };

    const std::string layerName("softmax");
    const armnn::TensorInfo info({1, 10}, armnn::DataType::Float32);

    armnn::SoftmaxDescriptor descriptor;
    descriptor.m_Beta = 1.0f;

    armnn::INetworkPtr network = armnn::INetwork::Create();
    armnn::IConnectableLayer* const inputLayer   = network->AddInputLayer(0);
    armnn::IConnectableLayer* const softmaxLayer = network->AddSoftmaxLayer(descriptor, layerName.c_str());
    armnn::IConnectableLayer* const outputLayer  = network->AddOutputLayer(0);

    inputLayer->GetOutputSlot(0).Connect(softmaxLayer->GetInputSlot(0));
    softmaxLayer->GetOutputSlot(0).Connect(outputLayer->GetInputSlot(0));

    inputLayer->GetOutputSlot(0).SetTensorInfo(info);
    softmaxLayer->GetOutputSlot(0).SetTensorInfo(info);

    armnn::INetworkPtr deserializedNetwork = DeserializeNetwork(SerializeNetwork(*network));
    BOOST_CHECK(deserializedNetwork);

    SoftmaxLayerVerifier verifier(layerName, {info}, {info}, descriptor);
    deserializedNetwork->Accept(verifier);
}

BOOST_AUTO_TEST_CASE(SerializeSpaceToBatchNd)
{
    class SpaceToBatchNdLayerVerifier : public LayerVerifierBase
    {
    public:
        SpaceToBatchNdLayerVerifier(const std::string& layerName,
                                    const std::vector<armnn::TensorInfo>& inputInfos,
                                    const std::vector<armnn::TensorInfo>& outputInfos,
                                    const armnn::SpaceToBatchNdDescriptor& descriptor)
        : LayerVerifierBase(layerName, inputInfos, outputInfos)
        , m_Descriptor(descriptor) {}

        void VisitSpaceToBatchNdLayer(const armnn::IConnectableLayer* layer,
                                      const armnn::SpaceToBatchNdDescriptor& descriptor,
                                      const char* name) override
        {
            VerifyNameAndConnections(layer, name);
            VerifyDescriptor(descriptor);
        }

    private:
        void VerifyDescriptor(const armnn::SpaceToBatchNdDescriptor& descriptor)
        {
            BOOST_TEST(descriptor.m_PadList == m_Descriptor.m_PadList);
            BOOST_TEST(descriptor.m_BlockShape == m_Descriptor.m_BlockShape);
            BOOST_TEST(GetDataLayoutName(descriptor.m_DataLayout) == GetDataLayoutName(m_Descriptor.m_DataLayout));
        }

        armnn::SpaceToBatchNdDescriptor m_Descriptor;
    };

    const std::string layerName("spaceToBatchNd");
    const armnn::TensorInfo inputInfo({2, 1, 2, 4}, armnn::DataType::Float32);
    const armnn::TensorInfo outputInfo({8, 1, 1, 3}, armnn::DataType::Float32);

    armnn::SpaceToBatchNdDescriptor desc;
    desc.m_DataLayout = armnn::DataLayout::NCHW;
    desc.m_BlockShape = {2, 2};
    desc.m_PadList = {{0, 0}, {2, 0}};

    armnn::INetworkPtr network = armnn::INetwork::Create();
    armnn::IConnectableLayer* const inputLayer = network->AddInputLayer(0);
    armnn::IConnectableLayer* const spaceToBatchNdLayer = network->AddSpaceToBatchNdLayer(desc, layerName.c_str());
    armnn::IConnectableLayer* const outputLayer = network->AddOutputLayer(0);

    inputLayer->GetOutputSlot(0).Connect(spaceToBatchNdLayer->GetInputSlot(0));
    spaceToBatchNdLayer->GetOutputSlot(0).Connect(outputLayer->GetInputSlot(0));

    inputLayer->GetOutputSlot(0).SetTensorInfo(inputInfo);
    spaceToBatchNdLayer->GetOutputSlot(0).SetTensorInfo(outputInfo);

    armnn::INetworkPtr deserializedNetwork = DeserializeNetwork(SerializeNetwork(*network));
    BOOST_CHECK(deserializedNetwork);

    SpaceToBatchNdLayerVerifier verifier(layerName, {inputInfo}, {outputInfo}, desc);
    deserializedNetwork->Accept(verifier);
}

BOOST_AUTO_TEST_CASE(SerializeSplitter)
{
    class SplitterLayerVerifier : public LayerVerifierBase
    {
    public:
        SplitterLayerVerifier(const std::string& layerName,
                              const std::vector<armnn::TensorInfo>& inputInfos,
                              const std::vector<armnn::TensorInfo>& outputInfos,
                              const armnn::ViewsDescriptor& descriptor)
        : LayerVerifierBase(layerName, inputInfos, outputInfos)
        , m_Descriptor(descriptor) {}

        void VisitSplitterLayer(const armnn::IConnectableLayer* layer,
                                const armnn::ViewsDescriptor& descriptor,
                                const char* name) override
        {
            VerifyNameAndConnections(layer, name);
            VerifyDescriptor(descriptor);
        }

    private:
        void VerifyDescriptor(const armnn::ViewsDescriptor& descriptor)
        {
            BOOST_TEST(descriptor.GetNumViews() == m_Descriptor.GetNumViews());
            BOOST_TEST(descriptor.GetNumDimensions() == m_Descriptor.GetNumDimensions());

            for (uint32_t i = 0; i < descriptor.GetNumViews(); i++)
            {
                for (uint32_t j = 0; j < descriptor.GetNumDimensions(); j++)
                {
                    BOOST_TEST(descriptor.GetViewOrigin(i)[j] == m_Descriptor.GetViewOrigin(i)[j]);
                    BOOST_TEST(descriptor.GetViewSizes(i)[j] == m_Descriptor.GetViewSizes(i)[j]);
                }
            }
        }

        armnn::ViewsDescriptor m_Descriptor;
    };

    const unsigned int numViews = 3;
    const unsigned int numDimensions = 4;
    const unsigned int inputShape[] = {1, 18, 4, 4};
    const unsigned int outputShape[] = {1, 6, 4, 4};

    // This is modelled on how the caffe parser sets up a splitter layer to partition an input along dimension one.
    unsigned int splitterDimSizes[4] = {static_cast<unsigned int>(inputShape[0]),
                                        static_cast<unsigned int>(inputShape[1]),
                                        static_cast<unsigned int>(inputShape[2]),
                                        static_cast<unsigned int>(inputShape[3])};
    splitterDimSizes[1] /= numViews;
    armnn::ViewsDescriptor desc(numViews, numDimensions);

    for (unsigned int g = 0; g < numViews; ++g)
    {
        desc.SetViewOriginCoord(g, 1, splitterDimSizes[1] * g);

        for (unsigned int dimIdx=0; dimIdx < 4; dimIdx++)
        {
            desc.SetViewSize(g, dimIdx, splitterDimSizes[dimIdx]);
        }
    }

    const std::string layerName("splitter");
    const armnn::TensorInfo inputInfo(numDimensions, inputShape, armnn::DataType::Float32);
    const armnn::TensorInfo outputInfo(numDimensions, outputShape, armnn::DataType::Float32);

    armnn::INetworkPtr network = armnn::INetwork::Create();
    armnn::IConnectableLayer* const inputLayer = network->AddInputLayer(0);
    armnn::IConnectableLayer* const splitterLayer = network->AddSplitterLayer(desc, layerName.c_str());
    armnn::IConnectableLayer* const outputLayer0 = network->AddOutputLayer(0);
    armnn::IConnectableLayer* const outputLayer1 = network->AddOutputLayer(1);
    armnn::IConnectableLayer* const outputLayer2 = network->AddOutputLayer(2);

    inputLayer->GetOutputSlot(0).Connect(splitterLayer->GetInputSlot(0));
    splitterLayer->GetOutputSlot(0).Connect(outputLayer0->GetInputSlot(0));
    splitterLayer->GetOutputSlot(1).Connect(outputLayer1->GetInputSlot(0));
    splitterLayer->GetOutputSlot(2).Connect(outputLayer2->GetInputSlot(0));

    inputLayer->GetOutputSlot(0).SetTensorInfo(inputInfo);
    splitterLayer->GetOutputSlot(0).SetTensorInfo(outputInfo);
    splitterLayer->GetOutputSlot(1).SetTensorInfo(outputInfo);
    splitterLayer->GetOutputSlot(2).SetTensorInfo(outputInfo);

    armnn::INetworkPtr deserializedNetwork = DeserializeNetwork(SerializeNetwork(*network));
    BOOST_CHECK(deserializedNetwork);

    SplitterLayerVerifier verifier(layerName, {inputInfo}, {outputInfo, outputInfo, outputInfo}, desc);
    deserializedNetwork->Accept(verifier);
}

BOOST_AUTO_TEST_CASE(SerializeStridedSlice)
{
    class StridedSliceLayerVerifier : public LayerVerifierBase
    {
    public:
        StridedSliceLayerVerifier(const std::string& layerName,
                                  const std::vector<armnn::TensorInfo>& inputInfos,
                                  const std::vector<armnn::TensorInfo>& outputInfos,
                                  const armnn::StridedSliceDescriptor& descriptor)
        : LayerVerifierBase(layerName, inputInfos, outputInfos)
        , m_Descriptor(descriptor) {}

        void VisitStridedSliceLayer(const armnn::IConnectableLayer* layer,
                                    const armnn::StridedSliceDescriptor& descriptor,
                                    const char* name) override
        {
            VerifyNameAndConnections(layer, name);
            VerifyDescriptor(descriptor);
        }

    private:
        void VerifyDescriptor(const armnn::StridedSliceDescriptor& descriptor)
        {
            BOOST_TEST(descriptor.m_Begin == m_Descriptor.m_Begin);
            BOOST_TEST(descriptor.m_End == m_Descriptor.m_End);
            BOOST_TEST(descriptor.m_Stride == m_Descriptor.m_Stride);
            BOOST_TEST(descriptor.m_BeginMask == m_Descriptor.m_BeginMask);
            BOOST_TEST(descriptor.m_EndMask == m_Descriptor.m_EndMask);
            BOOST_TEST(descriptor.m_ShrinkAxisMask == m_Descriptor.m_ShrinkAxisMask);
            BOOST_TEST(descriptor.m_EllipsisMask == m_Descriptor.m_EllipsisMask);
            BOOST_TEST(descriptor.m_NewAxisMask == m_Descriptor.m_NewAxisMask);
            BOOST_TEST(GetDataLayoutName(descriptor.m_DataLayout) == GetDataLayoutName(m_Descriptor.m_DataLayout));
        }
        armnn::StridedSliceDescriptor m_Descriptor;
    };

    const std::string layerName("stridedSlice");
    const armnn::TensorInfo inputInfo = armnn::TensorInfo({3, 2, 3, 1}, armnn::DataType::Float32);
    const armnn::TensorInfo outputInfo = armnn::TensorInfo({3, 1}, armnn::DataType::Float32);

    armnn::StridedSliceDescriptor desc({0, 0, 1, 0}, {1, 1, 1, 1}, {1, 1, 1, 1});
    desc.m_EndMask = (1 << 4) - 1;
    desc.m_ShrinkAxisMask = (1 << 1) | (1 << 2);
    desc.m_DataLayout = armnn::DataLayout::NCHW;

    armnn::INetworkPtr network = armnn::INetwork::Create();
    armnn::IConnectableLayer* const inputLayer = network->AddInputLayer(0);
    armnn::IConnectableLayer* const stridedSliceLayer = network->AddStridedSliceLayer(desc, layerName.c_str());
    armnn::IConnectableLayer* const outputLayer = network->AddOutputLayer(0);

    inputLayer->GetOutputSlot(0).Connect(stridedSliceLayer->GetInputSlot(0));
    stridedSliceLayer->GetOutputSlot(0).Connect(outputLayer->GetInputSlot(0));

    inputLayer->GetOutputSlot(0).SetTensorInfo(inputInfo);
    stridedSliceLayer->GetOutputSlot(0).SetTensorInfo(outputInfo);

    armnn::INetworkPtr deserializedNetwork = DeserializeNetwork(SerializeNetwork(*network));
    BOOST_CHECK(deserializedNetwork);

    StridedSliceLayerVerifier verifier(layerName, {inputInfo}, {outputInfo}, desc);
    deserializedNetwork->Accept(verifier);
}

BOOST_AUTO_TEST_CASE(SerializeSubtraction)
{
    class SubtractionLayerVerifier : public LayerVerifierBase
    {
    public:
        SubtractionLayerVerifier(const std::string& layerName,
                                 const std::vector<armnn::TensorInfo>& inputInfos,
                                 const std::vector<armnn::TensorInfo>& outputInfos)
        : LayerVerifierBase(layerName, inputInfos, outputInfos) {}

        void VisitSubtractionLayer(const armnn::IConnectableLayer* layer, const char* name) override
        {
            VerifyNameAndConnections(layer, name);
        }
    };

    const std::string layerName("subtraction");
    const armnn::TensorInfo info({ 1, 4 }, armnn::DataType::Float32);

    armnn::INetworkPtr network = armnn::INetwork::Create();
    armnn::IConnectableLayer* const inputLayer0 = network->AddInputLayer(0);
    armnn::IConnectableLayer* const inputLayer1 = network->AddInputLayer(1);
    armnn::IConnectableLayer* const subtractionLayer = network->AddSubtractionLayer(layerName.c_str());
    armnn::IConnectableLayer* const outputLayer = network->AddOutputLayer(0);

    inputLayer0->GetOutputSlot(0).Connect(subtractionLayer->GetInputSlot(0));
    inputLayer1->GetOutputSlot(0).Connect(subtractionLayer->GetInputSlot(1));
    subtractionLayer->GetOutputSlot(0).Connect(outputLayer->GetInputSlot(0));

    inputLayer0->GetOutputSlot(0).SetTensorInfo(info);
    inputLayer1->GetOutputSlot(0).SetTensorInfo(info);
    subtractionLayer->GetOutputSlot(0).SetTensorInfo(info);

    armnn::INetworkPtr deserializedNetwork = DeserializeNetwork(SerializeNetwork(*network));
    BOOST_CHECK(deserializedNetwork);

    SubtractionLayerVerifier verifier(layerName, {info, info}, {info});
    deserializedNetwork->Accept(verifier);
}

BOOST_AUTO_TEST_CASE(SerializeSwitch)
{
    class SwitchLayerVerifier : public LayerVerifierBase
    {
    public:
        SwitchLayerVerifier(const std::string& layerName,
                                 const std::vector<armnn::TensorInfo>& inputInfos,
                                 const std::vector<armnn::TensorInfo>& outputInfos)
            : LayerVerifierBase(layerName, inputInfos, outputInfos) {}

        void VisitSwitchLayer(const armnn::IConnectableLayer* layer, const char* name) override
        {
            VerifyNameAndConnections(layer, name);
        }

        void VisitConstantLayer(const armnn::IConnectableLayer* layer,
                                const armnn::ConstTensor& input,
                                const char *name) override {}
    };

    const std::string layerName("switch");
    const armnn::TensorInfo info({ 1, 4 }, armnn::DataType::Float32);

    std::vector<float> constantData = GenerateRandomData<float>(info.GetNumElements());
    armnn::ConstTensor constTensor(info, constantData);

    armnn::INetworkPtr network = armnn::INetwork::Create();
    armnn::IConnectableLayer* const inputLayer = network->AddInputLayer(0);
    armnn::IConnectableLayer* const constantLayer = network->AddConstantLayer(constTensor, "constant");
    armnn::IConnectableLayer* const switchLayer = network->AddSwitchLayer(layerName.c_str());
    armnn::IConnectableLayer* const trueOutputLayer = network->AddOutputLayer(0);
    armnn::IConnectableLayer* const falseOutputLayer = network->AddOutputLayer(1);

    inputLayer->GetOutputSlot(0).Connect(switchLayer->GetInputSlot(0));
    constantLayer->GetOutputSlot(0).Connect(switchLayer->GetInputSlot(1));
    switchLayer->GetOutputSlot(0).Connect(trueOutputLayer->GetInputSlot(0));
    switchLayer->GetOutputSlot(1).Connect(falseOutputLayer->GetInputSlot(0));

    inputLayer->GetOutputSlot(0).SetTensorInfo(info);
    constantLayer->GetOutputSlot(0).SetTensorInfo(info);
    switchLayer->GetOutputSlot(0).SetTensorInfo(info);
    switchLayer->GetOutputSlot(1).SetTensorInfo(info);

    armnn::INetworkPtr deserializedNetwork = DeserializeNetwork(SerializeNetwork(*network));
    BOOST_CHECK(deserializedNetwork);

    SwitchLayerVerifier verifier(layerName, {info, info}, {info, info});
    deserializedNetwork->Accept(verifier);
}

BOOST_AUTO_TEST_CASE(SerializeDeserializeNonLinearNetwork)
{
    class ConstantLayerVerifier : public LayerVerifierBase
    {
    public:
        ConstantLayerVerifier(const std::string& layerName,
                              const std::vector<armnn::TensorInfo>& inputInfos,
                              const std::vector<armnn::TensorInfo>& outputInfos,
                              const armnn::ConstTensor& layerInput)
            : LayerVerifierBase(layerName, inputInfos, outputInfos)
            , m_LayerInput(layerInput) {}

        void VisitConstantLayer(const armnn::IConnectableLayer* layer,
                                const armnn::ConstTensor& input,
                                const char* name) override
        {
            VerifyNameAndConnections(layer, name);

            CompareConstTensor(input, m_LayerInput);
        }

        void VisitAdditionLayer(const armnn::IConnectableLayer* layer, const char* name = nullptr) override {}

    private:
        armnn::ConstTensor m_LayerInput;
    };

    const std::string layerName("constant");
    const armnn::TensorInfo info({ 2, 3 }, armnn::DataType::Float32);

    std::vector<float> constantData = GenerateRandomData<float>(info.GetNumElements());
    armnn::ConstTensor constTensor(info, constantData);

    armnn::INetworkPtr network(armnn::INetwork::Create());
    armnn::IConnectableLayer* input = network->AddInputLayer(0);
    armnn::IConnectableLayer* add = network->AddAdditionLayer();
    armnn::IConnectableLayer* constant = network->AddConstantLayer(constTensor, layerName.c_str());
    armnn::IConnectableLayer* output = network->AddOutputLayer(0);

    input->GetOutputSlot(0).Connect(add->GetInputSlot(0));
    constant->GetOutputSlot(0).Connect(add->GetInputSlot(1));
    add->GetOutputSlot(0).Connect(output->GetInputSlot(0));

    input->GetOutputSlot(0).SetTensorInfo(info);
    constant->GetOutputSlot(0).SetTensorInfo(info);
    add->GetOutputSlot(0).SetTensorInfo(info);

    armnn::INetworkPtr deserializedNetwork = DeserializeNetwork(SerializeNetwork(*network));
    BOOST_CHECK(deserializedNetwork);

    ConstantLayerVerifier verifier(layerName, {}, {info}, constTensor);
    deserializedNetwork->Accept(verifier);
}

class VerifyLstmLayer : public LayerVerifierBase
{
public:
    VerifyLstmLayer(const std::string& layerName,
                    const std::vector<armnn::TensorInfo>& inputInfos,
                    const std::vector<armnn::TensorInfo>& outputInfos,
                    const armnn::LstmDescriptor& descriptor,
                    const armnn::LstmInputParams& inputParams) :
         LayerVerifierBase(layerName, inputInfos, outputInfos), m_Descriptor(descriptor), m_InputParams(inputParams)
    {
    }
    void VisitLstmLayer(const armnn::IConnectableLayer* layer,
                        const armnn::LstmDescriptor& descriptor,
                        const armnn::LstmInputParams& params,
                        const char* name)
    {
        VerifyNameAndConnections(layer, name);
        VerifyDescriptor(descriptor);
        VerifyInputParameters(params);
    }
protected:
    void VerifyDescriptor(const armnn::LstmDescriptor& descriptor)
    {
        BOOST_TEST(m_Descriptor.m_ActivationFunc == descriptor.m_ActivationFunc);
        BOOST_TEST(m_Descriptor.m_ClippingThresCell == descriptor.m_ClippingThresCell);
        BOOST_TEST(m_Descriptor.m_ClippingThresProj == descriptor.m_ClippingThresProj);
        BOOST_TEST(m_Descriptor.m_CifgEnabled == descriptor.m_CifgEnabled);
        BOOST_TEST(m_Descriptor.m_PeepholeEnabled = descriptor.m_PeepholeEnabled);
        BOOST_TEST(m_Descriptor.m_ProjectionEnabled == descriptor.m_ProjectionEnabled);
    }
    void VerifyInputParameters(const armnn::LstmInputParams& params)
    {
        VerifyConstTensors(
            "m_InputToInputWeights", m_InputParams.m_InputToInputWeights, params.m_InputToInputWeights);
        VerifyConstTensors(
            "m_InputToForgetWeights", m_InputParams.m_InputToForgetWeights, params.m_InputToForgetWeights);
        VerifyConstTensors(
            "m_InputToCellWeights", m_InputParams.m_InputToCellWeights, params.m_InputToCellWeights);
        VerifyConstTensors(
            "m_InputToOutputWeights", m_InputParams.m_InputToOutputWeights, params.m_InputToOutputWeights);
        VerifyConstTensors(
            "m_RecurrentToInputWeights", m_InputParams.m_RecurrentToInputWeights, params.m_RecurrentToInputWeights);
        VerifyConstTensors(
            "m_RecurrentToForgetWeights", m_InputParams.m_RecurrentToForgetWeights, params.m_RecurrentToForgetWeights);
        VerifyConstTensors(
            "m_RecurrentToCellWeights", m_InputParams.m_RecurrentToCellWeights, params.m_RecurrentToCellWeights);
        VerifyConstTensors(
            "m_RecurrentToOutputWeights", m_InputParams.m_RecurrentToOutputWeights, params.m_RecurrentToOutputWeights);
        VerifyConstTensors(
            "m_CellToInputWeights", m_InputParams.m_CellToInputWeights, params.m_CellToInputWeights);
        VerifyConstTensors(
            "m_CellToForgetWeights", m_InputParams.m_CellToForgetWeights, params.m_CellToForgetWeights);
        VerifyConstTensors(
            "m_CellToOutputWeights", m_InputParams.m_CellToOutputWeights, params.m_CellToOutputWeights);
        VerifyConstTensors(
            "m_InputGateBias", m_InputParams.m_InputGateBias, params.m_InputGateBias);
        VerifyConstTensors(
            "m_ForgetGateBias", m_InputParams.m_ForgetGateBias, params.m_ForgetGateBias);
        VerifyConstTensors(
            "m_CellBias", m_InputParams.m_CellBias, params.m_CellBias);
        VerifyConstTensors(
            "m_OutputGateBias", m_InputParams.m_OutputGateBias, params.m_OutputGateBias);
        VerifyConstTensors(
            "m_ProjectionWeights", m_InputParams.m_ProjectionWeights, params.m_ProjectionWeights);
        VerifyConstTensors(
            "m_ProjectionBias", m_InputParams.m_ProjectionBias, params.m_ProjectionBias);
    }
    void VerifyConstTensors(const std::string& tensorName,
                            const armnn::ConstTensor* expectedPtr,
                            const armnn::ConstTensor* actualPtr)
    {
        if (expectedPtr == nullptr)
        {
            BOOST_CHECK_MESSAGE(actualPtr == nullptr, tensorName + " should not exist");
        }
        else
        {
            BOOST_CHECK_MESSAGE(actualPtr != nullptr, tensorName + " should have been set");
            if (actualPtr != nullptr)
            {
                const armnn::TensorInfo& expectedInfo = expectedPtr->GetInfo();
                const armnn::TensorInfo& actualInfo = actualPtr->GetInfo();

                BOOST_CHECK_MESSAGE(expectedInfo.GetShape() == actualInfo.GetShape(),
                                    tensorName + " shapes don't match");
                BOOST_CHECK_MESSAGE(
                    GetDataTypeName(expectedInfo.GetDataType()) == GetDataTypeName(actualInfo.GetDataType()),
                    tensorName + " data types don't match");

                BOOST_CHECK_MESSAGE(expectedPtr->GetNumBytes() == actualPtr->GetNumBytes(),
                                    tensorName + " (GetNumBytes) data sizes do not match");
                if (expectedPtr->GetNumBytes() == actualPtr->GetNumBytes())
                {
                    //check the data is identical
                    const char* expectedData = static_cast<const char*>(expectedPtr->GetMemoryArea());
                    const char* actualData = static_cast<const char*>(actualPtr->GetMemoryArea());
                    bool same = true;
                    for (unsigned int i = 0; i < expectedPtr->GetNumBytes(); ++i)
                    {
                        same = expectedData[i] == actualData[i];
                        if (!same)
                        {
                            break;
                        }
                    }
                    BOOST_CHECK_MESSAGE(same, tensorName + " data does not match");
                }
            }
        }
    }
private:
    armnn::LstmDescriptor m_Descriptor;
    armnn::LstmInputParams m_InputParams;
};

BOOST_AUTO_TEST_CASE(SerializeDeserializeLstmCifgPeepholeNoProjection)
{
    armnn::LstmDescriptor descriptor;
    descriptor.m_ActivationFunc = 4;
    descriptor.m_ClippingThresProj = 0.0f;
    descriptor.m_ClippingThresCell = 0.0f;
    descriptor.m_CifgEnabled = true; // if this is true then we DON'T need to set the OptCifgParams
    descriptor.m_ProjectionEnabled = false;
    descriptor.m_PeepholeEnabled = true;

    const uint32_t batchSize = 1;
    const uint32_t inputSize = 2;
    const uint32_t numUnits = 4;
    const uint32_t outputSize = numUnits;

    armnn::TensorInfo inputWeightsInfo1({numUnits, inputSize}, armnn::DataType::Float32);
    std::vector<float> inputToForgetWeightsData = GenerateRandomData<float>(inputWeightsInfo1.GetNumElements());
    armnn::ConstTensor inputToForgetWeights(inputWeightsInfo1, inputToForgetWeightsData);

    std::vector<float> inputToCellWeightsData = GenerateRandomData<float>(inputWeightsInfo1.GetNumElements());
    armnn::ConstTensor inputToCellWeights(inputWeightsInfo1, inputToCellWeightsData);

    std::vector<float> inputToOutputWeightsData = GenerateRandomData<float>(inputWeightsInfo1.GetNumElements());
    armnn::ConstTensor inputToOutputWeights(inputWeightsInfo1, inputToOutputWeightsData);

    armnn::TensorInfo inputWeightsInfo2({numUnits, outputSize}, armnn::DataType::Float32);
    std::vector<float> recurrentToForgetWeightsData = GenerateRandomData<float>(inputWeightsInfo2.GetNumElements());
    armnn::ConstTensor recurrentToForgetWeights(inputWeightsInfo2, recurrentToForgetWeightsData);

    std::vector<float> recurrentToCellWeightsData = GenerateRandomData<float>(inputWeightsInfo2.GetNumElements());
    armnn::ConstTensor recurrentToCellWeights(inputWeightsInfo2, recurrentToCellWeightsData);

    std::vector<float> recurrentToOutputWeightsData = GenerateRandomData<float>(inputWeightsInfo2.GetNumElements());
    armnn::ConstTensor recurrentToOutputWeights(inputWeightsInfo2, recurrentToOutputWeightsData);

    armnn::TensorInfo inputWeightsInfo3({numUnits}, armnn::DataType::Float32);
    std::vector<float> cellToForgetWeightsData = GenerateRandomData<float>(inputWeightsInfo3.GetNumElements());
    armnn::ConstTensor cellToForgetWeights(inputWeightsInfo3, cellToForgetWeightsData);

    std::vector<float> cellToOutputWeightsData = GenerateRandomData<float>(inputWeightsInfo3.GetNumElements());
    armnn::ConstTensor cellToOutputWeights(inputWeightsInfo3, cellToOutputWeightsData);

    std::vector<float> forgetGateBiasData(numUnits, 1.0f);
    armnn::ConstTensor forgetGateBias(inputWeightsInfo3, forgetGateBiasData);

    std::vector<float> cellBiasData(numUnits, 0.0f);
    armnn::ConstTensor cellBias(inputWeightsInfo3, cellBiasData);

    std::vector<float> outputGateBiasData(numUnits, 0.0f);
    armnn::ConstTensor outputGateBias(inputWeightsInfo3, outputGateBiasData);

    armnn::LstmInputParams params;
    params.m_InputToForgetWeights = &inputToForgetWeights;
    params.m_InputToCellWeights = &inputToCellWeights;
    params.m_InputToOutputWeights = &inputToOutputWeights;
    params.m_RecurrentToForgetWeights = &recurrentToForgetWeights;
    params.m_RecurrentToCellWeights = &recurrentToCellWeights;
    params.m_RecurrentToOutputWeights = &recurrentToOutputWeights;
    params.m_ForgetGateBias = &forgetGateBias;
    params.m_CellBias = &cellBias;
    params.m_OutputGateBias = &outputGateBias;
    params.m_CellToForgetWeights = &cellToForgetWeights;
    params.m_CellToOutputWeights = &cellToOutputWeights;

    armnn::INetworkPtr network = armnn::INetwork::Create();
    armnn::IConnectableLayer* const inputLayer   = network->AddInputLayer(0);
    armnn::IConnectableLayer* const cellStateIn = network->AddInputLayer(1);
    armnn::IConnectableLayer* const outputStateIn = network->AddInputLayer(2);
    const std::string layerName("lstm");
    armnn::IConnectableLayer* const lstmLayer = network->AddLstmLayer(descriptor, params, layerName.c_str());
    armnn::IConnectableLayer* const scratchBuffer  = network->AddOutputLayer(0);
    armnn::IConnectableLayer* const outputStateOut  = network->AddOutputLayer(1);
    armnn::IConnectableLayer* const cellStateOut  = network->AddOutputLayer(2);
    armnn::IConnectableLayer* const outputLayer  = network->AddOutputLayer(3);

    // connect up
    armnn::TensorInfo inputTensorInfo({ batchSize, inputSize }, armnn::DataType::Float32);
    armnn::TensorInfo cellStateTensorInfo({ batchSize, numUnits}, armnn::DataType::Float32);
    armnn::TensorInfo outputStateTensorInfo({ batchSize, outputSize }, armnn::DataType::Float32);
    armnn::TensorInfo lstmTensorInfoScratchBuff({ batchSize, numUnits * 3 }, armnn::DataType::Float32);

    inputLayer->GetOutputSlot(0).Connect(lstmLayer->GetInputSlot(0));
    inputLayer->GetOutputSlot(0).SetTensorInfo(inputTensorInfo);

    outputStateIn->GetOutputSlot(0).Connect(lstmLayer->GetInputSlot(1));
    outputStateIn->GetOutputSlot(0).SetTensorInfo(outputStateTensorInfo);

    cellStateIn->GetOutputSlot(0).Connect(lstmLayer->GetInputSlot(2));
    cellStateIn->GetOutputSlot(0).SetTensorInfo(cellStateTensorInfo);

    lstmLayer->GetOutputSlot(0).Connect(scratchBuffer->GetInputSlot(0));
    lstmLayer->GetOutputSlot(0).SetTensorInfo(lstmTensorInfoScratchBuff);

    lstmLayer->GetOutputSlot(1).Connect(outputStateOut->GetInputSlot(0));
    lstmLayer->GetOutputSlot(1).SetTensorInfo(outputStateTensorInfo);

    lstmLayer->GetOutputSlot(2).Connect(cellStateOut->GetInputSlot(0));
    lstmLayer->GetOutputSlot(2).SetTensorInfo(cellStateTensorInfo);

    lstmLayer->GetOutputSlot(3).Connect(outputLayer->GetInputSlot(0));
    lstmLayer->GetOutputSlot(3).SetTensorInfo(outputStateTensorInfo);

    armnn::INetworkPtr deserializedNetwork = DeserializeNetwork(SerializeNetwork(*network));
    BOOST_CHECK(deserializedNetwork);

    VerifyLstmLayer checker(
        layerName,
        {inputTensorInfo, outputStateTensorInfo, cellStateTensorInfo},
        {lstmTensorInfoScratchBuff, outputStateTensorInfo, cellStateTensorInfo, outputStateTensorInfo},
        descriptor,
        params);
    deserializedNetwork->Accept(checker);
}

BOOST_AUTO_TEST_CASE(SerializeDeserializeLstmNoCifgWithPeepholeAndProjection)
{
    armnn::LstmDescriptor descriptor;
    descriptor.m_ActivationFunc = 4;
    descriptor.m_ClippingThresProj = 0.0f;
    descriptor.m_ClippingThresCell = 0.0f;
    descriptor.m_CifgEnabled = false; // if this is true then we DON'T need to set the OptCifgParams
    descriptor.m_ProjectionEnabled = true;
    descriptor.m_PeepholeEnabled = true;

    const uint32_t batchSize = 2;
    const uint32_t inputSize = 5;
    const uint32_t numUnits = 20;
    const uint32_t outputSize = 16;

    armnn::TensorInfo tensorInfo20x5({numUnits, inputSize}, armnn::DataType::Float32);
    std::vector<float> inputToInputWeightsData = GenerateRandomData<float>(tensorInfo20x5.GetNumElements());
    armnn::ConstTensor inputToInputWeights(tensorInfo20x5, inputToInputWeightsData);

    std::vector<float> inputToForgetWeightsData = GenerateRandomData<float>(tensorInfo20x5.GetNumElements());
    armnn::ConstTensor inputToForgetWeights(tensorInfo20x5, inputToForgetWeightsData);

    std::vector<float> inputToCellWeightsData = GenerateRandomData<float>(tensorInfo20x5.GetNumElements());
    armnn::ConstTensor inputToCellWeights(tensorInfo20x5, inputToCellWeightsData);

    std::vector<float> inputToOutputWeightsData = GenerateRandomData<float>(tensorInfo20x5.GetNumElements());
    armnn::ConstTensor inputToOutputWeights(tensorInfo20x5, inputToOutputWeightsData);

    armnn::TensorInfo tensorInfo20({numUnits}, armnn::DataType::Float32);
    std::vector<float> inputGateBiasData = GenerateRandomData<float>(tensorInfo20.GetNumElements());
    armnn::ConstTensor inputGateBias(tensorInfo20, inputGateBiasData);

    std::vector<float> forgetGateBiasData = GenerateRandomData<float>(tensorInfo20.GetNumElements());
    armnn::ConstTensor forgetGateBias(tensorInfo20, forgetGateBiasData);

    std::vector<float> cellBiasData = GenerateRandomData<float>(tensorInfo20.GetNumElements());
    armnn::ConstTensor cellBias(tensorInfo20, cellBiasData);

    std::vector<float> outputGateBiasData = GenerateRandomData<float>(tensorInfo20.GetNumElements());
    armnn::ConstTensor outputGateBias(tensorInfo20, outputGateBiasData);

    armnn::TensorInfo tensorInfo20x16({numUnits, outputSize}, armnn::DataType::Float32);
    std::vector<float> recurrentToInputWeightsData = GenerateRandomData<float>(tensorInfo20x16.GetNumElements());
    armnn::ConstTensor recurrentToInputWeights(tensorInfo20x16, recurrentToInputWeightsData);

    std::vector<float> recurrentToForgetWeightsData = GenerateRandomData<float>(tensorInfo20x16.GetNumElements());
    armnn::ConstTensor recurrentToForgetWeights(tensorInfo20x16, recurrentToForgetWeightsData);

    std::vector<float> recurrentToCellWeightsData = GenerateRandomData<float>(tensorInfo20x16.GetNumElements());
    armnn::ConstTensor recurrentToCellWeights(tensorInfo20x16, recurrentToCellWeightsData);

    std::vector<float> recurrentToOutputWeightsData = GenerateRandomData<float>(tensorInfo20x16.GetNumElements());
    armnn::ConstTensor recurrentToOutputWeights(tensorInfo20x16, recurrentToOutputWeightsData);

    std::vector<float> cellToInputWeightsData = GenerateRandomData<float>(tensorInfo20.GetNumElements());
    armnn::ConstTensor cellToInputWeights(tensorInfo20, cellToInputWeightsData);

    std::vector<float> cellToForgetWeightsData = GenerateRandomData<float>(tensorInfo20.GetNumElements());
    armnn::ConstTensor cellToForgetWeights(tensorInfo20, cellToForgetWeightsData);

    std::vector<float> cellToOutputWeightsData = GenerateRandomData<float>(tensorInfo20.GetNumElements());
    armnn::ConstTensor cellToOutputWeights(tensorInfo20,  cellToOutputWeightsData);

    armnn::TensorInfo tensorInfo16x20({outputSize, numUnits}, armnn::DataType::Float32);
    std::vector<float> projectionWeightsData = GenerateRandomData<float>(tensorInfo16x20.GetNumElements());
    armnn::ConstTensor projectionWeights(tensorInfo16x20, projectionWeightsData);

    armnn::TensorInfo tensorInfo16({outputSize}, armnn::DataType::Float32);
    std::vector<float> projectionBiasData(outputSize, 0.f);
    armnn::ConstTensor projectionBias(tensorInfo16, projectionBiasData);

    armnn::LstmInputParams params;
    params.m_InputToForgetWeights = &inputToForgetWeights;
    params.m_InputToCellWeights = &inputToCellWeights;
    params.m_InputToOutputWeights = &inputToOutputWeights;
    params.m_RecurrentToForgetWeights = &recurrentToForgetWeights;
    params.m_RecurrentToCellWeights = &recurrentToCellWeights;
    params.m_RecurrentToOutputWeights = &recurrentToOutputWeights;
    params.m_ForgetGateBias = &forgetGateBias;
    params.m_CellBias = &cellBias;
    params.m_OutputGateBias = &outputGateBias;

    // additional params because: descriptor.m_CifgEnabled = false
    params.m_InputToInputWeights = &inputToInputWeights;
    params.m_RecurrentToInputWeights = &recurrentToInputWeights;
    params.m_CellToInputWeights = &cellToInputWeights;
    params.m_InputGateBias = &inputGateBias;

    // additional params because: descriptor.m_ProjectionEnabled = true
    params.m_ProjectionWeights = &projectionWeights;
    params.m_ProjectionBias = &projectionBias;

    // additional params because: descriptor.m_PeepholeEnabled = true
    params.m_CellToForgetWeights = &cellToForgetWeights;
    params.m_CellToOutputWeights = &cellToOutputWeights;

    armnn::INetworkPtr network = armnn::INetwork::Create();
    armnn::IConnectableLayer* const inputLayer   = network->AddInputLayer(0);
    armnn::IConnectableLayer* const cellStateIn = network->AddInputLayer(1);
    armnn::IConnectableLayer* const outputStateIn = network->AddInputLayer(2);
    const std::string layerName("lstm");
    armnn::IConnectableLayer* const lstmLayer = network->AddLstmLayer(descriptor, params, layerName.c_str());
    armnn::IConnectableLayer* const scratchBuffer  = network->AddOutputLayer(0);
    armnn::IConnectableLayer* const outputStateOut  = network->AddOutputLayer(1);
    armnn::IConnectableLayer* const cellStateOut  = network->AddOutputLayer(2);
    armnn::IConnectableLayer* const outputLayer  = network->AddOutputLayer(3);

    // connect up
    armnn::TensorInfo inputTensorInfo({ batchSize, inputSize }, armnn::DataType::Float32);
    armnn::TensorInfo cellStateTensorInfo({ batchSize, numUnits}, armnn::DataType::Float32);
    armnn::TensorInfo outputStateTensorInfo({ batchSize, outputSize }, armnn::DataType::Float32);
    armnn::TensorInfo lstmTensorInfoScratchBuff({ batchSize, numUnits * 4 }, armnn::DataType::Float32);

    inputLayer->GetOutputSlot(0).Connect(lstmLayer->GetInputSlot(0));
    inputLayer->GetOutputSlot(0).SetTensorInfo(inputTensorInfo);

    outputStateIn->GetOutputSlot(0).Connect(lstmLayer->GetInputSlot(1));
    outputStateIn->GetOutputSlot(0).SetTensorInfo(outputStateTensorInfo);

    cellStateIn->GetOutputSlot(0).Connect(lstmLayer->GetInputSlot(2));
    cellStateIn->GetOutputSlot(0).SetTensorInfo(cellStateTensorInfo);

    lstmLayer->GetOutputSlot(0).Connect(scratchBuffer->GetInputSlot(0));
    lstmLayer->GetOutputSlot(0).SetTensorInfo(lstmTensorInfoScratchBuff);

    lstmLayer->GetOutputSlot(1).Connect(outputStateOut->GetInputSlot(0));
    lstmLayer->GetOutputSlot(1).SetTensorInfo(outputStateTensorInfo);

    lstmLayer->GetOutputSlot(2).Connect(cellStateOut->GetInputSlot(0));
    lstmLayer->GetOutputSlot(2).SetTensorInfo(cellStateTensorInfo);

    lstmLayer->GetOutputSlot(3).Connect(outputLayer->GetInputSlot(0));
    lstmLayer->GetOutputSlot(3).SetTensorInfo(outputStateTensorInfo);

    armnn::INetworkPtr deserializedNetwork = DeserializeNetwork(SerializeNetwork(*network));
    BOOST_CHECK(deserializedNetwork);

    VerifyLstmLayer checker(
        layerName,
        {inputTensorInfo, outputStateTensorInfo, cellStateTensorInfo},
        {lstmTensorInfoScratchBuff, outputStateTensorInfo, cellStateTensorInfo, outputStateTensorInfo},
        descriptor,
        params);
    deserializedNetwork->Accept(checker);
}

BOOST_AUTO_TEST_SUITE_END()
