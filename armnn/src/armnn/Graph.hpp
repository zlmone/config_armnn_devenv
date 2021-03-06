//
// Copyright © 2017 Arm Ltd. All rights reserved.
// SPDX-License-Identifier: MIT
//
#pragma once

#include "LayersFwd.hpp"
#include "IGraphObservable.hpp"

#include <armnn/Types.hpp>
#include <armnn/TensorFwd.hpp>
#include <armnn/NetworkFwd.hpp>
#include <armnn/Exceptions.hpp>

#include <list>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <boost/assert.hpp>
#include <boost/iterator/transform_iterator.hpp>

namespace armnn
{

class SubgraphView;

class Graph
{
public:
    template <typename LayerType>
    static LayerType* PtrCast(Layer* const layer)
    {
        return boost::polymorphic_downcast<LayerType*>(layer);
    }

    template <typename Func>
    void ForEachLayer(Func func) const
    {
        for (auto it = m_Layers.begin(); it != m_Layers.end(); )
        {
             auto next = std::next(it);
             func(*it);
             it = next;
        }
    }

    using LayerList = std::list<Layer*>;
    using Iterator = LayerList::const_iterator; // Const so pointers in the list can't be modified externally.
    using IteratorDifference = Iterator::difference_type;

    using ConstIterator        = boost::transform_iterator<decltype(&PtrCast<const Layer>),       Iterator>;
    using ConstIteratorInputs  = boost::transform_iterator<decltype(&PtrCast<const InputLayer>),  Iterator>;
    using ConstIteratorOutputs = boost::transform_iterator<decltype(&PtrCast<const OutputLayer>), Iterator>;

    /// Wrapper class returned by Graph::GetInputLayers()
    struct InputLayersAccessor
    {
        explicit InputLayersAccessor(const Graph& graph) : m_Graph(graph) {}

        ConstIteratorInputs begin() const
        {
            return { m_Graph.m_Layers.begin(), &(PtrCast<const InputLayer>) };
        }

        ConstIteratorInputs end() const
        {
            return { std::next(m_Graph.m_Layers.begin(), static_cast<IteratorDifference>(m_Graph.GetNumInputs())),
                     &(PtrCast<const InputLayer>) };
        }

        const Graph& m_Graph;
    };

    /// Wrapper class returned by Graph::GetOutputLayers()
    struct OutputLayersAccessor
    {
        explicit OutputLayersAccessor(const Graph& graph) : m_Graph(graph) {}

        ConstIteratorOutputs begin() const
        {
            return { std::prev(m_Graph.m_Layers.end(), static_cast<IteratorDifference>(m_Graph.GetNumOutputs())),
                     &(PtrCast<const OutputLayer>) };
        }

        ConstIteratorOutputs end() const
        {
            return { m_Graph.m_Layers.end(), &(PtrCast<const OutputLayer>) };
        }

        const Graph& m_Graph;
    };

    Graph() : m_LayersInOrder(true) {}

    Graph(const Graph& other);

    Graph& operator=(const Graph& other) = delete;

    Graph(Graph&& other)
    {
        *this = std::move(other);
    }

    Graph& operator=(Graph&& other)
    {
        m_InputIds      = std::move(other.m_InputIds);
        m_OutputIds     = std::move(other.m_OutputIds);
        m_LayersInOrder = std::move(other.m_LayersInOrder);
        m_Views         = std::move(other.m_Views);

        other.ForEachLayer([this](Layer* otherLayer)
        {
            otherLayer->Reparent(*this, m_Layers.end());
        });

        BOOST_ASSERT(other.m_PosInGraphMap.empty());
        BOOST_ASSERT(other.m_Layers.empty());

        return *this;
    }

    ~Graph()
    {
        ForEachLayer([](Layer* layer)
        {
            delete layer;
        });
    }

    Status Print() const;

    Status SerializeToDot(std::ostream& stream);

    /// Adds a new layer, of type LayerType, to the graph constructed with the arguments passed.
    template <typename LayerT, typename... Args>
    LayerT* AddLayer(Args&&... args);

