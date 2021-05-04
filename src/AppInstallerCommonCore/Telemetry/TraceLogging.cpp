﻿// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License. See LICENSE in the project root for license information.

#include "pch.h"
#include "TraceLogging.h"

// GUID for Microsoft.PackageManager.Client : {c0cf606f-569b-5c20-27d9-88a745fa2175}
TRACELOGGING_DEFINE_PROVIDER(
    g_hTraceProvider,
    "Microsoft.PackageManager.Client",
    (0xc0cf606f, 0x569b, 0x5c20, 0x27, 0xd9, 0x88, 0xa7, 0x45, 0xfa, 0x21, 0x75),
    TraceLoggingOptionMicrosoftTelemetry());

bool g_IsTelemetryProviderEnabled{};
UCHAR g_TelemetryProviderLevel{};
ULONGLONG g_TelemetryProviderMatchAnyKeyword{};
GUID g_TelemetryProviderActivityId{};
std::once_flag g_registerTraceProvideOnlyOnce{};

void WINAPI TelemetryProviderEnabledCallback(
    _In_      LPCGUID /*sourceId*/,
    _In_      ULONG isEnabled,
    _In_      UCHAR level,
    _In_      ULONGLONG matchAnyKeyword,
    _In_      ULONGLONG /*matchAllKeywords*/,
    _In_opt_  PEVENT_FILTER_DESCRIPTOR /*filterData*/,
    _In_opt_  PVOID /*callbackContext*/)
{
    g_IsTelemetryProviderEnabled = !!isEnabled;
    g_TelemetryProviderLevel = level;
    g_TelemetryProviderMatchAnyKeyword = matchAnyKeyword;
}

void RegisterTraceLogging()
{
    try
    {
        std::call_once(g_registerTraceProvideOnlyOnce, []()
        {
            HRESULT hr = S_OK;

            TraceLoggingRegisterEx(g_hTraceProvider, TelemetryProviderEnabledCallback, nullptr);

            // Generate the ActivityId used to track the session
            // This is never used. Should this be removed ? Or can be this purposed for anything good ?
            hr = CoCreateGuid(&g_TelemetryProviderActivityId);
            if (FAILED(hr))
            {
                TraceLoggingWriteActivity(
                    g_hTraceProvider,
                    "CreateGuidError",
                    nullptr,
                    nullptr,
                    TraceLoggingHResult(hr),
                    TelemetryPrivacyDataTag(PDT_ProductAndServicePerformance),
                    TraceLoggingKeyword(MICROSOFT_KEYWORD_CRITICAL_DATA));

                g_TelemetryProviderActivityId = GUID_NULL;
            };
        });
    }
    catch (...)
    {
        // May throw std::system_error if any condition prevents calls to call_once from executing as specified
        // Loggers are best effort and shouldn't block core functionality. So eat up the exceptions here
    }
}

void UnRegisterTraceLogging()
{
    TraceLoggingUnregister(g_hTraceProvider);
}
