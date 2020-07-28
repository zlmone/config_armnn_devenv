﻿//
// Copyright © 2017 Arm Ltd. All rights reserved.
// SPDX-License-Identifier: MIT
//
#pragma once

#include "CpuTensorHandleFwd.hpp"
#include "CompatibleTypes.hpp"

#include <armnn/TypesUtils.hpp>

#include <backendsCommon/OutputHandler.hpp>

#include <algorithm>

namespace armnn
{

// Abstract tensor handles wrapping a CPU-readable region of memory, interpreting it as tensor data.
class ConstCpuTensorHandle : public ITensorHandle
{
public:
    template <typename T>
    const T* GetConstTensor() const
    {
        BOOST_ASSERT(CompatibleTypes<T>(GetTensorInfo().GetDataType()));
        return reinterpret_cast<const T*>(m_Memory);
    }

    const TensorInfo& GetTensorInfo() const
    {
        return m_TensorInfo;
    }

    virtual void Manage() override {}

    virtual ITensorHandle* GetParent() const override { return nullptr; }

    virtual const void* Map(bool /* blocking = true */) const override { return m_Memory; }
    virtual void Unmap() const override {}

    TensorShape GetStrides() const override
    {
        TensorShape shape(m_TensorInfo.GetShape());
        auto size = GetDataTypeSize(m_TensorInfo.GetDataType());
        auto runningSize = size;
        std::vector<unsigned int> strides(shape.GetNumDimensions());
        auto lastIdx = shape.GetNumDimensions()-1;
        for (unsigned int i=0; i < lastIdx ; i++)
        {
            strides[lastIdx-i] = runningSize;
            runningSize *= shape[lastIdx-i];
        }
        strides[0] = runningSize;
        return TensorShape(shape.GetNumDimensions(), strides.data());
    }
    TensorShape GetShape() const override { return m_TensorInfo.GetShape(); }

protected:
    ConstCpuTensorHandle(const TensorInfo& tensorInfo);

    void SetConstMemory(const void* mem) { m_Memory = mem; }

private:
    // Only used for testing
    void CopyOutTo(void *) const override {}
    void CopyInFrom(const void*) override {}

    ConstCpuTensorHandle(const ConstCpuTensorHandle& other) = delete;
    ConstCpuTensorHandle& operator=(const ConstCpuTensorHandle& other) = delete;

    TensorInfo m_TensorInfo;
    const void* m_Memory;
};

template<>
const void* ConstCpuTensorHandle::GetConstTensor<void>() const;

// Abstract specialization of ConstCpuTensorHandle that allows write access to the same data.
class CpuTensorHandle : public ConstCpuTensorHandle
{
public:
    template <typename T>
    T* GetTensor() const
    {
        BOOST_ASSERT(CompatibleTypes<T>(GetTensorInfo().GetDataType()));
        return reinterpret_cast<T*>(m_MutableMemory);
    }

protected:
    CpuTensorHandle(const TensorInfo& tensorInfo);

    void SetMemory(void* mem)
    {
        m_MutableMemory = mem;
        SetConstMemory(m_MutableMemory);
    }

private:

    CpuTensorHandle(const CpuTensorHandle& other) = delete;
    CpuTensorHandle& operator=(const CpuTensorHandle& other) = delete;
    void* m_MutableMemory;
};

template <>
void* CpuTensorHandle::GetTensor<void>() const;

// A CpuTensorHandle that owns the wrapped memory region.
class ScopedCpuTensorHandle : public CpuTensorHandle
{
public:
    explicit ScopedCpuTensorHandle(const TensorInfo& tensorInfo);

    // Copies contents from Tensor.
    explicit ScopedCpuTensorHandle(const ConstTensor& tensor);

    // Copies contents from ConstCpuTensorHandle
    explicit ScopedCpuTensorHandle(const ConstCpuTensorHandle& tensorHandle);

    ScopedCpuTensorHandle(const ScopedCpuTensorHandle& other);
    ScopedCpuTensorHandle& operator=(const ScopedCpuTensorHandle& other);
    ~ScopedCpuTensorHandle();

    virtual void Allocate() override;

private:
    // Only used for testing
    void CopyOutTo(void* memory) const override;
    void CopyInFrom(const void* memory) override;

    void CopyFrom(const ScopedCpuTensorHandle& other);
    void CopyFrom(const void* srcMemory, unsigned int numBytes);
};

// A CpuTensorHandle that wraps an already allocated memory region.
//
// Clients must make sure the passed in memory region stays alive for the lifetime of
// the PassthroughCpuTensorHandle instance.
//
// Note there is no polymorphism to/from ConstPassthroughCpuTensorHandle.
class PassthroughCpuTensorHandle : public CpuTensorHandle
{
public:
    PassthroughCpuTensorHandle(const TensorInfo& tensorInfo, void* mem)
    :   CpuTensorHandle(tensorInfo)
    {
        SetMemory(mem);
    }

    virtual void Allocate() override;
};

// A ConstCpuTensorHandle that wraps an already allocated memory region.
//
// This allows users to pass in const memory to a network.
// Clients must make sure the passed in memory region stays alive for the lifetime of
// the PassthroughCpuTensorHandle instance.
//
// Note there is no polymorphism to/from PassthroughCpuTensorHandle.
class ConstPassthroughCpuTensorHandle : public ConstCpuTensorHandle
{
public:
    ConstPassthroughCpuTensorHandle(const TensorInfo& tensorInfo, const void* mem)
    :   ConstCpuTensorHandle(tensorInfo)
    {
        SetConstMemory(mem);
    }

    virtual void Allocate() override;
};


// Template specializations.

template <>
const void* ConstCpuTensorHandle::GetConstTensor() const;

template <>
void* CpuTensorHandle::GetTensor() const;

} // namespace armnn
