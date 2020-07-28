﻿//
// Copyright © 2017 Arm Ltd. All rights reserved.
// SPDX-License-Identifier: MIT
//

#include "ConvImpl.hpp"

#include <boost/assert.hpp>

#include <cmath>
#include <limits>

namespace armnn
{

QuantizedMultiplierSmallerThanOne::QuantizedMultiplierSmallerThanOne(float multiplier)
{
    BOOST_ASSERT(multiplier >= 0.0f && multiplier < 1.0f);
    if (multiplier == 0.0f)
    {
        m_Multiplier = 0;
        m_RightShift = 0;
    }
    else
    {
        const double q = std::frexp(multiplier, &m_RightShift);
        m_RightShift = -m_RightShift;
        int64_t qFixed = static_cast<int64_t>(std::round(q * (1ll << 31)));
        BOOST_ASSERT(qFixed <= (1ll << 31));
        if (qFixed == (1ll << 31))
        {
            qFixed /= 2;
            --m_RightShift;
        }
        BOOST_ASSERT(m_RightShift >= 0);
        BOOST_ASSERT(qFixed <= std::numeric_limits<int32_t>::max());
        m_Multiplier = static_cast<int32_t>(qFixed);
    }
}

int32_t QuantizedMultiplierSmallerThanOne::operator*(int32_t rhs) const
{
    int32_t x = SaturatingRoundingDoublingHighMul(rhs, m_Multiplier);
    return RoundingDivideByPOT(x, m_RightShift);
}

int32_t QuantizedMultiplierSmallerThanOne::SaturatingRoundingDoublingHighMul(int32_t a, int32_t b)
{
    // Check for overflow.
    if (a == b && a == std::numeric_limits<int32_t>::min())
    {
        return std::numeric_limits<int32_t>::max();
    }
    int64_t a_64(a);
    int64_t b_64(b);
    int64_t ab_64 = a_64 * b_64;
    int32_t nudge = ab_64 >= 0 ? (1 << 30) : (1 - (1 << 30));
    int32_t ab_x2_high32 = static_cast<std::int32_t>((ab_64 + nudge) / (1ll << 31));
    return ab_x2_high32;
}

int32_t QuantizedMultiplierSmallerThanOne::RoundingDivideByPOT(int32_t x, int exponent)
{
    BOOST_ASSERT(exponent >= 0 && exponent <= 31);
    int32_t mask = (1 << exponent) - 1;
    int32_t remainder = x & mask;
    int32_t threshold = (mask >> 1) + (x < 0 ? 1 : 0);
    return (x >> exponent) + (remainder > threshold ? 1 : 0);
}

inline unsigned int GetOffset(DataLayout& dataLayout, const TensorShape& shape, unsigned int b, unsigned int c,
                              unsigned int h, unsigned int w)
{
    switch (dataLayout)
    {
        case DataLayout::NHWC:
            b *= shape[1] * shape[2] * shape[3];
            h *= shape[2] * shape[3];
            w *= shape[3];
            break;
        case DataLayout::NCHW:
        default:
            b *= shape[1] * shape[2] * shape[3];
            c *= shape[2] * shape[3];
            h *= shape[3];
            break;
    }
    return b + c + h + w;
}

void Convolve(const TensorShape& rInputShape,
              Decoder<float>& rInputDecoder,
              const TensorShape& rOutputShape,
              Encoder<float>& rOutputEncoder,
              const TensorShape& rFilterShape,
              Decoder<float>& rFilterDecoder,
              bool biasEnabled,
              Decoder<float>* pBiasDecoder,
              DataLayout dataLayout,
              unsigned int paddingTop,
              unsigned int paddingLeft,
              unsigned int xStride,
              unsigned int yStride,
              unsigned int xDilation,
              unsigned int yDilation,
              bool depthwise)
{
    if (biasEnabled && !pBiasDecoder)
    {
        throw InvalidArgumentException("Bias is enabled but the bias data is invalid");
    }
    const armnnUtils::DataLayoutIndexed dataLayoutIndexed(dataLayout);

    const unsigned int channelsIndex = dataLayoutIndexed.GetChannelsIndex();
    const unsigned int heightIndex   = dataLayoutIndexed.GetHeightIndex();
    const unsigned int widthIndex    = dataLayoutIndexed.GetWidthIndex();

    unsigned int depthMultiplier = depthwise ? rFilterShape[0] : 1;
    unsigned int inputChannels   = depthwise ? rFilterShape[1] : rFilterShape[channelsIndex];
    unsigned int outputChannels  = depthwise ? inputChannels * depthMultiplier : rFilterShape[0];

    unsigned int batchSize    = rOutputShape[0];
    unsigned int outputHeight = rOutputShape[heightIndex];
    unsigned int outputWidth  = rOutputShape[widthIndex];
    unsigned int inputHeight  = rInputShape[heightIndex];
    unsigned int inputWidth   = rInputShape[widthIndex];

    unsigned int filterHeight = depthwise ? rFilterShape[2] : rFilterShape[heightIndex];
    unsigned int filterWidth  = depthwise ? rFilterShape[3] : rFilterShape[widthIndex];

    for (unsigned int batchIdx = 0; batchIdx < batchSize; batchIdx++)
    {
        for (unsigned int cOutput = 0; cOutput < outputChannels; cOutput++)
        {
            for (unsigned int yOutput = 0; yOutput < outputHeight; yOutput++)
            {
                for (unsigned int xOutput = 0; xOutput < outputWidth; xOutput++)
                {
                    // This loop goes over each output element.
                    float sum =  0.0f;

                    // For depthwise, each output channel corresponds to exactly one input channel.
                    // For normal, must loop over each input channel.
                    for (unsigned int cInput = 0; cInput < (depthwise ? 1 : inputChannels); cInput++)
                    {
                        unsigned int depthwiseMultiplierIdx = 0;
                        if (depthwise)
                        {
                            cInput = cOutput / depthMultiplier;
                            depthwiseMultiplierIdx = cOutput % depthMultiplier;
                        }

                        for (unsigned int yFilter = 0; yFilter < filterHeight; yFilter++)
                        {
                            for (unsigned int xFilter = 0; xFilter < filterWidth; xFilter++)
                            {
                                // This loop goes over each input element for each output element.
                                unsigned int filterIndex = 0;

                                // Since dimensionality of kernel depends on depthwiseness, so does index.
                                if (depthwise)
                                {
                                    filterIndex = depthwiseMultiplierIdx * filterWidth * filterHeight * inputChannels +
                                                  cInput * filterWidth * filterHeight +
                                                  yFilter * filterWidth +
                                                  xFilter;
                                }
                                else
                                {
                                    if (dataLayout == DataLayout::NHWC)
                                    {
                                        filterIndex = cOutput * filterHeight * filterWidth * inputChannels +
                                                      yFilter * filterWidth * inputChannels +
                                                      xFilter * inputChannels +
                                                      cInput;
                                    }
                                    else
                                    {
                                        filterIndex = cOutput * filterWidth * filterHeight * inputChannels +
                                                      cInput  * filterWidth * filterHeight +
                                                      yFilter * filterWidth +
                                                      xFilter;
                                    }
                                }
                                rFilterDecoder += filterIndex;
                                float filterValue = rFilterDecoder.Get();
                                rFilterDecoder -= filterIndex;

                                unsigned int yInput = yOutput * yStride + yFilter * yDilation;
                                unsigned int xInput = xOutput * xStride + xFilter * xDilation;

                                float inputValue;

                                // Check if we're in the padding.
                                if (yInput < paddingTop || yInput >= inputHeight + paddingTop ||
                                    xInput < paddingLeft || xInput >= inputWidth + paddingLeft )
                                {
                                    inputValue = 0.0f;
                                }
                                else
                                {
                                    unsigned int inputIndex;

                                    if (dataLayout == DataLayout::NHWC)
                                    {
                                        inputIndex = batchIdx * inputHeight * inputWidth  * inputChannels +
                                                     (yInput - paddingTop) * inputWidth * inputChannels +
                                                     (xInput - paddingLeft) * inputChannels +
                                                     cInput;
                                    }
                                    else
                                    {
                                        inputIndex = batchIdx * inputWidth * inputHeight * inputChannels +
                                                     inputWidth * inputHeight * cInput +
                                                     inputWidth * (yInput - paddingTop) +
                                                     xInput - paddingLeft;
                                    }
                                    rInputDecoder += inputIndex;
                                    inputValue = rInputDecoder.Get();
                                    rInputDecoder -= inputIndex;
                                }
                                sum += filterValue * inputValue;
                            }
                        }
                    }

                    if (biasEnabled)
                    {
                        *pBiasDecoder += cOutput;
                        sum += pBiasDecoder->Get();
                        *pBiasDecoder -= cOutput;
                    }
                    unsigned int outIdx = GetOffset(dataLayout, rOutputShape, batchIdx, cOutput, yOutput, xOutput);

                    rOutputEncoder += outIdx;
                    rOutputEncoder.Set(sum);
                    rOutputEncoder -= outIdx;
                }
            }
        }
    }
}

} //namespace armnn
