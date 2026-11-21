/*++
    Print to image - XPSDrv render filter

    dllentry.cpp - DLL entry points and class factory
--*/
#include "precomp.h"
#include "WppTrace.h"
#include "Exception.h"
#include "filtertypes.h"
#include "UnknownBase.h"
#include "xpsrasfilter.h"


//
// The render filter class ID. It must match the CLSID in
// driver/ptimage-pipelineconfig.xml.
//
static const CLSID CLSID_PtImageFilter =
    { 0x8d1a7c42, 0x3b6e, 0x4f90, { 0xa2, 0xd5, 0x6c, 0x1e, 0x9b, 0x47, 0xf3, 0x08 } };

namespace
{

volatile LONG g_serverLocks = 0;

class ClassFactory : public xpsrasfilter::UnknownBase<IClassFactory>
{
public:
    HRESULT STDMETHODCALLTYPE
    CreateInstance(
        _In_opt_ IUnknown *pUnkOuter,
        _In_     REFIID    riid,
        _Outptr_ void    **ppv
        )
    {
        if (ppv == NULL)
        {
            return E_POINTER;
        }

        *ppv = NULL;

        if (pUnkOuter != NULL)
        {
            return CLASS_E_NOAGGREGATION;
        }

        xpsrasfilter::PtImageFilter *pFilter =
            new (std::nothrow) xpsrasfilter::PtImageFilter();

        if (pFilter == NULL)
        {
            return E_OUTOFMEMORY;
        }

        HRESULT hr = pFilter->QueryInterface(riid, ppv);

        pFilter->Release();

        return hr;
    }

    HRESULT STDMETHODCALLTYPE
    LockServer(
        _In_ BOOL bLock
        )
    {
        if (bLock)
        {
            ::InterlockedIncrement(&g_serverLocks);
        }
        else
        {
            ::InterlockedDecrement(&g_serverLocks);
        }

        return S_OK;
    }
};

} // anonymous namespace

extern "C"
BOOL WINAPI
DllMain(
    _In_     HINSTANCE hInst,
    _In_     DWORD     dwReason,
    _In_opt_ PVOID     pReserved
    )
{
    switch (dwReason)
    {
    case DLL_PROCESS_ATTACH:
        ::DisableThreadLibraryCalls(hInst);
        break;

    case DLL_PROCESS_DETACH:
        break;

    default:
        break;
    }

    UNREFERENCED_PARAMETER(hInst);
    UNREFERENCED_PARAMETER(pReserved);

    return TRUE;
}

extern "C" HRESULT STDMETHODCALLTYPE
DllCanUnloadNow()
{
    if (g_serverLocks != 0 || xpsrasfilter::PtImageFilter::ms_numObjects != 0)
    {
        return S_FALSE;
    }

    return S_OK;
}

extern "C" HRESULT STDMETHODCALLTYPE
DllGetClassObject(
    _In_  REFCLSID rclsid,
    _In_  REFIID   riid,
    _Out_ void   **ppv
    )
{
    if (ppv == NULL)
    {
        return E_POINTER;
    }

    *ppv = NULL;

    if (!::IsEqualCLSID(rclsid, CLSID_PtImageFilter))
    {
        return CLASS_E_CLASSNOTAVAILABLE;
    }

    ClassFactory *pFactory = new (std::nothrow) ClassFactory();

    if (pFactory == NULL)
    {
        return E_OUTOFMEMORY;
    }

    HRESULT hr = pFactory->QueryInterface(riid, ppv);

    pFactory->Release();

    return hr;
}
