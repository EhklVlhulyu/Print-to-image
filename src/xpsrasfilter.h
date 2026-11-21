/*++
    Print to image - XPSDrv render filter

    xpsrasfilter.h - filter object and cancellation callback
--*/
#pragma once

#include "filtertypes.h"

namespace xpsrasfilter
{

//
// Cancellation / liveness callback given to the XPS rasterization service.
// The pipeline manager calls Cancel() from ShutdownOperation() (which runs on
// a different thread); the rasterizer polls Continue() between bands.
//
class FilterLiveness : public IXpsRasterizerNotificationCallback
{
public:
    FilterLiveness() :
        m_cRef(1),
        m_cancelled(FALSE)
    {
    }

    HRESULT STDMETHODCALLTYPE
    QueryInterface(
        _In_  REFIID  riid,
        _Out_ void  **ppv
        )
    {
        if (ppv == NULL)
        {
            return E_POINTER;
        }

        if (riid == IID_IUnknown ||
            riid == __uuidof(IXpsRasterizerNotificationCallback))
        {
            *ppv = static_cast<IXpsRasterizerNotificationCallback *>(this);
            AddRef();

            return S_OK;
        }

        *ppv = NULL;

        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE
    AddRef()
    {
        return static_cast<ULONG>(::InterlockedIncrement(&m_cRef));
    }

    ULONG STDMETHODCALLTYPE
    Release()
    {
        ULONG cRef = static_cast<ULONG>(::InterlockedDecrement(&m_cRef));

        if (cRef == 0)
        {
            delete this;
        }

        return cRef;
    }

    //
    // IXpsRasterizerNotificationCallback
    //
    HRESULT STDMETHODCALLTYPE
    Continue()
    {
        return m_cancelled ? E_ABORT : S_OK;
    }

    void
    Cancel()
    {
        m_cancelled = TRUE;
    }

    BOOL
    IsAlive() const
    {
        return !m_cancelled;
    }

private:
    ~FilterLiveness()
    {
    }

    volatile LONG  m_cRef;
    volatile BOOL  m_cancelled;
};

//
// The render filter itself.
//
class PtImageFilter : public IPrintPipelineFilter
{
public:
    static LONG ms_numObjects;

    PtImageFilter();
    virtual ~PtImageFilter();

    //
    // IUnknown
    //
    _Must_inspect_result_
    HRESULT STDMETHODCALLTYPE
    QueryInterface(
        _In_  REFIID   riid,
        _Out_ void   **ppv
        );

    ULONG STDMETHODCALLTYPE
    AddRef();

    ULONG STDMETHODCALLTYPE
    Release();

    //
    // IPrintPipelineFilter
    //
    _Must_inspect_result_
    HRESULT STDMETHODCALLTYPE
    InitializeFilter(
        _In_ IInterFilterCommunicator       *pICommunicator,
        _In_ IPrintPipelinePropertyBag      *pIPropertyBag,
        _In_ IPrintPipelineManagerControl   *pIPipelineControl
        );

    _Must_inspect_result_
    HRESULT STDMETHODCALLTYPE
    ShutdownOperation();

    _Must_inspect_result_
    HRESULT STDMETHODCALLTYPE
    StartOperation();

private:
    void
    InitializeFilter_throws(
        const IInterFilterCommunicator_t   &pICommunicator,
        const IPrintPipelinePropertyBag_t  &pIPropertyBag
        );

    void
    StartOperation_throws();

    PtImageFilter(const PtImageFilter &);
    PtImageFilter &operator=(const PtImageFilter &);

    volatile LONG                       m_cRef;

    IInterFilterCommunicator_t          m_pCommunicator;
    IPrintPipelinePropertyBag_t         m_pPropertyBag;

    IXpsDocumentProvider_t              m_pReader;
    IPrintWriteStream_t                 m_pWriter;

    FilterLiveness_t                    m_pLiveness;
};

} // namespace xpsrasfilter
