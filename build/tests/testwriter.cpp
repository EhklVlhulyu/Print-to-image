/*++
    print to images - offline test of ptimage::ImageWriter

    Feeds a synthetic rasterization factory + property bag + write stream into
    the real ImageWriter, so the WIC encoding / file naming / publishing logic
    can be exercised without installing the print driver.
--*/
#include "../../src/precomp.h"
#include "../../src/WppTrace.h"
#include "../../src/Exception.h"
#include "../../src/filtertypes.h"
#include "../../src/xpsrasfilter.h"
#include "../../src/rasinterface.h"

#include <stdio.h>

namespace
{

int g_failures = 0;

void
Check(
    bool        condition,
    const char *what
    )
{
    if (condition)
    {
        printf("  [ ok ] %s\n", what);
    }
    else
    {
        printf("  [FAIL] %s\n", what);
        ++g_failures;
    }
}

//
// Stand-in for XpsRasterService: paints a gradient whose blue channel encodes
// the absolute page row, so a wrong band offset shows up in the output.
//
bool g_transparentRasterizer = false;

class TestRasterizer : public IXpsRasterizer
{
public:
    explicit TestRasterizer(IWICImagingFactory *pWIC) : m_cRef(1), m_pWIC(pWIC)
    {
        m_pWIC->AddRef();
    }

    virtual ~TestRasterizer()
    {
        m_pWIC->Release();
    }

