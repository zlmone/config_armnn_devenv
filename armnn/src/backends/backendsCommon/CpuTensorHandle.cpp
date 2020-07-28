﻿//
// Copyright © 2017 Arm Ltd. All rights reserved.
// SPDX-License-Identifier: MIT
//
#include <armnn/Exceptions.hpp>

#include <backendsCommon/CpuTensorHandle.hpp>

#include <cstring>

namespace armnn
{

ConstCpuTensorHandle::ConstCpuTensorHandle(const TensorInfo& tensorInfo)
: m_TensorInfo(tensorInfo)
, m_Memory(nullptr)
{
}

template <>
const void* ConstCpuTensorHandle::GetConstTensor<void>() const
{
    return m_Memory;
}

CpuTensorHandle::CpuTensorHandle(const TensorInfo& tensorInfo)
: ConstCpuTensorHandle(tensorInfo)
, m_MutableMemory(nullptr)
{
}

template <>
void* CpuTensorHandle::GetTensor<void>() const
{
    return m_MutableMemory;
}

ScopedCpuTensorHandle::ScopedCpuTensorHandle(const TensorInfo& tensorInfo)
: CpuTensorHandle(tensorInfo)
{
}

ScopedCpuTensorHandle::ScopedCpuTensorHandle(const ConstTensor& tensor)
: ScopedCpuTensorHandle(tensor.GetInfo())
{
    CopyFrom(tensor.GetMemoryArea(), tensor.GetNumBytes());
}

ScopedCpuTensorHandle::ScopedCpuTensorHandle(const ConstCpuTensorHandle& tensorHandle)
: ScopedCpuTensorHandle(tensorHandle.GetTensorInfo())
{
    CopyFrom(tensorHandle.GetConstTensor<void>(), tensorHandle.GetTensorInfo().GetNumBytes());
}

ScopedCpuTensorHandle::ScopedCpuTensorHandle(const ScopedCpuTensorHandle& other)
: CpuTensorHandle(other.GetTensorInfo())
{
    CopyFrom(other);
}

ScopedCpuTensorHandle& ScopedCpuTensorHandle::operator=(const ScopedCpuTensorHandle& other)
{
    ::operator delete(GetTensor<void>());
    SetMemory(nullptr);
    CopyFrom(other);
    return *this;
}

ScopedCpuTensorHandle::~ScopedCpuTensorHandle()
{
    ::operator delete(GetTensor<void>());
}

void ScopedCpuTensorHandle::Allocate()
{
    if (GetTensor<void>() == nullptr)
    {
        SetMemory(::operator new(GetTensorInfo().GetNumBytes()));
    }
    else
    {
        throw InvalidArgumentException("CpuTensorHandle::Allocate Trying to allocate a CpuTensorHandle"
            "that already has allocated memory.");
    }
}

void ScopedCpuTensorHandle::CopyOutTo(void* memory) const
{
    memcpy(memory, GetTensor<void>(), GetTensorInfo().GetNumBytes());
}

void ScopedCpuTensorHandle::CopyInFrom(const void* memory)
{
    memcpy(GetTensor<void>(), memory, GetTensorInfo().GetNumBytes());
}

void ScopedCpuTensorHandle::CopyFrom(const ScopedCpuTensorHandle& other)
{
    CopyFrom(other.GetTensor<void>(), other.GetTensorInfo().GetNumBytes());
}

void ScopedCpuTensorHandle::CopyFrom(const void* srcMemory, unsigned int numBytes)
{
    BOOST_ASSERT(GetTensor<void>() == nullptr);
    BOOST_ASSERT(GetTensorInfo().GetNumBytes() == numBytes);

    if (srcMemory)
    {
        Allocate();
        memcpy(GetTensor<void>(), srcMemory, numBytes);
    }
}

void PassthroughCpuTensorHandle::Allocate()
{
    throw InvalidArgumentException("PassthroughCpuTensorHandle::Allocate() should never be called");
}

void ConstPassthroughCpuTensorHandle::Allocate()
{
    throw InvalidArgumentException("ConstPassthroughCpuTensorHandle::Allocate() should never be called");
}

} // namespace armnn
