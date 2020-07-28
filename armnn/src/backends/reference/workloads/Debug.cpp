//
// Copyright © 2017 Arm Ltd. All rights reserved.
// SPDX-License-Identifier: MIT
//
#include "Debug.hpp"

#include <boost/numeric/conversion/cast.hpp>

#include <algorithm>
#include <iostream>

namespace armnn
{

template <typename T>
void Debug(const TensorInfo& inputInfo,
           const T* inputData,
           LayerGuid guid,
           const std::string& layerName,
           unsigned int slotIndex)
{
    const unsigned int numDims = inputInfo.GetNumDimensions();
    const unsigned int numElements = inputInfo.GetNumElements();
    const TensorShape& inputShape = inputInfo.GetShape();

    unsigned int strides[numDims];
    strides[numDims - 1] = inputShape[numDims - 1];

    for (unsigned int i = 2; i <= numDims; i++)
    {
        strides[numDims - i] = strides[numDims - i + 1] * inputShape[numDims - i];
    }

    std::cout << "{ ";
    std::cout << "\"layerGuid\": " << guid << ", ";
    std::cout << "\"layerName\": \"" << layerName << "\", ";
    std::cout << "\"outputSlot\": " << slotIndex << ", ";
    std::cout << "\"shape\": ";

    std::cout << "[";
    for (unsigned int i = 0; i < numDims; i++)
    {
        std::cout << inputShape[i];
        if (i != numDims - 1)
        {
            std::cout << ", ";
        }
    }
    std::cout << "], ";

    std::cout << "\"min\": "
        << boost::numeric_cast<float>(*std::min_element(inputData, inputData + numElements)) << ", ";

    std::cout << "\"max\": "
        << boost::numeric_cast<float>(*std::max_element(inputData, inputData + numElements)) << ", ";

    std::cout << "\"data\": ";

    for (unsigned int i = 0; i < numElements; i++)
    {
        for (unsigned int j = 0; j < numDims; j++)
        {
            if (i % strides[j] == 0)
            {
                std::cout << "[" ;
            }
        }

        std::cout << boost::numeric_cast<float>(inputData[i]);

        for (unsigned int j = 0; j < numDims; j++)
        {
            if ((i+1) % strides[j] == 0)
            {
                std::cout << "]" ;
            }
        }

        if (i != numElements - 1)
        {
            std::cout << ", ";
        }
    }

    std::cout << " }" << std::endl;
}

template void Debug<float>(const TensorInfo& inputInfo,
                           const float* inputData,
                           LayerGuid guid,
                           const std::string& layerName,
                           unsigned int slotIndex);

template void Debug<uint8_t>(const TensorInfo& inputInfo,
                             const uint8_t* inputData,
                             LayerGuid guid,
                             const std::string& layerName,
                             unsigned int slotIndex);

} // namespace armnn
