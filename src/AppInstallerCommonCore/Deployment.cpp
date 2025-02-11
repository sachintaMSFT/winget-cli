// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
#pragma once
#include "pch.h"
#include "Public/AppInstallerDeployment.h"
#include "Public/AppInstallerLogging.h"
#include "Public/AppInstallerMsixInfo.h"
#include "Public/AppInstallerRuntime.h"
#include "Public/AppInstallerStrings.h"

namespace AppInstaller::Deployment
{
    using namespace winrt::Windows::Foundation;
    using namespace winrt::Windows::Management::Deployment;

    namespace
    {
        size_t GetDeploymentOperationId()
        {
            static std::atomic_size_t s_deploymentId = 0;
            return s_deploymentId.fetch_add(1);
        }

        HRESULT WaitForDeployment(
            IAsyncOperationWithProgress<DeploymentResult, DeploymentProgress>& deployOperation,
            size_t id,
            IProgressCallback& callback,
            bool throwOnError = true)
        {
            AICLI_LOG(Core, Info, << "Begin waiting for operation #" << id);

            AsyncOperationProgressHandler<DeploymentResult, DeploymentProgress> progressCallback(
                [&callback](const IAsyncOperationWithProgress<DeploymentResult, DeploymentProgress>&, DeploymentProgress progress)
                {
                    callback.OnProgress(progress.percentage, 100, ProgressType::Percent);
                }
            );

            // Set progress callback.
            deployOperation.Progress(progressCallback);

            auto removeCancel = callback.SetCancellationFunction([&]() { deployOperation.Cancel(); });

            AICLI_LOG(Core, Info, << "Begin blocking for operation #" << id);

            auto deployResult = deployOperation.get();

            if (!SUCCEEDED(deployResult.ExtendedErrorCode()))
            {
                AICLI_LOG(Core, Error, << "Deployment operation #" << id << ": " << Utility::ConvertToUTF8(deployResult.ErrorText()));

                // Note that while the format string is char*, it gets converted to wchar before being used.
                if (throwOnError)
                {
                    THROW_HR_MSG(deployResult.ExtendedErrorCode(), "Operation failed: %ws", deployResult.ErrorText().c_str());
                }
                else
                {
                    // Simple return because this path is generally used for recovery cases
                    return deployResult.ExtendedErrorCode();
                }
            }
            else
            {
                AICLI_LOG(Core, Info, << "Successfully completed #" << id);
            }

            return S_OK;
        }
    }

    void AddPackage(
        const winrt::Windows::Foundation::Uri& uri,
        winrt::Windows::Management::Deployment::DeploymentOptions options,
        bool skipSmartScreen,
        IProgressCallback& callback)
    {
        size_t id = GetDeploymentOperationId();
        AICLI_LOG(Core, Info, << "Starting AddPackage operation #" << id << ": " << Utility::ConvertToUTF8(uri.AbsoluteUri().c_str()) << " SkipSmartScreen: " << skipSmartScreen);

        PackageManager packageManager;

        IAsyncOperationWithProgress<DeploymentResult, DeploymentProgress> deployOperation;

        if (skipSmartScreen)
        {
            deployOperation = packageManager.AddPackageAsync(
                uri,
                nullptr, /*dependencyPackageUris*/
                options,
                nullptr, /*targetVolume*/
                nullptr, /*optionalAndRelatedPackageFamilyNames*/
                nullptr, /*optionalPackageUris*/
                nullptr /*relatedPackageUris*/);
        }
        else
        {
            deployOperation = packageManager.RequestAddPackageAsync(
                uri,
                nullptr, /*dependencyPackageUris*/
                options,
                nullptr, /*targetVolume*/
                nullptr, /*optionalAndRelatedPackageFamilyNames*/
                nullptr /*relatedPackageUris*/);
        }

        WaitForDeployment(deployOperation, id, callback);
    }