    /// Inserts a new layer between the output slot currently connected to insertBefore
    /// and insertBefore itself.
    template <typename LayerT, typename... Args>
    LayerT* InsertNewLayer(InputSlot& insertBefore, Args&&... args);

    /// Inserts a new layer between insertAfter and the input slot(s) currently connected to it
    template <typename LayerT, typename... Args>
    LayerT* InsertNewLayer(OutputSlot& insertAfter, Args&&... args);

    /// Deletes the layer at the specified position.
    void EraseLayer(Iterator pos);

    /// Deletes the layer. Sets @a layer to nullptr on return.
    /// Templated to support pointers to any layer type.
    template <typename LayerT>
    void EraseLayer(LayerT*& layer);

    /// Returns iterator pointing to the beginning of the list. Lowercase for range-based for loops.
    Iterator begin() { return m_Layers.begin(); }
    /// Returns iterator pointing to the end of the list. Lowercase for range-based for loops.
    Iterator end() { return m_Layers.end(); }

    /// Returns const iterator pointing to the beginning of the list. Lowercase for range-based for loops.
    ConstIterator begin() const { return {m_Layers.begin(), &(PtrCast<const Layer>)}; }
    /// Returns const iterator pointing to the end of the list. Lowercase for range-based for loops.
    ConstIterator end() const { return {m_Layers.end(), &(PtrCast<const Layer>)}; }

    /// Returns const iterator pointing to the beginning of the list. Lowercase for range-based for loops.
    ConstIterator cbegin() const { return begin(); }
    /// Returns const iterator pointing to the end of the list. Lowercase for range-based for loops.
    ConstIterator cend() const { return end(); }

    /// Sorts layers in topological order and return this.
    Graph& TopologicalSort() { const_cast<const Graph*>(this)->TopologicalSort(); return *this; }
    const Graph& TopologicalSort() const;

    size_t GetNumInputs() const { return m_InputIds.size(); }
    size_t GetNumOutputs() const { return m_OutputIds.size(); }

    /// Returns a wrapper object with begin(), end() methods to iterate over the input layers
    /// in a range-based for loop.
    InputLayersAccessor GetInputLayers() const { return InputLayersAccessor(*this); }

    /// Returns a wrapper object with begin(), end() methods to iterate over the output layers
    /// in a range-based for loop.
    OutputLayersAccessor GetOutputLayers() const { return OutputLayersAccessor(*this); }

    size_t GetNumLayers() const { return m_Layers.size(); }

    /// Allocates memory for all tensors under output tensor handers of each layer.
    Status AllocateDynamicBuffers();

    /// Modifies the graph in-place, removing edges connecting layers using different compute devices,
    /// and relinking them via an intermediary copy layers.
    void AddCopyLayers();

    /// Substitutes the given sub-graph with either a new layer or a new sub-graph.
    /// In either case, the given layer or all the layers in the given sub-graph must belong to this graph.
    void SubstituteSubgraph(SubgraphView& subgraph, IConnectableLayer* substituteLayer);
    void SubstituteSubgraph(SubgraphView& subgraph, const SubgraphView& substituteSubgraph);

    void InferTensorInfos();

    void AttachObservable(IGraphObservable* const observable, GraphEvent notifyOnEvent) {
        m_Views[notifyOnEvent].emplace_back(observable);
    }

    void DetachObservable(IGraphObservable* const observable, GraphEvent notifyOnEvent) {
        m_Views[notifyOnEvent].remove(observable);
    }

    /// Gets the position of a layer in the graph.
    Iterator GetPosInGraph(Layer& layer);

private:
    template <typename LayerT>
    class LayerInGraphBase;

    template <typename LayerT>
    class LayerInGraph;

    Iterator ForwardToEndOfInputs(Iterator it) const
    {
        while ((it != m_Layers.end()) && ((*it)->GetType() == LayerType::Input))
        {
            ++it;
        }
        return it;
    }

