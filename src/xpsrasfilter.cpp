/*++
    Print to image - XPSDrv render filter

    xpsrasfilter.cpp - IPrintPipelineFilter implementation
--*/
#include "precomp.h"
#include "WppTrace.h"
#include "Exception.h"
#include "filtertypes.h"
#include "OMConvertor.h"
#include "PThandler.h"
#include "xpsrasfilter.h"
#include "rasinterface.h"


namespace xpsrasfilter
{

LONG PtImageFilter::ms_numObjects = 0;

//
// DPI used when the print ticket does not provide one
//
const FLOAT c_defaultDPI = 300.0f;

PtImageFilter::PtImageFilter() :
    m_cRef(1)
{
    m_pLiveness.Attach(new FilterLiveness());

    ::InterlockedIncrement(&ms_numObjects);
}

PtImageFilter::~PtImageFilter()
{
    ::InterlockedDecrement(&ms_numObjects);
}

HRESULT STDMETHODCALLTYPE
PtImageFilter::QueryInterface(
    _In_  REFIID   riid,
    _Out_ void   **ppv
    )
{
    if (ppv == NULL)
    {
        return E_POINTER;
    }

    *ppv = NULL;

    if (riid == IID_IUnknown)
    {
        *ppv = static_cast<IPrintPipelineFilter *>(this);
    }
    else if (riid == __uuidof(IPrintPipelineFilter))
    {
        *ppv = static_cast<IPrintPipelineFilter *>(this);
    }
    else
    {
        return E_NOINTERFACE;
    }

    AddRef();

    return S_OK;
}

ULONG STDMETHODCALLTYPE
PtImageFilter::AddRef()
{
    return static_cast<ULONG>(::InterlockedIncrement(&m_cRef));
}

ULONG STDMETHODCALLTYPE
PtImageFilter::Release()
{
    ULONG cRef = static_cast<ULONG>(::InterlockedDecrement(&m_cRef));

    if (cRef == 0)
    {
        delete this;
    }

    return cRef;
}

HRESULT STDMETHODCALLTYPE
PtImageFilter::InitializeFilter(
    _In_ IInterFilterCommunicator       *pICommunicator,
    _In_ IPrintPipelinePropertyBag      *pIPropertyBag,
    _In_ IPrintPipelineManagerControl   *pIPipelineControl
    )
{
    if (pICommunicator    == NULL ||
        pIPropertyBag     == NULL ||
        pIPipelineControl == NULL)
    {
        return E_POINTER;
    }

    HRESULT hr = S_OK;

    ptimage::FilterLog(L"InitializeFilter: communicator=%p bag=%p", pICommunicator, pIPropertyBag);

    try
    {
        InitializeFilter_throws(pICommunicator, pIPropertyBag);
    }
    CATCH_VARIOUS(hr);

    if (FAILED(hr))
    {
        ptimage::FilterLogError(L"InitializeFilter: failed hr=0x%08X", hr);
    }

    return hr;
}

void
PtImageFilter::InitializeFilter_throws(
    const IInterFilterCommunicator_t   &pICommunicator,
    const IPrintPipelinePropertyBag_t  &pIPropertyBag
    )
{
    m_pCommunicator = pICommunicator;
    m_pPropertyBag = pIPropertyBag;

    THROW_ON_FAILED_HRESULT(
        pICommunicator->RequestReader(reinterpret_cast<void **>(&m_pReader))
        );

    THROW_ON_FAILED_HRESULT(
        pICommunicator->RequestWriter(reinterpret_cast<void **>(&m_pWriter))
        );

    //
    // Verify that the interfaces match the pipeline configuration:
    // IXpsDocumentProvider in, IPrintWriteStream out.
    //
    {
        IXpsDocumentProvider_t pReaderCheck;
        IPrintWriteStream_t    pWriterCheck;

        THROW_ON_FAILED_HRESULT(m_pReader.QueryInterface(&pReaderCheck));
        THROW_ON_FAILED_HRESULT(m_pWriter.QueryInterface(&pWriterCheck));
    }
}

HRESULT STDMETHODCALLTYPE
PtImageFilter::ShutdownOperation()
{
    if (m_pLiveness != NULL)
    {
        m_pLiveness->Cancel();
    }

    return S_OK;
}

HRESULT STDMETHODCALLTYPE
PtImageFilter::StartOperation()
{
    HRESULT hr = S_OK;

    ptimage::FilterLog(L"StartOperation: begin");

    try
    {
        StartOperation_throws();
    }
    CATCH_VARIOUS(hr);

    if (FAILED(hr))
    {
        ptimage::FilterLogError(L"StartOperation: failed hr=0x%08X", hr);
    }
    else
    {
        ptimage::FilterLog(L"StartOperation: completed");
    }

    if (m_pWriter != NULL)
    {
        m_pWriter->Close();
    }

    return hr;
}

void
PtImageFilter::StartOperation_throws()
{
    SafeCoInit coInit;

    //
    // XPS Object Model factory (converts IFixedPage to IXpsOMPage)
    //
    IXpsOMObjectFactory_t pOMFactory;

    THROW_ON_FAILED_HRESULT(
        ::CoCreateInstance(
            __uuidof(XpsOMObjectFactory),
            NULL,
            CLSCTX_INPROC_SERVER,
            __uuidof(IXpsOMObjectFactory),
            reinterpret_cast<LPVOID *>(&pOMFactory)
            )
        );

    //
    // OPC factory (creates part URIs / part resources for the OM conversion)
    //
    IOpcFactory_t pOpcFactory;

    THROW_ON_FAILED_HRESULT(
        ::CoCreateInstance(
            __uuidof(OpcFactory),
            NULL,
            CLSCTX_INPROC_SERVER,
            __uuidof(IOpcFactory),
            reinterpret_cast<LPVOID *>(&pOpcFactory)
            )
        );

    //
    // Print ticket handler: provides the destination DPI. Failure here is not
    // fatal - the default DPI is used instead.
    //
    PrintTicketHandler_t pPThandler;

    try
    {
        pPThandler = PrintTicketHandler::CreatePrintTicketHandler(m_pPropertyBag);
    }
    catch (...)
    {
        pPThandler.reset();
    }

    ptimage::ImageWriter writer(m_pPropertyBag, m_pWriter);

    ptimage::FilterLog(L"StartOperation: factories created, reading parts");

    IUnknown_t pUnk;

    THROW_ON_FAILED_HRESULT(m_pReader->GetXpsPart(&pUnk));

    while (m_pLiveness->IsAlive() && pUnk != NULL)
    {
        IFixedPage_t             pFP;
        IFixedDocument_t         pFD;
        IFixedDocumentSequence_t pFDS;

        if (SUCCEEDED(pUnk.QueryInterface(&pFP)))
        {
            FLOAT dpi = c_defaultDPI;

            if (pPThandler.get() != NULL)
            {
                try
                {
                    pPThandler->ProcessPart(pFP);

                    ParametersFromPrintTicket ticketParams =
                        pPThandler->GetMergedPrintTicketParams();

                    if (ticketParams.destDPI >= 72.0f &&
                        ticketParams.destDPI <= 1200.0f)
                    {
                        dpi = ticketParams.destDPI;
                    }
                }
                catch (...)
                {
                    dpi = c_defaultDPI;
                }
            }

            IXpsOMPage_t pOMPage =
                CreateXpsOMPageFromIFixedPage(pFP, pOMFactory, pOpcFactory);

            XPS_SIZE pageSize = { 0, 0 };

            if (pOMPage != NULL)
            {
                pOMPage->GetPageDimensions(&pageSize);
            }

            ptimage::FilterLog(
                L"page %lu: %ux%u xps units, dpi=%.2f",
                writer.GetPageCount() + 1,
                static_cast<unsigned>(pageSize.width),
                static_cast<unsigned>(pageSize.height),
                dpi
                );

            writer.AddPage(pOMPage, dpi, m_pLiveness);

            ptimage::FilterLog(L"page %lu: written", writer.GetPageCount());
        }
        else if (SUCCEEDED(pUnk.QueryInterface(&pFD)))
        {
            if (pPThandler.get() != NULL)
            {
                try
                {
                    pPThandler->ProcessPart(pFD);
                }
                catch (...)
                {
                }
            }
        }
        else if (SUCCEEDED(pUnk.QueryInterface(&pFDS)))
        {
            if (pPThandler.get() != NULL)
            {
                try
                {
                    pPThandler->ProcessPart(pFDS);
                }
                catch (...)
                {
                }
            }
        }

        pUnk.Release();

        THROW_ON_FAILED_HRESULT(m_pReader->GetXpsPart(&pUnk));
    }

    writer.FinishDocument();

    ptimage::FilterLog(L"StartOperation: %lu page(s) written", writer.GetPageCount());
}

} // namespace xpsrasfilter
