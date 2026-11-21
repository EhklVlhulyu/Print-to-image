/*++
    Print to image - XPSDrv render filter

    rasinterface.h - rasterization (XpsRasterService) + WIC encoding + output
--*/
#pragma once

#include <string>
#include <vector>

#include "filtertypes.h"
#include "xpsrasfilter.h"

namespace ptimage
{

//
// Best-effort diagnostic log. The print pipeline runs the filter in a
// restricted process, so every failure is silent unless the filter writes it
// somewhere. The log lands in the first writable location out of
// %ProgramData%\print-to-images\logs, %LOCALAPPDATA%\print-to-images\logs,
// %TEMP% and the output folder the user picked.
//
void
FilterLog(
    const wchar_t *format,
    ...
    );

//
// Records the message and flushes the buffered history to the log file. Only
// errors create a log file; successful jobs leave no trace on disk.
//
void
FilterLogError(
    const wchar_t *format,
    ...
    );

enum ImageOutputFormat
{
    ImageFormatPng = 0,
    ImageFormatJpeg,
    ImageFormatBmp
};

class ImageWriter
{
public:
    ImageWriter(
        const IPrintPipelinePropertyBag_t &pPropertyBag,
        const IPrintWriteStream_t         &pWriteStream
        );

    ~ImageWriter();

    //
    // Rasterizes one page and appends it to the output.
    //
    void
    AddPage(
        const IXpsOMPage_t                    &pPage,
        FLOAT                                  dpi,
        const FilterLiveness_t  &pLiveness
        );

    //
    // Commits the last frame / copies the produced file to the output sink.
    //
    void
    FinishDocument();

    ULONG
    GetPageCount() const
    {
        return m_pageCount;
    }

    //
    // Target band size; 16MB of 32bpp pixels
    //
    const static LONG ms_targetBandSize = 1024 * 1024 * 16;

private:
    ImageWriter(const ImageWriter &);
    ImageWriter &operator=(const ImageWriter &);

    void
    InitFactories();

    void
    ReadOutputTarget();

    //
    // Per-page rasterization + encoding
    //
    void
    RasterizePage(
        const IXpsOMPage_t                    &pPage,
        FLOAT                                  dpi,
        const FilterLiveness_t  &pLiveness
        );

    //
    // Creates the encoder/frame for the page about to be written.
    //
    void
    BeginPage(
        UINT    pixelWidth,
        UINT    pixelHeight,
        FLOAT   dpi
        );

    void
    EndPage();

    void
    EncodeBand(
        IWICBitmap *pBand,
        UINT        bandHeight
        );

    //
    // Streams the finished page to the spooler write stream and returns the
    // number of bytes that reached the caller.
    //
    ULONG
    PublishStream(
        IStream_t &pSource
        );

    void
    WriteStreamToFile(
        IStream_t          &pSource,
        const std::wstring &path
        );

    std::wstring
    MakePageFilePath(
        ULONG pageIndex
        );

    void
    OpenFileStream(
        const std::wstring &path,
        IStream_t          &pStream
        );

    //
    // Same as OpenFileStream but reports failure through the return value so
    // the caller can fall back to an in-memory stream.
    //
    bool
    TryOpenFileStream(
        const std::wstring &path,
        IStream_t          &pStream
        );

    IPrintPipelinePropertyBag_t m_pPropertyBag;
    IPrintWriteStream_t         m_pWriteStream;

    IWICImagingFactory_t        m_pWIC;
    IXpsRasterizationFactory_t  m_pRasFactory;

    ImageOutputFormat           m_format;
    std::wstring                m_outputPath;      // XPS_FP_OUTPUT_FILE, may be empty
    std::wstring                m_outputDir;
    std::wstring                m_outputBaseName;
    std::wstring                m_outputExtension; // includes the dot

    ULONG                       m_pageCount;

    //
    // Single image (PNG/JPEG/BMP) state, valid for the page being written
    //
    IWICBitmapEncoder_t         m_pEncoder;
    IStream_t                   m_pPageStream;
    IStream_t                   m_pageMemory;      // used when no file can be created
    std::wstring                m_pageFilePath;    // set when written directly

    IWICBitmapFrameEncode_t     m_pFrame;

    HANDLE                      m_userToken;

    UINT                        m_pageWidth;
    UINT                        m_pageHeight;
    WICPixelFormatGUID          m_pixelFormat;

    std::vector<BYTE>           m_bandBuffer;
};

} // namespace ptimage
