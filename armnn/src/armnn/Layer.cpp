﻿//
// Copyright © 2017 Arm Ltd. All rights reserved.
// SPDX-License-Identifier: MIT
//
#include "Layer.hpp"

#include "Graph.hpp"
#include <backendsCommon/WorkloadData.hpp>
#include <backendsCommon/CpuTensorHandle.hpp>

#include <boost/cast.hpp>
#include <boost/format.hpp>
#include <boost/log/trivial.hpp>

#include <numeric>

namespace armnn
{

void InputSlot::Insert(Layer& layer)
{
    BOOST_ASSERT(layer.GetNumOutputSlots() == 1);

    OutputSlot* const prevSlot = GetConnectedOutputSlot();

    if (prevSlot != nullptr)
    {
        // Disconnects parent from this.
        prevSlot->Disconnect(*this);

        // Connects inserted layer to parent.
        BOOST_ASSERT(layer.GetNumInputSlots() == 1);
        prevSlot->Connect(layer.GetInputSlot(0));

        // Sets tensor info for inserted layer.
        const TensorInfo& tensorInfo = prevSlot->GetTensorInfo();
        layer.GetOutputHandler().SetTensorInfo(tensorInfo);
    }

    // Connects inserted layer to this.
    layer.GetOutputSlot(0).Connect(*this);
}

const InputSlot* OutputSlot::GetConnection(unsigned int index) const
{
    ValidateConnectionIndex(index);
    return m_Connections[index];
}

InputSlot* OutputSlot::GetConnection(unsigned int index)
{
    ValidateConnectionIndex(index);
    return m_Connections[index];
}

void OutputSlot::SetTensorInfo(const TensorInfo& tensorInfo)
{
    GetOutputHandler().SetTensorInfo(tensorInfo);
}

const TensorInfo& OutputSlot::GetTensorInfo() const
{
    return GetOutputHandler().GetTensorInfo();
}

bool OutputSlot::IsTensorInfoSet() const
{
    return GetOutputHandler().IsTensorInfoSet();
}

bool OutputSlot::ValidateTensorShape(const TensorShape& shape) const
{
    BOOST_ASSERT_MSG(IsTensorInfoSet(), "TensorInfo must be set in order to validate the shape.");
    return shape == m_OutputHandler.GetTensorInfo().GetShape();
}

int OutputSlot::Connect(InputSlot& destination)
{
    destination.SetConnection(this);
    m_Connections.push_back(&destination);
    return boost::numeric_cast<int>(m_Connections.size() - 1);
}

void OutputSlot::Disconnect(InputSlot& slot)
{
    slot.SetConnection(nullptr);
    m_Connections.erase(std::remove(m_Connections.begin(), m_Connections.end(), &slot), m_Connections.end());
}

void OutputSlot::DisconnectAll()
{
    while (GetNumConnections() > 0)
    {
        InputSlot& connection = *GetConnection(0);
        Disconnect(connection);
    }
}

void OutputSlot::MoveAllConnections(OutputSlot& destination)
{
    while (GetNumConnections() > 0)
    {
        InputSlot& connection = *GetConnection(0);
        Disconnect(connection);
        destination.Connect(connection);
    }
}

unsigned int OutputSlot::CalculateIndexOnOwner() const
{
    for (unsigned int i = 0; i < GetOwningLayer().GetNumOutputSlots(); i++)
    {
        if (GetOwningLayer().GetOutputSlot(i) == (*this))
        {
            return i;
        }
    }
    BOOST_ASSERT_MSG(false, "Did not find slot on owner.");
    return 0; // Error
}

bool OutputSlot::operator==(const OutputSlot& other) const
{
    bool isSame = other.GetNumConnections() == GetNumConnections();
    if (!isSame)
    {
        return false;
    }

    for (unsigned int i = 0; i < GetNumConnections(); i++)
    {
        isSame &= other.GetConnection(i) == GetConnection(i);
    }
    return isSame;
}

void OutputSlot::ValidateConnectionIndex(unsigned int index) const
{
    if (boost::numeric_cast<std::size_t>(index) >= m_Connections.size())
    {
        throw InvalidArgumentException(
            boost::str(boost::format("GetConnection: Invalid index %1% provided") % index));
    }
}

LayerGuid OutputSlot::GetOwningLayerGuid() const
{
    return GetOwningLayer().GetGuid();
}

namespace {
LayerGuid GenerateLayerGuid()
{
    // Note: Not thread safe.
    static LayerGuid newGuid=0;
    return newGuid++;
}
} // namespace

Layer::Layer(unsigned int numInputSlots,
             unsigned int numOutputSlots,
             LayerType type,
             DataLayout layout,
             const char* name)
: m_OutputHandlers(numOutputSlots)
, m_LayerName(name ? name : "")
, m_Type(type)
, m_BackendId()
, m_Guid(GenerateLayerGuid())
{
    m_InputSlots.reserve(numInputSlots);
    for (unsigned int i = 0; i < numInputSlots; ++i)
    {
        m_InputSlots.emplace_back(*this, i);
    }

    m_OutputSlots.reserve(numOutputSlots);
    for (unsigned int i = 0; i < numOutputSlots; ++i)
    {
        m_OutputSlots.emplace_back(*this, m_OutputHandlers[i]);
    }
}

Layer::Layer(unsigned int numInputSlots,
             unsigned int numOutputSlots,
             LayerType type,
             const char* name)
: Layer(numInputSlots, numOutputSlots, type, DataLayout::NCHW, name)
{
}

void Layer::CollectWorkloadInputs(WorkloadDataCollector& dataCollector, const Graph& graph) const
{
    for (auto&& inputSlot : GetInputSlots())
    {
        // The graph must be well-formed at this point.
        BOOST_ASSERT(inputSlot.GetConnection());
        const OutputHandler& outputHandler = inputSlot.GetConnectedOutputSlot()->GetOutputHandler();
        dataCollector.Push(outputHandler.GetData(), outputHandler.GetTensorInfo());
    }
}

void Layer::CollectWorkloadOutputs(WorkloadDataCollector& dataCollector, const Graph& graph) const
{
    for (auto&& outputHandler : m_OutputHandlers)
    {
        outputHandler.CollectWorkloadOutputs(dataCollector);
    }
}

void Layer::CreateTensorHandles(Graph& graph, const IWorkloadFactory& factory)
{
    for (auto&& outputHandler : m_OutputHandlers)
    {
        outputHandler.CreateTensorHandles(factory);
    }
}

void Layer::ReleaseConstantData()
{
    // Now free up the static data.
    OperateOnConstantTensors([](std::unique_ptr<ScopedCpuTensorHandle>& handle)
                                 {
                                     handle.reset(nullptr);
                                 });
}

DataType Layer::GetDataType() const
{
    if (GetNumInputSlots() > 0) // Ignore the input layer.
    {
        return GetInputSlot(0).GetConnection()->GetTensorInfo().GetDataType();
    }
    return GetOutputSlot(0).GetTensorInfo().GetDataType();
}

void Layer::ResetPriority() const
{
    m_Priority = 0;
    m_Visiting = false;
}

LayerPriority Layer::GetPriority() const
{
    constexpr LayerPriority inputPrio = std::numeric_limits<LayerPriority>::lowest();
    constexpr LayerPriority outputPrio = std::numeric_limits<LayerPriority>::max();

    if (GetType() == LayerType::Input)
    {
        m_Priority = inputPrio;
    }
    else if (GetType() == LayerType::Output)
    {
        m_Priority = outputPrio;
    }
    else if (m_Priority == 0)
    {
        if (m_Visiting)
        {
            throw GraphValidationException("Graph has circular dependencies: cannot walk");
        }

        auto maxPrio = [](const LayerPriority prio, const InputSlot& slot) -> LayerPriority
            {
                const OutputSlot *outputSlot = slot.GetConnectedOutputSlot();
                if (outputSlot)
                {
                    const Layer& input = outputSlot->GetOwningLayer();
                    return std::max(prio, input.GetPriority());
                }
                else
                {
                    // unconnected input slot
                    return prio;
                }
            };

        m_Visiting = true;
        LayerPriority parentPrio = std::accumulate(GetInputSlots().cbegin(), GetInputSlots().cend(), 0U, maxPrio);
        m_Visiting = false;

        if (parentPrio >= outputPrio)
        {
            throw GraphValidationException("Graph has too many edges");
        }

        m_Priority = parentPrio + 1U;
    }

    return m_Priority;
}

void Layer::VerifyLayerConnections(unsigned int expectedConnections, const CheckLocation& location) const
{
    BOOST_ASSERT(GetNumInputSlots() == expectedConnections);

    for (unsigned int i=0; i<expectedConnections; ++i)
    {
        if (GetInputSlot(i).GetConnection() == nullptr)
        {
            throw LayerValidationException(
                boost::str(
                    boost::format(
                        "Input connection #%1% must be connected "
                        "for %2% layer %3% %4%")
                        % i
                        % GetLayerTypeAsCString(this->GetType())
                        % GetNameStr()
                        % location.AsString()));
        }
        if(! GetInputSlot(i).GetConnection()->IsTensorInfoSet())
        {
            throw LayerValidationException(
                boost::str(
                    boost::format(
                        "TensorInfo of Input connection #%1% must be set on connected OutputSlot for "
                        "%2% layer %3% %4%")
                        % i
                        % GetLayerTypeAsCString(this->GetType())
                        % GetNameStr()
                        % location.AsString()));
        }
    }
}

std::vector<TensorShape> Layer::InferOutputShapes(const std::vector<TensorShape>& inputShapes) const
{
    BOOST_ASSERT(GetNumInputSlots() != 0);
    BOOST_ASSERT(GetNumOutputSlots() != 0);

    // By default we return what we got, meaning the output shape(s) are the same as the input(s).
    // This only works if the number of inputs and outputs are the same. Since we are in the Layer
    // base class, this means the implementation needs to be overridden in the specific layers for
    // the other cases. So the missing implementation justifies the UnimplementedException.

    if (GetNumInputSlots() != GetNumOutputSlots())
    {
        throw UnimplementedException(
            boost::str(
                boost::format(
                    "Default implementation for InferOutputShapes can only be used for "
                    "layers with the same number of input and output slots. This doesn't "
                    "hold for %1% layer %2% (#inputs=%3% #outputs=%4%) %5%")
                    % GetLayerTypeAsCString(this->GetType())
                    % GetNameStr()
                    % GetNumInputSlots()
                    % GetNumOutputSlots()
                    % CHECK_LOCATION().AsString()));
    }
    return inputShapes;
}
} // namespace armnn
