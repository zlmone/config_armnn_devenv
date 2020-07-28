﻿//
// Copyright © 2017 Arm Ltd. All rights reserved.
// SPDX-License-Identifier: MIT
//
#pragma once

#include <armnn/ArmNN.hpp>
#include <armnn/TypesUtils.hpp>
#include "InferenceModel.hpp"

#include <Logging.hpp>

#include <boost/log/core/core.hpp>
#include <boost/program_options.hpp>


namespace armnn
{

inline std::istream& operator>>(std::istream& in, armnn::Compute& compute)
{
    std::string token;
    in >> token;
    compute = armnn::ParseComputeDevice(token.c_str());
    if (compute == armnn::Compute::Undefined)
    {
        in.setstate(std::ios_base::failbit);
        throw boost::program_options::validation_error(boost::program_options::validation_error::invalid_option_value);
    }
    return in;
}

inline std::istream& operator>>(std::istream& in, armnn::BackendId& backend)
{
    std::string token;
    in >> token;
    armnn::Compute compute = armnn::ParseComputeDevice(token.c_str());
    if (compute == armnn::Compute::Undefined)
    {
        in.setstate(std::ios_base::failbit);
        throw boost::program_options::validation_error(boost::program_options::validation_error::invalid_option_value);
    }
    backend = compute;
    return in;
}

namespace test
{

class TestFrameworkException : public Exception
{
public:
    using Exception::Exception;
};

struct InferenceTestOptions
{
    unsigned int m_IterationCount;
    std::string m_InferenceTimesFile;
    bool m_EnableProfiling;

    InferenceTestOptions()
        : m_IterationCount(0),
          m_EnableProfiling(0)
    {}
};

enum class TestCaseResult
{
    /// The test completed without any errors.
    Ok,
    /// The test failed (e.g. the prediction didn't match the validation file).
    /// This will eventually fail the whole program but the remaining test cases will still be run.
    Failed,
    /// The test failed with a fatal error. The remaining tests will not be run.
    Abort
};

class IInferenceTestCase
{
public:
    virtual ~IInferenceTestCase() {}

    virtual void Run() = 0;
    virtual TestCaseResult ProcessResult(const InferenceTestOptions& options) = 0;
};

class IInferenceTestCaseProvider
{
public:
    virtual ~IInferenceTestCaseProvider() {}

    virtual void AddCommandLineOptions(boost::program_options::options_description& options) {};
    virtual bool ProcessCommandLineOptions(const InferenceTestOptions &commonOptions) { return true; };
    virtual std::unique_ptr<IInferenceTestCase> GetTestCase(unsigned int testCaseId) = 0;
    virtual bool OnInferenceTestFinished() { return true; };
};

template <typename TModel>
class InferenceModelTestCase : public IInferenceTestCase
{
public:
    using TContainer = boost::variant<std::vector<float>, std::vector<int>, std::vector<unsigned char>>;

    InferenceModelTestCase(TModel& model,
                           unsigned int testCaseId,
                           const std::vector<TContainer>& inputs,
                           const std::vector<unsigned int>& outputSizes)
        : m_Model(model)
        , m_TestCaseId(testCaseId)
        , m_Inputs(std::move(inputs))
    {
        // Initialize output vector
        const size_t numOutputs = outputSizes.size();
        m_Outputs.reserve(numOutputs);

        for (size_t i = 0; i < numOutputs; i++)
        {
            m_Outputs.push_back(std::vector<typename TModel::DataType>(outputSizes[i]));
        }
    }

    virtual void Run() override
    {
        m_Model.Run(m_Inputs, m_Outputs);
    }

protected:
    unsigned int GetTestCaseId() const { return m_TestCaseId; }
    const std::vector<TContainer>& GetOutputs() const { return m_Outputs; }

private:
    TModel&                 m_Model;
    unsigned int            m_TestCaseId;
    std::vector<TContainer> m_Inputs;
    std::vector<TContainer> m_Outputs;
};

template <typename TTestCaseDatabase, typename TModel>
class ClassifierTestCase : public InferenceModelTestCase<TModel>
{
public:
    ClassifierTestCase(int& numInferencesRef,
        int& numCorrectInferencesRef,
        const std::vector<unsigned int>& validationPredictions,
        std::vector<unsigned int>* validationPredictionsOut,
        TModel& model,
        unsigned int testCaseId,
        unsigned int label,
        std::vector<typename TModel::DataType> modelInput);

    virtual TestCaseResult ProcessResult(const InferenceTestOptions& params) override;

private:
    unsigned int m_Label;
    InferenceModelInternal::QuantizationParams m_QuantizationParams;

    /// These fields reference the corresponding member in the ClassifierTestCaseProvider.
    /// @{
    int& m_NumInferencesRef;
    int& m_NumCorrectInferencesRef;
    const std::vector<unsigned int>& m_ValidationPredictions;
    std::vector<unsigned int>* m_ValidationPredictionsOut;
    /// @}
};

template <typename TDatabase, typename InferenceModel>
class ClassifierTestCaseProvider : public IInferenceTestCaseProvider
{
public:
    template <typename TConstructDatabaseCallable, typename TConstructModelCallable>
    ClassifierTestCaseProvider(TConstructDatabaseCallable constructDatabase, TConstructModelCallable constructModel);

    virtual void AddCommandLineOptions(boost::program_options::options_description& options) override;
    virtual bool ProcessCommandLineOptions(const InferenceTestOptions &commonOptions) override;
    virtual std::unique_ptr<IInferenceTestCase> GetTestCase(unsigned int testCaseId) override;
    virtual bool OnInferenceTestFinished() override;

private:
    void ReadPredictions();

    typename InferenceModel::CommandLineOptions m_ModelCommandLineOptions;
    std::function<std::unique_ptr<InferenceModel>(const InferenceTestOptions& commonOptions,
                                                  typename InferenceModel::CommandLineOptions)> m_ConstructModel;
    std::unique_ptr<InferenceModel> m_Model;

    std::string m_DataDir;
    std::function<TDatabase(const char*, const InferenceModel&)> m_ConstructDatabase;
    std::unique_ptr<TDatabase> m_Database;

    int m_NumInferences; // Referenced by test cases.
    int m_NumCorrectInferences; // Referenced by test cases.

    std::string m_ValidationFileIn;
    std::vector<unsigned int> m_ValidationPredictions; // Referenced by test cases.

    std::string m_ValidationFileOut;
    std::vector<unsigned int> m_ValidationPredictionsOut; // Referenced by test cases.
};

bool ParseCommandLine(int argc, char** argv, IInferenceTestCaseProvider& testCaseProvider,
    InferenceTestOptions& outParams);

bool ValidateDirectory(std::string& dir);

bool InferenceTest(const InferenceTestOptions& params,
    const std::vector<unsigned int>& defaultTestCaseIds,
    IInferenceTestCaseProvider& testCaseProvider);

template<typename TConstructTestCaseProvider>
int InferenceTestMain(int argc,
    char* argv[],
    const std::vector<unsigned int>& defaultTestCaseIds,
    TConstructTestCaseProvider constructTestCaseProvider);

template<typename TDatabase,
    typename TParser,
    typename TConstructDatabaseCallable>
int ClassifierInferenceTestMain(int argc, char* argv[], const char* modelFilename, bool isModelBinary,
    const char* inputBindingName, const char* outputBindingName,
    const std::vector<unsigned int>& defaultTestCaseIds,
    TConstructDatabaseCallable constructDatabase,
    const armnn::TensorShape* inputTensorShape = nullptr);

} // namespace test
} // namespace armnn

#include "InferenceTest.inl"