    bool AddPackageWithDeferredFallback(
        const std::string& uri,
        bool skipSmartScreen,
        IProgressCallback& callback)
    {
        PackageManager packageManager;

        // In the event of a failure we want to ensure that the package is not left on the system.
        Msix::MsixInfo packageInfo{ uri };
        std::wstring packageFullName = packageInfo.GetPackageFullNameWide();
        auto removePackage = wil::scope_exit([&]() {
            try
            {
                RemovePackage(Utility::ConvertToUTF8(packageFullName), callback);
            }
            CATCH_LOG();
        });

        Uri uriObject(Utility::ConvertToUTF16(uri));

        if (!skipSmartScreen)
        {
            // The only way to get SmartScreen is to use RequestAddPackageAsync, so we will have to start with that.
            size_t id = GetDeploymentOperationId();
            AICLI_LOG(Core, Info, << "Starting RequestAddPackageAsync operation #" << id << ": " << uri);

            DeploymentOptions options = DeploymentOptions::None;
            // Optimization to keep files if the package is in use. Only available in a newer OS per:
            // https://docs.microsoft.com/en-us/uwp/api/Windows.Management.Deployment.DeploymentOptions
            if (Runtime::IsCurrentOSVersionGreaterThanOrEqual(Utility::Version{ "10.0.18362.0" }))
            {
                options = DeploymentOptions::RetainFilesOnFailure;
            }

            IAsyncOperationWithProgress<DeploymentResult, DeploymentProgress> deployOperation = packageManager.RequestAddPackageAsync(
                uriObject,
                nullptr, /*dependencyPackageUris*/
                options,
                nullptr, /*targetVolume*/
                nullptr, /*optionalAndRelatedPackageFamilyNames*/
                nullptr /*relatedPackageUris*/);

            HRESULT hr = WaitForDeployment(deployOperation, id, callback, false);

            if (SUCCEEDED(hr))
            {
                removePackage.release();
                return false;
            }

            THROW_HR_IF(hr, FAILED(hr) && hr != HRESULT_FROM_WIN32(ERROR_PACKAGES_IN_USE));
        }

        // If we are skipping SmartScreen or the package was in use, stage then register the package.
        {
            size_t id = GetDeploymentOperationId();
            AICLI_LOG(Core, Info, << "Starting StagePackageAsync operation #" << id << ": " << uri);

            IAsyncOperationWithProgress<DeploymentResult, DeploymentProgress> stageOperation = packageManager.StagePackageAsync(uriObject, nullptr);
            WaitForDeployment(stageOperation, id, callback);
        }

        bool registrationDeferred = false;

        {
            size_t id = GetDeploymentOperationId();
            AICLI_LOG(Core, Info, << "Starting RegisterPackageByFullNameAsync operation #" << id << ": " << Utility::ConvertToUTF8(packageFullName));

            IAsyncOperationWithProgress<DeploymentResult, DeploymentProgress> registerOperation =
                packageManager.RegisterPackageByFullNameAsync(packageFullName, nullptr, DeploymentOptions::None);
            HRESULT hr = WaitForDeployment(registerOperation, id, callback, false);

            if (hr == HRESULT_FROM_WIN32(ERROR_PACKAGES_IN_USE))
            {
                registrationDeferred = true;
            }
            else
            {
                THROW_IF_FAILED(hr);
            }
        }

        removePackage.release();
        return registrationDeferred;
    }

    void RemovePackage(
        std::string_view packageFullName,
        IProgressCallback& callback)
    {
        size_t id = GetDeploymentOperationId();
        AICLI_LOG(Core, Info, << "Starting RemovePackage operation #" << id << ": " << packageFullName);

        PackageManager packageManager;
        winrt::hstring fullName = Utility::ConvertToUTF16(packageFullName).c_str();
        auto deployOperation = packageManager.RemovePackageAsync(fullName, RemovalOptions::None);

        WaitForDeployment(deployOperation, id, callback);
    }
}