    HRESULT STDMETHODCALLTYPE
    QueryInterface(REFIID riid, void **ppv)
    {
        if (ppv == NULL)
        {
            return E_POINTER;
        }

        if (riid == __uuidof(IXpsRasterizer) || riid == IID_IUnknown)
        {
            *ppv = static_cast<IXpsRasterizer *>(this);
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

    HRESULT STDMETHODCALLTYPE
    SetMinimalLineWidth(INT /*width*/)
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE
    RasterizeRect(
        INT                                  x,
        INT                                  y,
        INT                                  width,
        INT                                  height,
        IXpsRasterizerNotificationCallback  *pCallback,
        IWICBitmap                        **ppBitmap
        )
    {
        if (pCallback != NULL)
        {
            HRESULT hr = pCallback->Continue();

            if (FAILED(hr))
            {
                return hr;
            }
        }

        if (x != 0)
        {
            printf("    (rasterizer: unexpected x=%d)\n", x);
        }

        CComPtr<IWICBitmap> pBitmap;

        WICPixelFormatGUID bitmapFormat =
            g_transparentRasterizer ? GUID_WICPixelFormat32bppPBGRA : GUID_WICPixelFormat32bppBGR;

        HRESULT hr = m_pWIC->CreateBitmap(
                        static_cast<UINT>(width),
                        static_cast<UINT>(height),
                        bitmapFormat,
                        WICBitmapCacheOnLoad,
                        &pBitmap);

        if (FAILED(hr))
        {
            return hr;
        }

        WICRect lockRect = { 0, 0, width, height };
        CComPtr<IWICBitmapLock> pLock;

        hr = pBitmap->Lock(&lockRect, WICBitmapLockWrite, &pLock);
        if (FAILED(hr))
        {
            return hr;
        }

        UINT stride = 0;
        UINT size = 0;
        BYTE *pData = NULL;

        hr = pLock->GetStride(&stride);
        if (SUCCEEDED(hr)) hr = pLock->GetDataPointer(&size, &pData);
        if (FAILED(hr))
        {
            return hr;
        }

        for (INT row = 0; row < height; ++row)
        {
            BYTE *pLine = pData + static_cast<size_t>(row) * stride;

            for (INT column = 0; column < width; ++column)
            {
                if (g_transparentRasterizer)
                {
                    //
                    // Transparent everywhere, plus one opaque red square in
                    // the top left corner of the page, so the compositing of
                    // the background can be observed.
                    //
                    const bool opaque = (y + row) < 200 && column < 200;

                    pLine[column * 4 + 0] = 0;
                    pLine[column * 4 + 1] = 0;
                    pLine[column * 4 + 2] = opaque ? 0xFF : 0x00;
                    pLine[column * 4 + 3] = opaque ? 0xFF : 0x00;
                    continue;
                }

                pLine[column * 4 + 0] = static_cast<BYTE>((y + row) & 0xFF);
                pLine[column * 4 + 1] = static_cast<BYTE>(column & 0xFF);
                pLine[column * 4 + 2] = 0x40;
                pLine[column * 4 + 3] = 0xFF;
            }
        }

        *ppBitmap = pBitmap.Detach();

        return S_OK;
    }

private:
    volatile LONG m_cRef;
    IWICImagingFactory *m_pWIC;
};

class TestRasterFactory : public IXpsRasterizationFactory
{
public:
    explicit TestRasterFactory(IWICImagingFactory *pWIC) : m_cRef(1), m_pWIC(pWIC)
    {
        m_pWIC->AddRef();
    }

    virtual ~TestRasterFactory()
    {
        m_pWIC->Release();
    }

    HRESULT STDMETHODCALLTYPE
    QueryInterface(REFIID riid, void **ppv)
    {
        if (ppv == NULL)
        {
            return E_POINTER;
        }

        if (riid == __uuidof(IXpsRasterizationFactory) || riid == IID_IUnknown)
        {
            *ppv = static_cast<IXpsRasterizationFactory *>(this);
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

    HRESULT STDMETHODCALLTYPE
    CreateRasterizer(
        IXpsOMPage * /*pPage*/,
        FLOAT /*dpi*/,
        XPSRAS_RENDERING_MODE /*nonText*/,
        XPSRAS_RENDERING_MODE /*text*/,
        IXpsRasterizer **ppRasterizer
        )
    {
        *ppRasterizer = new TestRasterizer(m_pWIC);

        return S_OK;
    }

private:
    volatile LONG m_cRef;
    IWICImagingFactory *m_pWIC;
};

class TestPropertyBag : public IPrintPipelinePropertyBag
{
public:
    TestPropertyBag() : m_cRef(1) { }

    HRESULT STDMETHODCALLTYPE
    QueryInterface(REFIID riid, void **ppv)
    {
        if (ppv == NULL)
        {
            return E_POINTER;
        }

        if (riid == __uuidof(IPrintPipelinePropertyBag) || riid == IID_IUnknown)
        {
            *ppv = static_cast<IPrintPipelinePropertyBag *>(this);
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

    HRESULT STDMETHODCALLTYPE
    AddProperty(const wchar_t *pszName, const VARIANT *pVar)
    {
        for (size_t i = 0; i < m_names.size(); ++i)
        {
            if (_wcsicmp(m_names[i].c_str(), pszName) == 0)
            {
                ::VariantClear(&m_values[i]);
                return ::VariantCopy(&m_values[i], const_cast<VARIANT *>(pVar));
            }
        }

        VARIANT value;
        ::VariantInit(&value);
        ::VariantCopy(&value, const_cast<VARIANT *>(pVar));

        m_names.push_back(pszName);
        m_values.push_back(value);

        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE
    GetProperty(const wchar_t *pszName, VARIANT *pVar)
    {
        if (pVar == NULL)
        {
            return E_POINTER;
        }

        ::VariantInit(pVar);

        for (size_t i = 0; i < m_names.size(); ++i)
        {
            if (_wcsicmp(m_names[i].c_str(), pszName) == 0)
            {
                return ::VariantCopy(pVar, &m_values[i]);
            }
        }

        return DISP_E_UNKNOWNNAME;
    }

    BOOL STDMETHODCALLTYPE
    DeleteProperty(const wchar_t * /*pszName*/)
    {
        return FALSE;
    }

private:
    volatile LONG m_cRef;
    std::vector<std::wstring> m_names;
    std::vector<VARIANT> m_values;
};

class TestWriteStream : public IPrintWriteStream
{
public:
    explicit TestWriteStream(const std::wstring &path) : m_cRef(1), m_file(NULL), m_bytes(0)
    {
        m_file = _wfopen(path.c_str(), L"wb");
    }

    virtual ~TestWriteStream()
    {
        if (m_file != NULL)
        {
            fclose(m_file);
        }
    }

    bool IsOpen() const { return m_file != NULL; }
    ULONGLONG BytesWritten() const { return m_bytes; }

    HRESULT STDMETHODCALLTYPE
    QueryInterface(REFIID riid, void **ppv)
    {
        if (ppv == NULL)
        {
            return E_POINTER;
        }

        if (riid == __uuidof(IPrintWriteStream) || riid == IID_IUnknown)
        {
            *ppv = static_cast<IPrintWriteStream *>(this);
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

    HRESULT STDMETHODCALLTYPE
    WriteBytes(const void *pvBuffer, ULONG cbBuffer, ULONG *pcbWritten)
    {
        if (m_file == NULL)
        {
            return E_FAIL;
        }

        size_t written = fwrite(pvBuffer, 1, cbBuffer, m_file);

        if (pcbWritten != NULL)
        {
            *pcbWritten = static_cast<ULONG>(written);
        }

        m_bytes += written;

        return (written == cbBuffer) ? S_OK : E_FAIL;
    }

    void STDMETHODCALLTYPE
    Close()
    {
        if (m_file != NULL)
        {
            fclose(m_file);
            m_file = NULL;
        }
    }

private:
    volatile LONG m_cRef;
    FILE *m_file;
    ULONGLONG m_bytes;
};

std::wstring
OutputDir()
{
    wchar_t buffer[MAX_PATH] = { 0 };

    ::GetModuleFileNameW(NULL, buffer, ARRAYSIZE(buffer));

    std::wstring path = buffer;
    std::wstring::size_type sep = path.find_last_of(L"\\/");

    return path.substr(0, sep + 1) + L"test-out";
}

bool
FileExists(const std::wstring &path)
{
    return ::GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

void
InspectImage(
    IWICImagingFactory *pWIC,
    const std::wstring &path,
    const wchar_t      *expectedFormat,
    UINT                expectedWidth,
    UINT                expectedHeight,
    UINT                expectedFrames
    )
{
    CComPtr<IWICBitmapDecoder> pDecoder;

    HRESULT hr = pWIC->CreateDecoderFromFilename(
                    path.c_str(),
                    NULL,
                    GENERIC_READ,
                    WICDecodeMetadataCacheOnDemand,
                    &pDecoder);

    if (FAILED(hr))
    {
        printf("    decode failed 0x%08X for %S\n", hr, path.c_str());
        ++g_failures;
        return;
    }

    GUID container = {};
    pDecoder->GetContainerFormat(&container);

    UINT frames = 0;
    pDecoder->GetFrameCount(&frames);

    CComPtr<IWICBitmapFrameDecode> pFrame;
    pDecoder->GetFrame(0, &pFrame);

    UINT width = 0;
    UINT height = 0;
    pFrame->GetSize(&width, &height);

    bool formatOk = false;

    if (_wcsicmp(expectedFormat, L"png") == 0) formatOk = (container == GUID_ContainerFormatPng);
    if (_wcsicmp(expectedFormat, L"jpg") == 0) formatOk = (container == GUID_ContainerFormatJpeg);
    if (_wcsicmp(expectedFormat, L"bmp") == 0) formatOk = (container == GUID_ContainerFormatBmp);
    if (_wcsicmp(expectedFormat, L"tif") == 0) formatOk = (container == GUID_ContainerFormatTiff);

    printf("    %S: frames=%u %ux%u %s\n",
           path.c_str(), frames, width, height, formatOk ? "format=ok" : "format=MISMATCH");

    Check(formatOk, "container format");
    Check(width == expectedWidth && height == expectedHeight, "pixel dimensions");
    Check(frames == expectedFrames, "frame count");
}

void
RunFormatTest(
    IWICImagingFactory        *pWIC,
    IXpsRasterizationFactory  *pFactory,
    const wchar_t             *extension,
    const wchar_t             *formatName,
    UINT                       pageCount,
    FLOAT                      dpi
    )
{
    printf("\n-- %S, %u page(s), %.0f dpi\n", extension, pageCount, dpi);

    const std::wstring outDir = OutputDir();
    const std::wstring path = outDir + L"\\out-" + formatName + L"." + extension;

    ::DeleteFileW(path.c_str());

    for (UINT page = 1; page <= 8; ++page)
    {
        wchar_t sibling[MAX_PATH] = { 0 };
        swprintf_s(sibling, ARRAYSIZE(sibling), L"%s\\out-%s-%u.%s",
                   outDir.c_str(), formatName, page, extension);
        ::DeleteFileW(sibling);
    }

    CComPtr<IXpsOMObjectFactory> pOMFactory;

    if (FAILED(::CoCreateInstance(__uuidof(XpsOMObjectFactory), NULL, CLSCTX_INPROC_SERVER,
                                  __uuidof(IXpsOMObjectFactory), (void **)&pOMFactory)))
    {
        printf("    XpsOMObjectFactory unavailable\n");
        ++g_failures;
        return;
    }

    CComPtr<IOpcFactory> pOpcFactory;
    HRESULT hr = ::CoCreateInstance(__uuidof(OpcFactory), NULL, CLSCTX_INPROC_SERVER,
                                    __uuidof(IOpcFactory), (void **)&pOpcFactory);

    if (FAILED(hr))
    {
        printf("    OpcFactory unavailable 0x%08X\n", hr);
        ++g_failures;
        return;
    }

    CComPtr<IOpcPartUri> pPartUri;
    hr = pOpcFactory->CreatePartUri(L"/FixedDocument/Pages/1.fpage", &pPartUri);

    XPS_SIZE pageSize = { 8.5f * 96.0f, 11.0f * 96.0f };

    CComPtr<IXpsOMPage> pPage;
    hr = pOMFactory->CreatePage(&pageSize, L"en-US", pPartUri, &pPage);

    if (FAILED(hr))
    {
        printf("    CreatePage failed 0x%08X\n", hr);
        ++g_failures;
        return;
    }

    CComPtr<TestPropertyBag> pBag;
    pBag.Attach(new TestPropertyBag());

    VARIANT v;
    ::VariantInit(&v);
    v.vt = VT_UNKNOWN;
    v.punkVal = pFactory;
    pFactory->AddRef();
    pBag->AddProperty(L"MS_IXpsRasterizationFactory", &v);
    ::VariantClear(&v);

    ::VariantInit(&v);
    v.vt = VT_BSTR;
    v.bstrVal = ::SysAllocString(path.c_str());
    pBag->AddProperty(L"PrintOutputFileName", &v);
    ::VariantClear(&v);

    CComPtr<IPrintPipelinePropertyBag> pBagIface;
    pBag->QueryInterface(__uuidof(IPrintPipelinePropertyBag), reinterpret_cast<void **>(&pBagIface));

    CComPtr<TestWriteStream> pStream;
    pStream.Attach(new TestWriteStream(path));

    CComPtr<IPrintWriteStream> pStreamIface;
    pStream->QueryInterface(__uuidof(IPrintWriteStream), reinterpret_cast<void **>(&pStreamIface));

    Check(pStream->IsOpen(), "output stream opened");

    CComPtr<xpsrasfilter::FilterLiveness> pLiveness;
    pLiveness.Attach(new xpsrasfilter::FilterLiveness());

    {
        ptimage::ImageWriter writer(pBagIface, pStreamIface);

        for (UINT page = 0; page < pageCount; ++page)
        {
            writer.AddPage(pPage, dpi, pLiveness);
        }

        writer.FinishDocument();

        Check(writer.GetPageCount() == pageCount, "page count");
    }

    pStream->Close();

    UINT expectedWidth = static_cast<UINT>(pageSize.width * dpi / 96.0f + 0.5f);
    UINT expectedHeight = static_cast<UINT>(pageSize.height * dpi / 96.0f + 0.5f);

    Check(FileExists(path), "index file written");
    Check(pStream->BytesWritten() > 0, "bytes published to the stream");

    InspectImage(pWIC, path, extension, expectedWidth, expectedHeight,
                1);

    if (pageCount > 1)
    {
        for (UINT page = 2; page <= pageCount; ++page)
        {
            wchar_t sibling[MAX_PATH] = { 0 };
            swprintf_s(sibling, ARRAYSIZE(sibling), L"%s\\out-%s-%u.%s",
                       outDir.c_str(), formatName, page, extension);

            Check(FileExists(sibling), "additional page file written");
            InspectImage(pWIC, sibling, extension, expectedWidth, expectedHeight, 1);
        }
    }
}

bool
ReadWholeFile(const std::wstring &path, std::string &out)
{
    HANDLE h = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    if (h == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    LARGE_INTEGER size = { 0 };

    if (!::GetFileSizeEx(h, &size) || size.QuadPart <= 0 || size.QuadPart > 32 * 1024 * 1024)
    {
        ::CloseHandle(h);
        return false;
    }

    out.resize(static_cast<size_t>(size.QuadPart));

    DWORD read = 0;
    const BOOL ok = ::ReadFile(h, &out[0], static_cast<DWORD>(out.size()), &read, NULL);

    ::CloseHandle(h);

    return ok != FALSE && read == out.size();
}

//
// Proves that the page markup the filter receives can be parsed into an
// IXpsOMPage. The spooler emits OpenXPS markup when the manifest asks for
// XpsFormat=OpenXPS, and IXpsOMObjectFactory only understands the classic XPS
// namespace - that mismatch is what produced 0 byte files in the field.
//
HRESULT
ParsePageMarkup(
    IXpsOMObjectFactory  *pOMFactory,
    IOpcFactory          *pOpcFactory,
    const std::string    &markup,
    const wchar_t        *partUriName,
    IXpsOMPage           **ppPage
    )
{
    *ppPage = NULL;

    CComPtr<IOpcPartUri> pPartUri;
    HRESULT hr = pOpcFactory->CreatePartUri(partUriName, &pPartUri);

    if (FAILED(hr))
    {
        return hr;
    }

    HGLOBAL hGlobal = ::GlobalAlloc(GMEM_MOVEABLE, markup.size());

    if (hGlobal == NULL)
    {
        return E_OUTOFMEMORY;
    }

    void *dest = ::GlobalLock(hGlobal);
    memcpy(dest, markup.data(), markup.size());
    ::GlobalUnlock(hGlobal);

    CComPtr<IStream> pMarkup;
    hr = ::CreateStreamOnHGlobal(hGlobal, TRUE, &pMarkup);

    if (FAILED(hr))
    {
        ::GlobalFree(hGlobal);
        return hr;
    }

    //
    // CreatePageFromStream does not accept a NULL resource collection.
    //
    CComPtr<IXpsOMPartResources> pResources;
    hr = pOMFactory->CreatePartResources(&pResources);

    if (FAILED(hr))
    {
        return hr;
    }

    return pOMFactory->CreatePageFromStream(pMarkup, pPartUri, pResources, FALSE, ppPage);
}

//
// Proves that the page markup the filter receives can be parsed into an
// IXpsOMPage. The spooler emits OpenXPS markup when the manifest asks for
// XpsFormat=OpenXPS, and IXpsOMObjectFactory only understands the classic XPS
// namespace - that mismatch is what produced 0 byte files in the field.
//
bool
ReadPixel(
    IWICImagingFactory     *pWIC,
    const std::wstring     &path,
    UINT                    x,
    UINT                    y,
    BYTE                    pixel[4]
    )
{
    CComPtr<IWICBitmapDecoder> pDecoder;

    HRESULT hr = pWIC->CreateDecoderFromFilename(
                    path.c_str(), NULL, GENERIC_READ,
                    WICDecodeMetadataCacheOnDemand, &pDecoder);

    if (FAILED(hr))
    {
        return false;
    }

    CComPtr<IWICBitmapFrameDecode> pFrame;

    hr = pDecoder->GetFrame(0, &pFrame);
    if (FAILED(hr))
    {
        return false;
    }

    CComPtr<IWICFormatConverter> pConverter;

    hr = pWIC->CreateFormatConverter(&pConverter);
    if (FAILED(hr))
    {
        return false;
    }

    hr = pConverter->Initialize(pFrame, GUID_WICPixelFormat32bppBGRA,
                                WICBitmapDitherTypeNone, NULL, 0.0,
                                WICBitmapPaletteTypeCustom);

    if (FAILED(hr))
    {
        return false;
    }

    WICRect rect = { static_cast<INT>(x), static_cast<INT>(y), 1, 1 };

    hr = pConverter->CopyPixels(&rect, 4, 4, pixel);

    return SUCCEEDED(hr);
}

void
RunWhiteBackgroundTest(
    IWICImagingFactory        *pWIC,
    IXpsRasterizationFactory  *pFactory
    )
{
    printf("\n-- white background\n");

    g_transparentRasterizer = true;
    RunFormatTest(pWIC, pFactory, L"png", L"white", 1, 150.0f);
    g_transparentRasterizer = false;

    const std::wstring path = OutputDir() + L"\\out-white.png";

    BYTE corner[4] = { 0, 0, 0, 0 };

    if (ReadPixel(pWIC, path, 400, 400, corner))
    {
        printf("    transparent area pixel B=%u G=%u R=%u\n",
               corner[0], corner[1], corner[2]);

        Check(corner[0] == 0xFF && corner[1] == 0xFF && corner[2] == 0xFF,
              "transparent areas are composited onto white");
    }
    else
    {
        Check(false, "read background pixel");
    }

    BYTE opaque[4] = { 0, 0, 0, 0 };

    if (ReadPixel(pWIC, path, 50, 50, opaque))
    {
        printf("    painted area pixel B=%u G=%u R=%u\n",
               opaque[0], opaque[1], opaque[2]);

        Check(opaque[0] == 0 && opaque[1] == 0 && opaque[2] == 0xFF,
              "painted pixels keep their colour");
    }
    else
    {
        Check(false, "read painted pixel");
    }
}

void
RunPageMarkupTest(
    IWICImagingFactory        *pWIC,
    IXpsRasterizationFactory  *pFactory,
    const std::wstring        &markupPath
    )
{
    printf("\n-- page markup: %S\n", markupPath.c_str());

    std::string markup;

    if (!ReadWholeFile(markupPath, markup))
    {
        printf("    [skip] cannot read the sample page\n");
        return;
    }

    const char *openXpsNs = "http://schemas.openxps.org/oxps/v1.0";
    const char *classicNs = "http://schemas.microsoft.com/xps/2005/06";

    const bool delivered = markup.find(openXpsNs) != std::string::npos;

    Check(delivered, "sample page uses the OpenXPS namespace (what XpsFormat=OpenXPS delivers)");

    CComPtr<IXpsOMObjectFactory> pOMFactory;

    if (FAILED(::CoCreateInstance(__uuidof(XpsOMObjectFactory), NULL, CLSCTX_INPROC_SERVER,
                                  __uuidof(IXpsOMObjectFactory), (void **)&pOMFactory)))
    {
        printf("    XpsOMObjectFactory unavailable\n");
        ++g_failures;
        return;
    }

    CComPtr<IOpcFactory> pOpcFactory;

    if (FAILED(::CoCreateInstance(__uuidof(OpcFactory), NULL, CLSCTX_INPROC_SERVER,
                                  __uuidof(IOpcFactory), (void **)&pOpcFactory)))
    {
        printf("    OpcFactory unavailable\n");
        ++g_failures;
        return;
    }

    CComPtr<IXpsOMPage> pPage;

    for (int pass = 0; pass < 2; ++pass)
    {
        std::string candidate = markup;

        if (pass == 1)
        {
            if (!delivered)
            {
                break;
            }

            candidate.replace(candidate.find(openXpsNs), strlen(openXpsNs), classicNs);

            //
            // This harness has no access to the font part the page points at
            // (the pipeline supplies it through CollectPageResources), so the
            // Glyphs element is dropped to keep the parse independent of it.
            //
            std::string::size_type glyphs = candidate.find("<Glyphs");

            if (glyphs != std::string::npos)
            {
                std::string::size_type glyphEnd = candidate.find("/>", glyphs);

                if (glyphEnd != std::string::npos)
                {
                    candidate.erase(glyphs, glyphEnd + 2 - glyphs);
                }
            }
        }

        const HRESULT hrPage = ParsePageMarkup(pOMFactory, pOpcFactory, candidate,
                                               L"/Documents/1/Pages/1.fpage", &pPage);

        printf("    %-26s CreatePageFromStream hr=0x%08X\n",
               (pass == 0) ? "as delivered (OpenXPS)" : "classic XPS namespace", hrPage);

        if (pass == 0)
        {
            Check(hrPage == (HRESULT)0x8052000C,
                  "OpenXPS markup is rejected with XPS_E_INVALID_MARKUP");

            pPage.Release();
        }
        else
        {
            Check(SUCCEEDED(hrPage), "classic XPS markup is accepted");
        }
    }

    if (pPage == NULL)
    {
        printf("    [skip] no page could be parsed\n");
        ++g_failures;
        return;
    }

    XPS_SIZE pageSize = { 0, 0 };
    pPage->GetPageDimensions(&pageSize);
    printf("    page size: %.2f x %.2f (1/96 inch)\n", pageSize.width, pageSize.height);

    const FLOAT dpi = 150.0f;
    const std::wstring path = OutputDir() + L"\\out-markup.png";

    ::DeleteFileW(path.c_str());

    CComPtr<TestPropertyBag> pBag;
    pBag.Attach(new TestPropertyBag());

    VARIANT v;
    ::VariantInit(&v);
    v.vt = VT_UNKNOWN;
    v.punkVal = pFactory;
    pFactory->AddRef();
    pBag->AddProperty(L"MS_IXpsRasterizationFactory", &v);
    ::VariantClear(&v);

    ::VariantInit(&v);
    v.vt = VT_BSTR;
    v.bstrVal = ::SysAllocString(path.c_str());
    pBag->AddProperty(L"PrintOutputFileName", &v);
    ::VariantClear(&v);

    CComPtr<IPrintPipelinePropertyBag> pBagIface;
    pBag->QueryInterface(__uuidof(IPrintPipelinePropertyBag), reinterpret_cast<void **>(&pBagIface));

    CComPtr<TestWriteStream> pStream;
    pStream.Attach(new TestWriteStream(path));

    CComPtr<IPrintWriteStream> pStreamIface;
    pStream->QueryInterface(__uuidof(IPrintWriteStream), reinterpret_cast<void **>(&pStreamIface));

    CComPtr<xpsrasfilter::FilterLiveness> pLiveness;
    pLiveness.Attach(new xpsrasfilter::FilterLiveness());

    {
        ptimage::ImageWriter writer(pBagIface, pStreamIface);

        writer.AddPage(pPage, dpi, pLiveness);
        writer.FinishDocument();

        Check(writer.GetPageCount() == 1, "markup page produced one image");
    }

    pStream->Close();

    UINT expectedWidth = static_cast<UINT>(pageSize.width * dpi / 96.0f + 0.5f);
    UINT expectedHeight = static_cast<UINT>(pageSize.height * dpi / 96.0f + 0.5f);

    Check(FileExists(path), "markup image written");
    Check(pStream->BytesWritten() > 0, "markup image published to the stream");

    InspectImage(pWIC, path, L"png", expectedWidth, expectedHeight, 1);
}


//
// The print pipeline may run the filter in a process that cannot create
// files in the destination folder. The writer must then fall back to an
// in-memory stream so the first page still reaches the spooler.
//
void
RunFallbackTest(
    IWICImagingFactory        *pWIC,
    IXpsRasterizationFactory  *pFactory
    )
{
    printf("\n-- unwritable output folder (memory fallback)\n");

    // A drive letter that does not exist, so creating the scratch file fails.
    const std::wstring path = L"Q:\\ptimage-fallback\\out.png";
    const std::wstring sink = OutputDir() + L"\\out-fallback.png";

    ::DeleteFileW(sink.c_str());

    CComPtr<IXpsOMObjectFactory> pOMFactory;

    if (FAILED(::CoCreateInstance(__uuidof(XpsOMObjectFactory), NULL, CLSCTX_INPROC_SERVER,
                                  __uuidof(IXpsOMObjectFactory), (void **)&pOMFactory)))
    {
        printf("    XpsOMObjectFactory unavailable\n");
        ++g_failures;
        return;
    }

    CComPtr<IOpcFactory> pOpcFactory;

    if (FAILED(::CoCreateInstance(__uuidof(OpcFactory), NULL, CLSCTX_INPROC_SERVER,
                                  __uuidof(IOpcFactory), (void **)&pOpcFactory)))
    {
        printf("    OpcFactory unavailable\n");
        ++g_failures;
        return;
    }

    CComPtr<IOpcPartUri> pPartUri;

    if (FAILED(pOpcFactory->CreatePartUri(L"/FixedDocument/Pages/1.fpage", &pPartUri)))
    {
        printf("    CreatePartUri failed\n");
        ++g_failures;
        return;
    }

    XPS_SIZE pageSize = { 612.0f, 792.0f };

    CComPtr<IXpsOMPage> pPage;

    if (FAILED(pOMFactory->CreatePage(&pageSize, L"en-US", pPartUri, &pPage)))
    {
        printf("    CreatePage failed\n");
        ++g_failures;
        return;
    }

    CComPtr<TestPropertyBag> pBag;
    pBag.Attach(new TestPropertyBag());

    VARIANT v;
    ::VariantInit(&v);
    v.vt = VT_UNKNOWN;
    v.punkVal = pFactory;
    pFactory->AddRef();
    pBag->AddProperty(L"MS_IXpsRasterizationFactory", &v);
    ::VariantClear(&v);

    ::VariantInit(&v);
    v.vt = VT_BSTR;
    v.bstrVal = ::SysAllocString(path.c_str());
    pBag->AddProperty(L"PrintOutputFileName", &v);
    ::VariantClear(&v);

    CComPtr<IPrintPipelinePropertyBag> pBagIface;
    pBag->QueryInterface(__uuidof(IPrintPipelinePropertyBag),
                         reinterpret_cast<void **>(&pBagIface));

    CComPtr<TestWriteStream> pStream;
    pStream.Attach(new TestWriteStream(sink));

    CComPtr<IPrintWriteStream> pWriterIface;
    pStream->QueryInterface(__uuidof(IPrintWriteStream),
                            reinterpret_cast<void **>(&pWriterIface));

    CComPtr<xpsrasfilter::FilterLiveness> pLiveness;
    pLiveness.Attach(new xpsrasfilter::FilterLiveness());

    ptimage::ImageWriter writer(pBagIface, pWriterIface);
    writer.AddPage(pPage, 150.0f, pLiveness);
    writer.FinishDocument();

    Check(FileExists(sink), "fallback page published to the stream");
    Check(pStream->BytesWritten() > 0, "fallback stream received bytes");
    Check(!FileExists(path), "no file was created in the unwritable folder");

    // Close the sink before decoding it.
    pWriterIface->Close();
    pWriterIface.Release();
    pStream.Release();

    InspectImage(pWIC, sink, L"png", 956, 1238, 1);
}

} // anonymous namespace

int
main()
{
    setvbuf(stdout, NULL, _IONBF, 0);

    HRESULT hr = ::CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    if (FAILED(hr))
    {
        printf("CoInitializeEx failed 0x%08X\n", hr);
        return 1;
    }

    ::CreateDirectoryW(OutputDir().c_str(), NULL);

    CComPtr<IWICImagingFactory> pWIC;

    if (FAILED(::CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                                  __uuidof(IWICImagingFactory), (void **)&pWIC)))
    {
        printf("WIC factory unavailable\n");
        return 1;
    }

    CComPtr<TestRasterFactory> pFactory;
    pFactory.Attach(new TestRasterFactory(pWIC));

    RunFormatTest(pWIC, pFactory, L"png", L"png", 1, 150.0f);
    RunFormatTest(pWIC, pFactory, L"jpg", L"jpg", 1, 150.0f);
    RunFormatTest(pWIC, pFactory, L"bmp", L"bmp", 1, 150.0f);
    RunFormatTest(pWIC, pFactory, L"png", L"png", 3, 150.0f);
    RunFormatTest(pWIC, pFactory, L"png", L"png2", 1, 300.0f);

    RunFallbackTest(pWIC, pFactory);

    {
        wchar_t moduleDir[MAX_PATH] = { 0 };
        ::GetModuleFileNameW(NULL, moduleDir, ARRAYSIZE(moduleDir));

        std::wstring sample = moduleDir;
        std::wstring::size_type sep = sample.find_last_of(L"\\/");
        sample = (sep == std::wstring::npos) ? std::wstring() : sample.substr(0, sep + 1);
        sample += L"tests\\data\\openxps-page.fpage";

        RunPageMarkupTest(pWIC, pFactory, sample);
    }

    RunWhiteBackgroundTest(pWIC, pFactory);

    printf("\n%d failure(s)\n", g_failures);
    fflush(stdout);

    pFactory.Release();
    pWIC.Release();

    ::CoUninitialize();

    printf("test complete\n");
    fflush(stdout);

    return g_failures == 0 ? 0 : 1;
}