    Iterator RewindToBeginOfOutputs(Iterator it) const
    {
        while ((it != m_Layers.begin()) && ((*std::prev(it))->GetType() == LayerType::Output))
        {
            --it;
        }
        return it;
    }

    void NotifyObservables(GraphEvent event, Layer* graphState)
    {
        // Iterate over all observables observing this event
        for (auto& observable : m_Views[event])
        {
            observable->Update(graphState);
        }
    }

    std::unordered_set<LayerBindingId> m_InputIds;
    std::unordered_set<LayerBindingId> m_OutputIds;
    std::unordered_map<const Layer*, Iterator> m_PosInGraphMap;

    void ReplaceSubgraphConnections(const SubgraphView& subgraph, IConnectableLayer* substituteLayer);
    void ReplaceSubgraphConnections(const SubgraphView& subgraph, const SubgraphView& substituteSubgraph);
    void EraseSubgraphLayers(SubgraphView &subgraph);

    /// Mutable to allow sorting on const object.
    mutable LayerList m_Layers;
    mutable bool m_LayersInOrder;

    std::map<const GraphEvent, std::list<IGraphObservable*>> m_Views;
};

/// Common base class for layers in the graph.
template <typename LayerT>
class Graph::LayerInGraphBase : public LayerT
{
protected:
    template <typename... Args>
    LayerInGraphBase(Graph& graph, Iterator insertBefore, Args&&... args)
        : LayerT(std::forward<Args>(args)...), m_Graph(&graph)
    {
        Insert(*m_Graph, insertBefore);
    }
    ~LayerInGraphBase()
    {
        Remove(*m_Graph);
    }

    void Reparent(Graph& destGraph, Iterator insertBefore) override
    {
        Insert(destGraph, insertBefore);
        Remove(*m_Graph);

        m_Graph = &destGraph;
    }

private:
    void Insert(Graph& graph, Iterator insertBefore)
    {
        graph.m_PosInGraphMap.emplace(this, graph.m_Layers.emplace(insertBefore, this));
    }

