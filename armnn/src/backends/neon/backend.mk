#
# Copyright © 2017 ARM Ltd. All rights reserved.
# SPDX-License-Identifier: MIT
#

# BACKEND_SOURCES contains the list of files to be included
# in the Android build and it is picked up by the Android.mk
# file in the root of ArmNN

# The variable to enable/disable the NEON backend (ARMNN_COMPUTE_NEON_ENABLED) is declared in android-nn-driver/Android.mk
ifeq ($(ARMNN_COMPUTE_NEON_ENABLED),1)

# ARMNN_COMPUTE_NEON_ENABLED == 1
# Include the source files for the NEON backend

BACKEND_SOURCES := \
        NeonBackend.cpp \
        NeonInterceptorScheduler.cpp \
        NeonLayerSupport.cpp \
        NeonTimer.cpp \
        NeonWorkloadFactory.cpp \
        workloads/NeonActivationWorkload.cpp \
        workloads/NeonAdditionWorkload.cpp \
        workloads/NeonBatchNormalizationWorkload.cpp \
        workloads/NeonConcatWorkload.cpp \
        workloads/NeonConstantWorkload.cpp \
        workloads/NeonConvertFp16ToFp32Workload.cpp \
        workloads/NeonConvertFp32ToFp16Workload.cpp \
        workloads/NeonConvolution2dWorkload.cpp \
        workloads/NeonDepthwiseConvolutionWorkload.cpp \
        workloads/NeonFloorFloatWorkload.cpp \
        workloads/NeonFullyConnectedWorkload.cpp \
        workloads/NeonGreaterWorkload.cpp \
        workloads/NeonL2NormalizationFloatWorkload.cpp \
        workloads/NeonLstmFloatWorkload.cpp \
        workloads/NeonMaximumWorkload.cpp \
        workloads/NeonMeanWorkload.cpp \
        workloads/NeonMinimumWorkload.cpp \
        workloads/NeonMultiplicationWorkload.cpp \
        workloads/NeonNormalizationFloatWorkload.cpp \
        workloads/NeonPadWorkload.cpp \
        workloads/NeonPermuteWorkload.cpp \
        workloads/NeonPooling2dWorkload.cpp \
        workloads/NeonReshapeWorkload.cpp \
        workloads/NeonResizeBilinearWorkload.cpp \
        workloads/NeonSoftmaxBaseWorkload.cpp \
        workloads/NeonSoftmaxFloatWorkload.cpp \
        workloads/NeonSoftmaxUint8Workload.cpp \
        workloads/NeonSplitterWorkload.cpp \
        workloads/NeonSubtractionWorkload.cpp

else

# ARMNN_COMPUTE_NEON_ENABLED == 0
# No source file will be compiled for the NEON backend

BACKEND_SOURCES :=

endif

# BACKEND_TEST_SOURCES contains the list of files to be included
# in the Android unit test build (armnn-tests) and it is picked
# up by the Android.mk file in the root of ArmNN

# The variable to enable/disable the NEON backend (ARMNN_COMPUTE_NEON_ENABLED) is declared in android-nn-driver/Android.mk
ifeq ($(ARMNN_COMPUTE_NEON_ENABLED),1)

# ARMNN_COMPUTE_NEON_ENABLED == 1
# Include the source files for the NEON backend tests

BACKEND_TEST_SOURCES := \
        test/NeonCreateWorkloadTests.cpp \
        test/NeonEndToEndTests.cpp \
        test/NeonJsonPrinterTests.cpp \
        test/NeonLayerSupportTests.cpp \
        test/NeonLayerTests.cpp \
        test/NeonMemCopyTests.cpp \
        test/NeonOptimizedNetworkTests.cpp \
        test/NeonRuntimeTests.cpp \
        test/NeonTimerTest.cpp

else

# ARMNN_COMPUTE_NEON_ENABLED == 0
# No source file will be compiled for the NEON backend tests

BACKEND_TEST_SOURCES :=

endif
