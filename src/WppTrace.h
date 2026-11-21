/*++
    Print to image - trace helpers

    The Microsoft sample used WPP tracing (which requires the WDK trace
    preprocessor to generate *.tmh files).  This build has no WDK, so the
    macros are kept source-compatible but send everything to the debugger
    via OutputDebugStringW.
--*/
#pragma once

#include <strsafe.h>

#define XPSRASFILTER_TRACE_ERROR    0x00000001
#define XPSRASFILTER_TRACE_WARNING  0x00000002
#define XPSRASFILTER_TRACE_INFO     0x00000004
#define XPSRASFILTER_TRACE_VERBOSE  0x00000008

#define WPP_INIT_TRACING(x)         ((void)0)
#define WPP_CLEANUP()               ((void)0)
#define DoTraceMessage(flags, ...)  ((void)0)
#define DoTraceMessageEx(flags, ...) ((void)0)

namespace xpsrasfilter
{

inline void
TraceFailedHRESULT(
    HRESULT         hr,
    char const     *fileName,
    int             lineNum,
    wchar_t const  *extraText
    )
{
    wchar_t buffer[1024];

    StringCchPrintfW(
        buffer,
        ARRAYSIZE(buffer),
        L"ptimage: HRESULT 0x%08X at %S(%d) %s\n",
        hr,
        fileName ? fileName : "?",
        lineNum,
        extraText ? extraText : L""
        );

    ::OutputDebugStringW(buffer);
}

} // namespace xpsrasfilter

#define WPP_LOG_ON_FAILED_HRESULT_WITH_TEXT(func_,text_)    \
    {                                                       \
        HRESULT hr_trace_ = func_;                          \
        if (FAILED(hr_trace_))                              \
        {                                                   \
            xpsrasfilter::TraceFailedHRESULT(               \
                hr_trace_,                                  \
                __FILE__,                                   \
                __LINE__,                                   \
                text_                                       \
                );                                          \
        }                                                   \
    }

#define WPP_LOG_ON_FAILED_HRESULT(func_)                    \
    {                                                       \
        WPP_LOG_ON_FAILED_HRESULT_WITH_TEXT(func_, L"")     \
    }