    void Remove(Graph& graph)
    {
        auto layerIt = graph.GetPosInGraph(*this);
        graph.m_Layers.erase(layerIt);

        const size_t numErased = graph.m_PosInGraphMap.erase(this);
        boost::ignore_unused(numErased);
        BOOST_ASSERT(numErased == 1);
    }

protected:
    Graph* m_Graph;
};

/// Input/Output layers specialize this template.
template <typename LayerT>
class Graph::LayerInGraph final : public LayerInGraphBase<LayerT>
{
public:
    template <typename... Args>
    LayerInGraph(Graph& graph, Args&&... args)
        : LayerInGraphBase<LayerT>(graph,
                                   // Insert at the back of the intermediate layers (before outputs).
                                   std::prev(graph.end(), IteratorDifference(graph.GetNumOutputs())),
                                   std::forward<Args>(args)...)
    {
    }
    template <typename... Args>
    LayerInGraph(Graph& graph, Iterator insertBefore, Args&&... args)
        : LayerInGraphBase<LayerT>(graph,
                                   // Make sure it's inserted after all inputs and before all outputs.
                                   graph.ForwardToEndOfInputs(graph.RewindToBeginOfOutputs(insertBefore)),
                                   std::forward<Args>(args)...)
    {
    }
};

/// Inputs add/remove their binding id to m_InputIds in the graph.
template <>
class Graph::LayerInGraph<InputLayer> final : public LayerInGraphBase<InputLayer>
{
public:
    template <typename... Args>
    LayerInGraph(Graph& graph, Args&&... args)
        : LayerInGraphBase<InputLayer>(graph,
                                       // Always add to the back of the inputs.
                                       std::next(graph.begin(), IteratorDifference(graph.GetNumInputs())),
                                       std::forward<Args>(args)...)
    {
        const bool isNewId = m_Graph->m_InputIds.emplace(GetBindingId()).second;
        if (!isNewId)
        {
            throw InvalidArgumentException("A layer already exists with the specified id");
        }
    }
    template <typename... Args>
    LayerInGraph(Graph& graph, Iterator, Args&&... args)
        // Ignore Iterator argument. Always add to the back of the inputs.
        : LayerInGraph(graph, std::forward<Args>(args)...)
    {
    }
    ~LayerInGraph() override
    {
        const size_t numErased = m_Graph->m_InputIds.erase(GetBindingId());
        boost::ignore_unused(numErased);
        BOOST_ASSERT(numErased == 1);
    }
};

/// Outputs add/remove their binding id to m_OutputIds in the graph.
template <>
class Graph::LayerInGraph<OutputLayer> final : public LayerInGraphBase<OutputLayer>
{
public:
    template <typename... Args>
    LayerInGraph(Graph& graph, Args&&... args)
        : LayerInGraphBase<OutputLayer>(graph,
                                        // Always add to the back of the outputs.
                                        graph.end(),
                                        std::forward<Args>(args)...)
    {
        const bool isNewId = m_Graph->m_OutputIds.emplace(GetBindingId()).second;
        if (!isNewId)
        {
            throw InvalidArgumentException("A layer already exists with the specified id");
        }
    }
    ~LayerInGraph() override
    {
        const size_t numErased = m_Graph->m_OutputIds.erase(GetBindingId());
        boost::ignore_unused(numErased);
        BOOST_ASSERT(numErased == 1);
    }
};

inline Graph::Iterator Graph::GetPosInGraph(Layer& layer)
{
    auto it = m_PosInGraphMap.find(&layer);
    BOOST_ASSERT(it != m_PosInGraphMap.end());
    return it->second;
}

template <typename LayerT, typename... Args>
inline LayerT* Graph::AddLayer(Args&&... args)
{
    m_LayersInOrder = m_LayersInOrder &&
        ((LayerEnumOf<LayerT>() == LayerType::Input) || (LayerEnumOf<LayerT>() == LayerType::Output));
    LayerT* const layer = new LayerInGraph<LayerT>(*this, std::forward<Args>(args)...);

    NotifyObservables(GraphEvent::LayerAdded, layer);

    return layer;
}

template <typename LayerT, typename... Args>
inline LayerT* Graph::InsertNewLayer(InputSlot& insertBefore, Args&&... args)
{
    // Insert after the parent if any, or before the child otherwise, so the topological order is kept.
    OutputSlot* parentOut = insertBefore.GetConnectedOutputSlot();
    const Iterator pos = (parentOut != nullptr)
                         ? std::next(GetPosInGraph(parentOut->GetOwningLayer()))
                         : GetPosInGraph(insertBefore.GetOwningLayer());
    LayerT* const layer = new LayerInGraph<LayerT>(*this, pos, std::forward<Args>(args)...);
    insertBefore.Insert(*layer);

    NotifyObservables(GraphEvent::LayerAdded, layer);

    return layer;
}

template <typename LayerT, typename... Args>
inline LayerT* Graph::InsertNewLayer(OutputSlot& insertAfter, Args&&... args)
{
    Layer& owningLayer = insertAfter.GetOwningLayer();

    const Iterator pos = std::next(GetPosInGraph(owningLayer));
    LayerT* const layer = new LayerInGraph<LayerT>(*this, pos, std::forward<Args>(args)...);

    BOOST_ASSERT(layer->GetNumInputSlots() == 1);

    insertAfter.MoveAllConnections(layer->GetOutputSlot());
    insertAfter.Connect(layer->GetInputSlot(0));

    NotifyObservables(GraphEvent::LayerAdded, layer);

    return layer;
}

inline void Graph::EraseLayer(Iterator pos)
{
    NotifyObservables(GraphEvent::LayerErased, *pos);

    delete *pos;
}

template <typename LayerT>
inline void Graph::EraseLayer(LayerT*& layer)
{
    BOOST_ASSERT(layer != nullptr);
    EraseLayer(GetPosInGraph(*layer));
    layer = nullptr;
}

} // namespace armnn
