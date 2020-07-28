//
// Copyright © 2017 Arm Ltd. All rights reserved.
// SPDX-License-Identifier: MIT
//
#pragma once

#include <test/CreateWorkload.hpp>

#include <backendsCommon/MemCopyWorkload.hpp>
#include <reference/RefWorkloadFactory.hpp>

#if defined(ARMCOMPUTECL_ENABLED)
#include <cl/ClTensorHandle.hpp>
#endif

#if defined(ARMCOMPUTENEON_ENABLED)
#include <neon/NeonTensorHandle.hpp>
#endif

using namespace armnn;

namespace
{

using namespace std;

template<typename IComputeTensorHandle>
boost::test_tools::predicate_result CompareTensorHandleShape(IComputeTensorHandle*               tensorHandle,
                                                             std::initializer_list<unsigned int> expectedDimensions)
{
    arm_compute::ITensorInfo* info = tensorHandle->GetTensor().info();

    auto infoNumDims = info->num_dimensions();
    auto numExpectedDims = expectedDimensions.size();
    if (infoNumDims != numExpectedDims)
    {
        boost::test_tools::predicate_result res(false);
        res.message() << "Different number of dimensions [" << info->num_dimensions()
                      << "!=" << expectedDimensions.size() << "]";
        return res;
    }

    size_t i = info->num_dimensions() - 1;

    for (unsigned int expectedDimension : expectedDimensions)
    {
        if (info->dimension(i) != expectedDimension)
        {
            boost::test_tools::predicate_result res(false);
            res.message() << "For dimension " << i <<
                             " expected size " << expectedDimension <<
                             " got " << info->dimension(i);
            return res;
        }

        i--;
    }

    return true;
}

template<typename IComputeTensorHandle>
void CreateMemCopyWorkloads(IWorkloadFactory& factory)
{
    Graph graph;
    RefWorkloadFactory refFactory;

    // Creates the layers we're testing.
    Layer* const layer1 = graph.AddLayer<MemCopyLayer>("layer1");
    Layer* const layer2 = graph.AddLayer<MemCopyLayer>("layer2");

    // Creates extra layers.
    Layer* const input = graph.AddLayer<InputLayer>(0, "input");
    Layer* const output = graph.AddLayer<OutputLayer>(0, "output");

    // Connects up.
    TensorInfo tensorInfo({2, 3}, DataType::Float32);
    Connect(input, layer1, tensorInfo);
    Connect(layer1, layer2, tensorInfo);
    Connect(layer2, output, tensorInfo);

    input->CreateTensorHandles(graph, refFactory);
    layer1->CreateTensorHandles(graph, factory);
    layer2->CreateTensorHandles(graph, refFactory);
    output->CreateTensorHandles(graph, refFactory);

    // make the workloads and check them
    auto workload1 = MakeAndCheckWorkload<CopyMemGenericWorkload>(*layer1, graph, factory);
    auto workload2 = MakeAndCheckWorkload<CopyMemGenericWorkload>(*layer2, graph, refFactory);

    MemCopyQueueDescriptor queueDescriptor1 = workload1->GetData();
    BOOST_TEST(queueDescriptor1.m_Inputs.size() == 1);
    BOOST_TEST(queueDescriptor1.m_Outputs.size() == 1);
    auto inputHandle1  = boost::polymorphic_downcast<ConstCpuTensorHandle*>(queueDescriptor1.m_Inputs[0]);
    auto outputHandle1 = boost::polymorphic_downcast<IComputeTensorHandle*>(queueDescriptor1.m_Outputs[0]);
    BOOST_TEST((inputHandle1->GetTensorInfo() == TensorInfo({2, 3}, DataType::Float32)));
    BOOST_TEST(CompareTensorHandleShape<IComputeTensorHandle>(outputHandle1, {2, 3}));


    MemCopyQueueDescriptor queueDescriptor2 = workload2->GetData();
    BOOST_TEST(queueDescriptor2.m_Inputs.size() == 1);
    BOOST_TEST(queueDescriptor2.m_Outputs.size() == 1);
    auto inputHandle2  = boost::polymorphic_downcast<IComputeTensorHandle*>(queueDescriptor2.m_Inputs[0]);
    auto outputHandle2 = boost::polymorphic_downcast<CpuTensorHandle*>(queueDescriptor2.m_Outputs[0]);
    BOOST_TEST(CompareTensorHandleShape<IComputeTensorHandle>(inputHandle2, {2, 3}));
    BOOST_TEST((outputHandle2->GetTensorInfo() == TensorInfo({2, 3}, DataType::Float32)));
}

} //namespace
