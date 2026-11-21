/*++
    Print to image - XPSDrv render filter

    rasinterface.cpp - page rasterization, image encoding and output
--*/
#include "precomp.h"
#include "WppTrace.h"
#include "Exception.h"
#include "filtertypes.h"
#include "xpsrasfilter.h"
#include "rasinterface.h"

#include <shlwapi.h>
#include <stdarg.h>
#include <stdio.h>
#include <wchar.h>


namespace ptimage
{

namespace
{

//
// Rough upper bound for a single page: about 400 megapixels. Anything larger
// is scaled down to keep the WIC encoder and the band buffers reasonable.
//
const double c_maxPagePixels = 400.0 * 1000.0 * 1000.0;

const ULONG c_copyChunkSize = 64 * 1024;

const wchar_t *
DefaultExtension(ImageOutputFormat format)
{
    switch (format)
    {
    case ImageFormatJpeg: return L"jpg";
    case ImageFormatBmp:  return L"bmp";
    case ImageFormatPng:
    default:              return L"png";
    }
}

GUID
ContainerFormat(ImageOutputFormat format)
{
    switch (format)
    {
    case ImageFormatJpeg: return GUID_ContainerFormatJpeg;
    case ImageFormatBmp:  return GUID_ContainerFormatBmp;
    case ImageFormatPng:
    default:              return GUID_ContainerFormatPng;
    }
}

ImageOutputFormat
FormatFromExtension(const std::wstring &extension)
{
    if (extension == L".jpg" ||
        extension == L".jpeg" ||
        extension == L".jpe")
    {
        return ImageFormatJpeg;
    }

    if (extension == L".bmp" ||
        extension == L".dib")
    {
        return ImageFormatBmp;
    }

    return ImageFormatPng;
}

//
// Returns the lower-cased extension including the leading dot, or an empty
// string when the path has none.
//
std::wstring
GetLowerExtension(const std::wstring &path)
{
    std::wstring::size_type dot = path.find_last_of(L'.');
    std::wstring::size_type sep = path.find_last_of(L"\\/:.");

    if (dot == std::wstring::npos)
    {
        return L"";
    }

    //
    // A dot in a directory name ("C:\a.b\file") is not an extension.
    //
    if (sep != std::wstring::npos && sep != dot)
    {
        return L"";
    }

    std::wstring extension = path.substr(dot);

    for (std::wstring::size_type i = 0; i < extension.size(); ++i)
    {
        extension[i] = static_cast<wchar_t>(::towlower(extension[i]));
    }

    return extension;
}

std::wstring
GetDirectoryPart(const std::wstring &path)
{
    std::wstring::size_type sep = path.find_last_of(L"\\/");

    if (sep == std::wstring::npos)
    {
        return L"";
    }

    return path.substr(0, sep);
}

std::wstring
GetFileNamePart(const std::wstring &path)
{
    std::wstring::size_type sep = path.find_last_of(L"\\/");

    if (sep == std::wstring::npos)
    {
        return path;
    }

    return path.substr(sep + 1);
}

std::wstring
GetTempDirectory()
{
    wchar_t directory[MAX_PATH + 1] = { 0 };

    DWORD length = ::GetTempPathW(ARRAYSIZE(directory) - 1, directory);

    if (length == 0 || length >= ARRAYSIZE(directory) - 1)
    {
        THROW_LAST_ERROR();
    }

    return directory;
}

} // anonymous namespace

namespace
{

//
// Diagnostic log state.
//
// The print pipeline runs this DLL inside a restricted process where nothing
// is visible, so a diagnostic trail is useful - but a file is only created
// when something goes wrong. Informational messages are kept in a small
// in-memory ring buffer; FilterLogError() flushes that history plus the error
// itself to %ProgramData%\Print to image (or a fallback location).
//
SRWLOCK       g_logLock = SRWLOCK_INIT;
HANDLE        g_logFile = INVALID_HANDLE_VALUE;
bool          g_logOpened = false;
ULONGLONG     g_logBytes = 0;

const ULONGLONG c_maxLogBytes = 1024 * 1024;

const int c_logRingEntries = 64;
const int c_logRingChars = 512;

struct LogRingEntry
{
    SYSTEMTIME time;
    DWORD      processId;
    DWORD      threadId;
    wchar_t    text[c_logRingChars];
};

LogRingEntry g_logRing[c_logRingEntries];
int          g_logRingCount = 0;
int          g_logRingNext = 0;


bool
EnsureDirectoryExists(
    const std::wstring &directory
    )
{
    if (directory.empty())
    {
        return false;
    }

    if (::CreateDirectoryW(directory.c_str(), NULL))
    {
        return true;
    }

    if (::GetLastError() == ERROR_ALREADY_EXISTS)
    {
        return true;
    }

    //
    // Create the parent first (e.g. %ProgramData%\print-to-images) and retry.
    //
    std::wstring::size_type separator = directory.find_last_of(L"\\/");

    if (separator == std::wstring::npos || separator <= 2)
    {
        return false;
    }

    if (!EnsureDirectoryExists(directory.substr(0, separator)))
    {
        return false;
    }

    if (::CreateDirectoryW(directory.c_str(), NULL))
    {
        return true;
    }

    return ::GetLastError() == ERROR_ALREADY_EXISTS;
}

void
AddCandidateDirectory(
    std::vector<std::wstring> &candidates,
    const wchar_t             *variable,
    const wchar_t             *suffix
    )
{
    wchar_t value[MAX_PATH + 1] = { 0 };

    DWORD length = ::GetEnvironmentVariableW(variable, value, ARRAYSIZE(value));

    if (length == 0 || length >= ARRAYSIZE(value))
    {
        return;
    }

    std::wstring directory = value;

    if (suffix != NULL)
    {
        directory += suffix;
    }

    candidates.push_back(directory);
}

HANDLE
OpenLogFile()
{
    std::vector<std::wstring> candidates;

    AddCandidateDirectory(candidates, L"ProgramData", L"\\Print to image");
    AddCandidateDirectory(candidates, L"LOCALAPPDATA", L"\\Print to image");

    try
    {
        candidates.push_back(GetTempDirectory());
    }
    catch (...)
    {
    }

    wchar_t name[MAX_PATH + 1] = { 0 };

    for (size_t index = 0; index < candidates.size(); index++)
    {
        const std::wstring &directory = candidates[index];

        if (!EnsureDirectoryExists(directory))
        {
            continue;
        }

        if (swprintf_s(
                name,
                ARRAYSIZE(name),
                L"%s\\filter-%lu.log",
                directory.c_str(),
                ::GetCurrentProcessId()
                ) < 0)
        {
            continue;
        }

        HANDLE file = ::CreateFileW(
                        name,
                        FILE_APPEND_DATA,
                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                        NULL,
                        OPEN_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL,
                        NULL
                        );

        if (file == INVALID_HANDLE_VALUE)
        {
            continue;
        }

        LARGE_INTEGER size = { 0 };

        if (::GetFileSizeEx(file, &size) && size.QuadPart == 0)
        {
            static const BYTE byteOrderMark[2] = { 0xFF, 0xFE };
            DWORD written = 0;

            ::WriteFile(file, byteOrderMark, sizeof(byteOrderMark), &written, NULL);
        }

        return file;
    }

    return INVALID_HANDLE_VALUE;
}

void
AppendRingEntry(
    const wchar_t *text
    )
{
    LogRingEntry &entry = g_logRing[g_logRingNext];

    ::GetLocalTime(&entry.time);
    entry.processId = ::GetCurrentProcessId();
    entry.threadId  = ::GetCurrentThreadId();

    if (text == NULL)
    {
        entry.text[0] = L'\0';
    }
    else
    {
        wcsncpy_s(entry.text, ARRAYSIZE(entry.text), text, _TRUNCATE);
    }

    g_logRingNext = (g_logRingNext + 1) % c_logRingEntries;

    if (g_logRingCount < c_logRingEntries)
    {
        g_logRingCount++;
    }
}

void
WriteLogLine(
    const wchar_t *text
    )
{
    if (g_logFile == INVALID_HANDLE_VALUE || g_logBytes >= c_maxLogBytes || text == NULL)
    {
        return;
    }

    size_t length = wcslen(text);

    if (length == 0)
    {
        return;
    }

    DWORD written = 0;
    DWORD bytes = static_cast<DWORD>(length * sizeof(wchar_t));

    if (::WriteFile(g_logFile, text, bytes, &written, NULL))
    {
        g_logBytes += written;
    }
}

//
// Writes the buffered history plus the error that triggered the flush. Called
// with g_logLock held.
//
void
WriteRingToFile(
    const wchar_t *reason
    )
{
    if (!g_logOpened)
    {
        g_logOpened = true;
        g_logFile = OpenLogFile();
    }

    if (g_logFile == INVALID_HANDLE_VALUE)
    {
        return;
    }

    wchar_t line[1400] = { 0 };

    SYSTEMTIME now = { 0 };
    ::GetLocalTime(&now);

    if (swprintf_s(
            line,
            ARRAYSIZE(line),
            L"\r\n===== %s at %04u-%02u-%02u %02u:%02u:%02u =====\r\n",
            reason != NULL ? reason : L"error",
            now.wYear, now.wMonth, now.wDay,
            now.wHour, now.wMinute, now.wSecond
            ) > 0)
    {
        WriteLogLine(line);
    }

    int first = g_logRingNext - g_logRingCount;

    if (first < 0)
    {
        first += c_logRingEntries;
    }

    for (int index = 0; index < g_logRingCount; index++)
    {
        const LogRingEntry &entry = g_logRing[(first + index) % c_logRingEntries];

        if (swprintf_s(
                line,
                ARRAYSIZE(line),
                L"%04u-%02u-%02u %02u:%02u:%02u.%03u [%lu:%lu] %s\r\n",
                entry.time.wYear, entry.time.wMonth, entry.time.wDay,
                entry.time.wHour, entry.time.wMinute, entry.time.wSecond,
                entry.time.wMilliseconds,
                entry.processId,
                entry.threadId,
                entry.text
                ) > 0)
        {
            WriteLogLine(line);
        }
    }
}

void
LogMessage(
    bool           isError,
    const wchar_t *format,
    va_list        arguments
    )
{
    wchar_t message[1024] = { 0 };

    if (format == NULL)
    {
        return;
    }

    if (_vsnwprintf_s(message, ARRAYSIZE(message), _TRUNCATE, format, arguments) <= 0)
    {
        return;
    }

    ::AcquireSRWLockExclusive(&g_logLock);

    AppendRingEntry(message);

    if (isError)
    {
        WriteRingToFile(L"error");

        //
        // Start a fresh history so a later error does not repeat these lines.
        //
        g_logRingCount = 0;
        g_logRingNext  = 0;
    }

    ::ReleaseSRWLockExclusive(&g_logLock);
}

} // anonymous namespace

void
FilterLog(
    const wchar_t *format,
    ...
    )
{
    if (format == NULL)
    {
        return;
    }

    va_list arguments;
    va_start(arguments, format);

    LogMessage(false, format, arguments);

    va_end(arguments);
}

void
FilterLogError(
    const wchar_t *format,
    ...
    )
{
    if (format == NULL)
    {
        return;
    }

    va_list arguments;
    va_start(arguments, format);

    LogMessage(true, format, arguments);

    va_end(arguments);
}


ImageWriter::ImageWriter(
    const IPrintPipelinePropertyBag_t &pPropertyBag,
    const IPrintWriteStream_t         &pWriteStream
    ) :
    m_pPropertyBag(pPropertyBag),
    m_pWriteStream(pWriteStream),
    m_format(ImageFormatPng),
    m_pageCount(0),
    m_pageWidth(0),
    m_pageHeight(0),
    m_userToken(NULL)
{
    InitFactories();
    ReadOutputTarget();
}

ImageWriter::~ImageWriter()
{
}

void
ImageWriter::InitFactories()
{
    //
    // WIC factory - encodes the rasterized bands.
    //
    THROW_ON_FAILED_HRESULT(
        ::CoCreateInstance(
            CLSID_WICImagingFactory,
            NULL,
            CLSCTX_INPROC_SERVER,
            __uuidof(IWICImagingFactory),
            reinterpret_cast<LPVOID *>(&m_pWIC)
            )
        );

    //
    // XPS rasterization service factory. The print pipeline places it in the
    // driver property bag (it is installed as a FilterServiceProvider).
    //
    SafeVariant varFactory;

    THROW_ON_FAILED_HRESULT(
        m_pPropertyBag->GetProperty(L"MS_IXpsRasterizationFactory", &varFactory)
        );

    if (varFactory.vt != VT_UNKNOWN || varFactory.punkVal == NULL)
    {
        THROW_ON_FAILED_HRESULT(E_NOINTERFACE);
    }

    THROW_ON_FAILED_HRESULT(
        varFactory.punkVal->QueryInterface(
            __uuidof(IXpsRasterizationFactory),
            reinterpret_cast<LPVOID *>(&m_pRasFactory)
            )
        );
}

void
ImageWriter::ReadOutputTarget()
{
    //
    // Destination file chosen by the user ("Save Print Output As" dialog).
    //
    SafeVariant varOutputFile;

    if (SUCCEEDED(m_pPropertyBag->GetProperty(XPS_FP_OUTPUT_FILE, &varOutputFile)) &&
        varOutputFile.vt == VT_BSTR &&
        varOutputFile.bstrVal != NULL)
    {
        m_outputPath = varOutputFile.bstrVal;
    }

    //
    // The user token is required to create additional pages in the directory
    // the user selected. Never free it - it belongs to the pipeline.
    //
    SafeVariant varToken;

    if (SUCCEEDED(m_pPropertyBag->GetProperty(XPS_FP_USER_TOKEN, &varToken)))
    {
        m_userToken = reinterpret_cast<HANDLE>(varToken.byref);
    }

    if (!m_outputPath.empty())
    {
        m_outputExtension = GetLowerExtension(m_outputPath);
        m_outputDir = GetDirectoryPart(m_outputPath);
        m_outputBaseName = GetFileNamePart(m_outputPath);

        std::wstring::size_type dot = m_outputBaseName.find_last_of(L'.');

        if (dot != std::wstring::npos)
        {
            m_outputBaseName = m_outputBaseName.substr(0, dot);
        }
    }

    m_format = FormatFromExtension(m_outputExtension);

    if (m_outputExtension.empty())
    {
        m_outputExtension = std::wstring(L".") + DefaultExtension(m_format);
    }

    FilterLog(
        L"ImageWriter: output='%s' dir='%s' format=%d extension='%s' token=%p",
        m_outputPath.c_str(),
        m_outputDir.c_str(),
        static_cast<int>(m_format),
        m_outputExtension.c_str(),
        m_userToken
        );
}

std::wstring
ImageWriter::MakePageFilePath(
    ULONG pageIndex
    )
{
    std::wstring directory = m_outputDir;

    if (directory.empty())
    {
        directory = GetTempDirectory();
    }

    std::wstring baseName = m_outputBaseName;

    if (baseName.empty())
    {
        baseName = L"print_to_image";
    }

    wchar_t path[MAX_PATH + 1] = { 0 };

    if (swprintf_s(
            path,
            ARRAYSIZE(path),
            L"%s\\%s-%lu%s",
            directory.c_str(),
            baseName.c_str(),
            pageIndex,
            m_outputExtension.c_str()
            ) < 0)
    {
        THROW_ON_FAILED_HRESULT(E_FAIL);
    }

    return path;
}

void
ImageWriter::OpenFileStream(
    const std::wstring &path,
    IStream_t          &pStream
    )
{    //
    // Create the file as the printing user where possible.
    //
    HRESULT hr = E_FAIL;

    if (m_userToken != NULL)
    {
        try
        {
            ScopeImpersonation impersonate(m_userToken);

            hr = ::SHCreateStreamOnFileEx(
                    path.c_str(),
                    STGM_CREATE | STGM_READWRITE | STGM_SHARE_DENY_WRITE,
                    FILE_ATTRIBUTE_NORMAL,
                    TRUE,
                    NULL,
                    &pStream
                    );
        }
        catch (...)
        {
            hr = E_FAIL;
        }
    }
    else
    {
        hr = ::SHCreateStreamOnFileEx(
                path.c_str(),
                STGM_CREATE | STGM_READWRITE | STGM_SHARE_DENY_WRITE,
                FILE_ATTRIBUTE_NORMAL,
                TRUE,
                NULL,
                &pStream
                );
    }

    THROW_ON_FAILED_HRESULT(hr);
}

bool
ImageWriter::TryOpenFileStream(
    const std::wstring &path,
    IStream_t          &pStream
    )
{
    try
    {
        OpenFileStream(path, pStream);
    }
    catch (...)
    {
        return false;
    }

    return pStream != NULL;
}

void
ImageWriter::AddPage(
    const IXpsOMPage_t                    &pPage,
    FLOAT                                  dpi,
    const FilterLiveness_t  &pLiveness
    )
{
    if (pPage == NULL)
    {
        return;
    }

    if (dpi < 72.0f || dpi > 1200.0f)
    {
        dpi = 300.0f;
    }

    //
    //
    // Without the destination file name (only the write stream is
    // available) there is no way to name additional pages, so only the
    // first page is produced.
    //
    if (m_pageCount > 0 && m_outputPath.empty())
    {
        WPP_LOG_ON_FAILED_HRESULT_WITH_TEXT(
            S_FALSE,
            L"Additional pages are skipped because the output file name is unknown."
            );

        return;
    }

    RasterizePage(pPage, dpi, pLiveness);

    ++m_pageCount;
}

void
ImageWriter::RasterizePage(
    const IXpsOMPage_t                    &pPage,
    FLOAT                                  dpi,
    const FilterLiveness_t  &pLiveness
    )
{
    //
    // Page size in XPS units (1/96 inch) -> device pixels at the requested
    // resolution.
    //
    XPS_SIZE pageSize = { 0, 0 };

    THROW_ON_FAILED_HRESULT(pPage->GetPageDimensions(&pageSize));

    if (pageSize.width <= 0.0f || pageSize.height <= 0.0f)
    {
        return;
    }

    FLOAT scale = dpi / xpsrasfilter::xpsDPI;

    double pixelWidth = pageSize.width * scale;
    double pixelHeight = pageSize.height * scale;

    double pixelCount = pixelWidth * pixelHeight;

    if (pixelCount > c_maxPagePixels)
    {
        double factor = ::sqrt(c_maxPagePixels / pixelCount);

        pixelWidth *= factor;
        pixelHeight *= factor;

        WPP_LOG_ON_FAILED_HRESULT_WITH_TEXT(
            S_FALSE,
            L"Page scaled down to stay within the supported image size."
            );
    }

    UINT width = static_cast<UINT>(pixelWidth + 0.5);
    UINT height = static_cast<UINT>(pixelHeight + 0.5);

    if (width == 0) width = 1;
    if (height == 0) height = 1;

    IXpsRasterizer_t pRasterizer;

    THROW_ON_FAILED_HRESULT(
        m_pRasFactory->CreateRasterizer(
            pPage,
            dpi,
            XPSRAS_RENDERING_MODE_ANTIALIASED,
            XPSRAS_RENDERING_MODE_ANTIALIASED,
            &pRasterizer
            )
        );

    //
    // The rasterizer returns 32bpp bands; the encoders take 24bpp.
    //
    BeginPage(width, height, dpi);

    UINT bandHeight = static_cast<UINT>(ms_targetBandSize / 4 / static_cast<LONG>(width));

    if (bandHeight == 0) bandHeight = 1;
    if (bandHeight > height) bandHeight = height;

    m_bandBuffer.resize(static_cast<size_t>(width) * bandHeight * 3);

    for (UINT y = 0; y < height; y += bandHeight)
    {
        if (pLiveness != NULL && !pLiveness->IsAlive())
        {
            THROW_ON_FAILED_HRESULT(HRESULT_FROM_WIN32(ERROR_PRINT_CANCELLED));
        }

        UINT band = (height - y < bandHeight) ? (height - y) : bandHeight;

        IWICBitmap_t pBand;

        THROW_ON_FAILED_HRESULT(
            pRasterizer->RasterizeRect(
                0,
                static_cast<INT>(y),
                static_cast<INT>(width),
                static_cast<INT>(band),
                pLiveness,
                &pBand
                )
            );

        EncodeBand(pBand, band);
    }

    EndPage();
}

void
ImageWriter::BeginPage(
    UINT    pixelWidth,
    UINT    pixelHeight,
    FLOAT   dpi
    )
{
    m_pFrame.Release();

    m_pageWidth = pixelWidth;
    m_pageHeight = pixelHeight;
    m_pixelFormat = GUID_WICPixelFormat24bppBGR;

    IWICBitmapEncoder_t pEncoder;
    IStream_t           pStream;

    m_pageFilePath.clear();
    m_pageMemory.Release();

    if (m_pageCount == 0)
    {
        //
        // First page: the spooler only gives us a write stream, so the page
        // is encoded into memory and copied to that stream. Encoding into a
        // temporary file first failed with E_ACCESSDENIED inside the print
        // pipeline (the filter cannot always create files next to the output),
        // and reading the file back can be blocked as well, so avoid files
        // altogether for the page that goes into the stream.
        //
        THROW_ON_FAILED_HRESULT(
            ::CreateStreamOnHGlobal(NULL, TRUE, &m_pageMemory)
            );

        pStream = m_pageMemory;
    }
    else
    {
        //
        // Additional pages cannot use the single output stream, so they
        // are written next to the chosen file as
        // "<name>-<page><ext>".
        //
        m_pageFilePath = MakePageFilePath(m_pageCount + 1);

        if (!TryOpenFileStream(m_pageFilePath, pStream))
        {
            FilterLog(
                L"BeginPage: cannot create '%s', encoding into memory instead",
                m_pageFilePath.c_str()
                );

            THROW_ON_FAILED_HRESULT(
                ::CreateStreamOnHGlobal(NULL, TRUE, &m_pageMemory)
                );

            pStream = m_pageMemory;
        }
    }

    //
    // Keep our own reference: the encoder is released at EndPage and the
    // file must be closed before it is published / reported to the caller.
    //
    m_pPageStream = pStream;

    FilterLog(
        L"BeginPage: %ux%u dpi=%.2f stream='%s'",
        pixelWidth,
        pixelHeight,
        dpi,
        m_pageFilePath.empty() ? L"(memory)" : m_pageFilePath.c_str()
        );

    THROW_ON_FAILED_HRESULT(
        m_pWIC->CreateEncoder(
            ContainerFormat(m_format),
            NULL,
            &pEncoder
            )
        );

    THROW_ON_FAILED_HRESULT(
        pEncoder->Initialize(pStream, WICBitmapEncoderNoCache)
        );

    m_pEncoder = pEncoder;

    IWICBitmapFrameEncode_t pFrame;
    IPropertyBag2_t          pOptions;

    THROW_ON_FAILED_HRESULT(pEncoder->CreateNewFrame(&pFrame, &pOptions));

    if (m_format == ImageFormatJpeg && pOptions != NULL)
    {
        PROPBAG2 option = { 0 };
        option.pstrName = const_cast<LPOLESTR>(L"ImageQuality");

        VARIANT value;
        ::VariantInit(&value);
        value.vt = VT_R4;
        value.fltVal = 0.9f;

        HRESULT hrQuality = pOptions->Write(1, &option, &value);

        ::VariantClear(&value);

        if (FAILED(hrQuality))
        {
            WPP_LOG_ON_FAILED_HRESULT_WITH_TEXT(
                hrQuality,
                L"Failed to set the JPEG quality; using the encoder default."
                );
        }
    }

    THROW_ON_FAILED_HRESULT(pFrame->Initialize(pOptions));
    THROW_ON_FAILED_HRESULT(pFrame->SetSize(pixelWidth, pixelHeight));
    THROW_ON_FAILED_HRESULT(pFrame->SetResolution(dpi, dpi));

    WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;

    THROW_ON_FAILED_HRESULT(pFrame->SetPixelFormat(&format));

    m_pixelFormat = format;
    m_pFrame = pFrame;
}

void
ImageWriter::EncodeBand(
    IWICBitmap *pBand,
    UINT        bandHeight
    )
{
    //
    // The rasterizer hands us a premultiplied bitmap whose transparent areas
    // are not painted by the page itself. Composite every band over white so
    // the saved image has a white background instead of black/transparent.
    //
    const UINT   bgraStride = m_pageWidth * 4;
    const size_t bgraSize   = static_cast<size_t>(bgraStride) * bandHeight;

    if (m_bandBuffer.size() < bgraSize)
    {
        m_bandBuffer.resize(bgraSize);
    }

    CComPtr<IWICFormatConverter> pConverter;

    THROW_ON_FAILED_HRESULT(m_pWIC->CreateFormatConverter(&pConverter));

    THROW_ON_FAILED_HRESULT(
        pConverter->Initialize(
            pBand,
            GUID_WICPixelFormat32bppPBGRA,
            WICBitmapDitherTypeNone,
            NULL,
            0.0,
            WICBitmapPaletteTypeCustom
            )
        );

    THROW_ON_FAILED_HRESULT(
        pConverter->CopyPixels(
            NULL,
            bgraStride,
            static_cast<UINT>(bgraSize),
            &m_bandBuffer[0]
            )
        );

    BYTE *pixels = &m_bandBuffer[0];

    for (size_t offset = 0; offset < bgraSize; offset += 4)
    {
        const DWORD alpha = pixels[offset + 3];

        if (alpha == 0xFF)
        {
            continue;
        }

        if (alpha == 0)
        {
            pixels[offset + 0] = 0xFF;
            pixels[offset + 1] = 0xFF;
            pixels[offset + 2] = 0xFF;
            pixels[offset + 3] = 0xFF;
            continue;
        }

        //
        // Premultiplied alpha: result = source + white * (1 - alpha).
        //
        for (int channel = 0; channel < 3; ++channel)
        {
            const DWORD value = pixels[offset + channel];
            const DWORD blended = value + (255 * (255 - alpha) + 127) / 255;

            pixels[offset + channel] = static_cast<BYTE>(blended > 255 ? 255 : blended);
        }

        pixels[offset + 3] = 0xFF;
    }

    //
    // Hand the band to the encoder in the pixel format the frame asked for.
    //
    const bool isFourByteFormat =
        (m_pixelFormat == GUID_WICPixelFormat32bppBGRA ||
         m_pixelFormat == GUID_WICPixelFormat32bppPBGRA ||
         m_pixelFormat == GUID_WICPixelFormat32bppBGR);

    UINT  stride  = bgraStride;
    BYTE *pSource = pixels;

    if (!isFourByteFormat)
    {
        //
        // Compact BGRA to BGR in place; writing forward is safe because the
        // destination index is always smaller than the source index.
        //
        stride = m_pageWidth * 3;

        size_t destination = 0;

        for (size_t source = 0; source < bgraSize; source += 4)
        {
            pixels[destination + 0] = pixels[source + 0];
            pixels[destination + 1] = pixels[source + 1];
            pixels[destination + 2] = pixels[source + 2];

            destination += 3;
        }
    }

    const size_t bufferSize = static_cast<size_t>(stride) * bandHeight;

    THROW_ON_FAILED_HRESULT(
        m_pFrame->WritePixels(
            bandHeight,
            stride,
            static_cast<UINT>(bufferSize),
            pSource
            )
        );
}

void
ImageWriter::EndPage()
{
    FilterLog(
        L"EndPage: page %lu (%ux%u) page='%s'",
        m_pageCount,
        m_pageWidth,
        m_pageHeight,
        m_pageFilePath.c_str()
        );

    HRESULT hr = m_pFrame->Commit();

    if (FAILED(hr))
    {
        FilterLogError(L"EndPage: frame commit failed 0x%08X", hr);
        THROW_ON_FAILED_HRESULT(hr);
    }

    m_pFrame.Release();

    hr = m_pEncoder->Commit();

    if (FAILED(hr))
    {
        FilterLogError(L"EndPage: encoder commit failed 0x%08X", hr);
        THROW_ON_FAILED_HRESULT(hr);
    }

    m_pEncoder.Release();
    m_pPageStream.Release();

    if (m_pageMemory != NULL)
    {
        LARGE_INTEGER origin = { 0 };

        THROW_ON_FAILED_HRESULT(m_pageMemory->Seek(origin, STREAM_SEEK_SET, NULL));

        if (m_pageFilePath.empty())
        {
            ULONG published = PublishStream(m_pageMemory);

            FilterLog(L"EndPage: published page 1 from memory (%lu bytes)", published);
        }
        else
        {
            try
            {
                WriteStreamToFile(m_pageMemory, m_pageFilePath);

                FilterLog(
                    L"EndPage: page %lu written from memory to '%s'",
                    m_pageCount + 1,
                    m_pageFilePath.c_str()
                    );
            }
            catch (...)
            {
                FilterLogError(
                    L"EndPage: page %lu could not be written to '%s'",
                    m_pageCount + 1,
                    m_pageFilePath.c_str()
                    );
            }

            m_pageFilePath.clear();
        }

        m_pageMemory.Release();
    }
    else if (!m_pageFilePath.empty())
    {
        FilterLog(
            L"EndPage: page %lu written to '%s'",
            m_pageCount + 1,
            m_pageFilePath.c_str()
            );

        m_pageFilePath.clear();
    }
}

void
ImageWriter::FinishDocument()
{
    //
    // Every page was published as soon as its frame was committed, so
    // there is nothing to flush here.
    //
}

ULONG
ImageWriter::PublishStream(
    IStream_t &pSource
    )
{
    if (m_pWriteStream == NULL || pSource == NULL)
    {
        return 0;
    }

    std::vector<BYTE> buffer(c_copyChunkSize);
    ULONG bytesRead = 0;
    ULONG total = 0;

    for (;;)
    {
        HRESULT hr = pSource->Read(&buffer[0], c_copyChunkSize, &bytesRead);

        if (FAILED(hr))
        {
            FilterLogError(L"PublishStream: read failed 0x%08X after %lu bytes", hr, total);
            THROW_ON_FAILED_HRESULT(hr);
        }

        if (bytesRead == 0)
        {
            break;
        }

        ULONG bytesWritten = 0;

        hr = m_pWriteStream->WriteBytes(&buffer[0], bytesRead, &bytesWritten);

        if (FAILED(hr))
        {
            FilterLogError(L"PublishStream: WriteBytes failed 0x%08X after %lu bytes", hr, total);
            THROW_ON_FAILED_HRESULT(hr);
        }

        if (bytesWritten != bytesRead)
        {
            THROW_ON_FAILED_HRESULT(E_FAIL);
        }

        total += bytesWritten;
    }

    return total;
}

void
ImageWriter::WriteStreamToFile(
    IStream_t          &pSource,
    const std::wstring &path
    )
{
    if (pSource == NULL || path.empty())
    {
        return;
    }

    CComPtr<IStream> pFile;

    THROW_ON_FAILED_HRESULT(
        ::SHCreateStreamOnFileEx(
            path.c_str(),
            STGM_CREATE | STGM_READWRITE | STGM_SHARE_DENY_WRITE,
            FILE_ATTRIBUTE_NORMAL,
            TRUE,
            NULL,
            &pFile
            )
        );

    std::vector<BYTE> buffer(c_copyChunkSize);
    ULONG bytesRead = 0;

    for (;;)
    {
        THROW_ON_FAILED_HRESULT(pSource->Read(&buffer[0], c_copyChunkSize, &bytesRead));

        if (bytesRead == 0)
        {
            break;
        }

        ULONG bytesWritten = 0;

        HRESULT hr = pFile->Write(&buffer[0], bytesRead, &bytesWritten);

        if (FAILED(hr))
        {
            FilterLogError(L"WriteStreamToFile: write to '%s' failed 0x%08X", path.c_str(), hr);
            THROW_ON_FAILED_HRESULT(hr);
        }
    }

    THROW_ON_FAILED_HRESULT(pFile->Commit(STGC_DEFAULT));
}

} // namespace ptimage
