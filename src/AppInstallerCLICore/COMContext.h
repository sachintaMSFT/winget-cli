// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
#pragma once
#include "pch.h"
#include "..\AppInstallerCommonCore\Public\AppInstallerProgress.h"
#include "ExecutionContext.h"
#include "Workflows/WorkflowBase.h"

namespace AppInstaller
{
    using namespace AppInstaller::CLI;
    using namespace AppInstaller::Workflow;

    enum class ReportType: uint32_t
    {
        ExecutionPhaseUpdate,
        BeginProgress,
        Progressing,
        EndProgress,
    };

    class NullStreamBuf : public std::streambuf {};

    struct NullStream
    {
        NullStream();

        ~NullStream() = default;

    protected:
        NullStreamBuf nullStreamBuf;
        std::unique_ptr<std::ostream> null_Out;
        std::unique_ptr<std::istream> null_In;
    };

    // NullStream constructs the Stream parameters for Context constructor
    // Hence, NullStream should always precede Context in base class order of COMContext's inheritance
    struct COMContext : IProgressSink, NullStream, Execution::Context
    {
        // When no Console streams need involvement, construct NullStreams instead to pass to Context
        COMContext() : NullStream(), Execution::Context(*null_Out, *null_In)
        {
            Reporter.SetProgressSink(this);
        }

        COMContext(std::ostream& out, std::istream& in) : Execution::Context(out, in) 
        {
            Reporter.SetProgressSink(this);
        }

        ~COMContext() = default;

        // IProgressSink
        void BeginProgress() override;
        void OnProgress(uint64_t current, uint64_t maximum, ProgressType type) override;
        void EndProgress(bool hideProgressWhenDone) override;

        //Execution::Context
        void SetExecutionStage(ExecutionStage executionPhase);

        void SetProgressCallbackFunction(std::function<void(ReportType reportType, uint64_t current, uint64_t maximum, ProgressType progressType, ExecutionStage executionPhase)>&& f)
        {
            m_comProgressCallback = std::move(f);
        }

    private:
        void SetProgress(uint64_t current, uint64_t maximum, ProgressType type);

        uint64_t m_current = 0;
        uint64_t m_maximum = 0;
        ProgressType m_type = ProgressType::None;
        ExecutionStage m_executionPhase = ExecutionStage::Initial;
        std::function<void(ReportType reportType, uint64_t current, uint64_t maximum, ProgressType progressType, ExecutionStage executionPhase)> m_comProgressCallback;
    };
}