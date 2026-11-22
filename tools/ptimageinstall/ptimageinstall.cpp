// ---------------------------------------------------------------------------
//  ptimageinstall.exe - installation helper (no PowerShell involved)
//
//  usage:
//    ptimageinstall.exe install   <inf path> <driver name> <port> <log file>
//    ptimageinstall.exe uninstall <driver name> <log file>
//
//  install does, in order:
//    1. make sure the port exists (XcvData "AddPort"; failures are only logged)
//    2. AddPrinterDriverExW(level 3, APD_COPY_ALL_FILES) to register the v4 driver
//    3. AddPrinterW(level 2) to create the print queue
//  Every Win32 error code is appended to the log file (ANSI, same as NSIS).
// ---------------------------------------------------------------------------

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <winspool.h>
#include <setupapi.h>
#include <stdio.h>
#include <string>

// not exported by every SDK version
#ifndef APD_INSTALL_WARNED_DRIVER
#define APD_INSTALL_WARNED_DRIVER 0x00080000
#endif

static FILE *g_log = NULL;

static void Log(const wchar_t *format, ...)
{
    wchar_t buffer[2048];
    va_list args;
    va_start(args, format);
    _vsnwprintf_s(buffer, _countof(buffer), _TRUNCATE, format, args);
    va_end(args);

    if (g_log != NULL)
    {
        char line[4096];
        int n = ::WideCharToMultiByte(CP_ACP, 0, buffer, -1, line, sizeof(line) - 1, NULL, NULL);
        if (n > 1)
        {
            line[n - 1] = '\n';
            fwrite(line, 1, n, g_log);
            fflush(g_log);
        }
    }
}

// log "what failed error=0x%08X (<n>) <system message>"
static void LogWin32(const wchar_t *what, DWORD error)
{
    wchar_t *message = NULL;
    ::FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                         FORMAT_MESSAGE_IGNORE_INSERTS,
                     NULL,
                     error,
                     0,
                     reinterpret_cast<LPWSTR>(&message),
                     0,
                     NULL);

    std::wstring text;
    if (message != NULL)
    {
        text.assign(message);
        ::LocalFree(message);
        while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n'))
        {
            text.pop_back();
        }
    }

    Log(L"%s failed error=0x%08X (%lu) %s", what, error, error, text.c_str());
}

static const wchar_t *EnvName()
{
    SYSTEM_INFO si = {0};
    ::GetNativeSystemInfo(&si);
    return (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64)
        ? L"Windows x64"
        : L"Windows NT x86";
}

static void AddLocalPort(const wchar_t *port)
{
    // XcvData(AddPort) with a double-NUL terminated port name.
    // Prefer the PORTPROMPT monitor (the "save as" port) and fall back to Local Port.
    static const wchar_t *const monitors[] = { L"PORTPROMPT", L"Local Port" };

    HANDLE hXcv = NULL;
    PRINTER_DEFAULTSW defaults = {0};
    defaults.DesiredAccess = SERVER_ACCESS_ADMINISTER;

    for (int i = 0; i < _countof(monitors) && hXcv == NULL; i++)
    {
        std::wstring name(L",XcvMonitor ");
        name += monitors[i];
        if (!::OpenPrinterW(&name[0], &hXcv, &defaults))
        {
            std::wstring what(L"port: OpenPrinter(");
            what += monitors[i];
            what += L")";
            LogWin32(what.c_str(), ::GetLastError());
            hXcv = NULL;
        }
    }

    if (hXcv == NULL)
    {
        Log(L"port: no port monitor available, continuing without creating '%s'", port);
        return;
    }

    std::wstring data(port);
    data.push_back(L'\0');
    data.push_back(L'\0');

    DWORD needed = 0;
    DWORD status = 0;
    if (::XcvDataW(hXcv,
                   L"AddPort",
                   reinterpret_cast<BYTE *>(&data[0]),
                   static_cast<DWORD>(data.size() * sizeof(wchar_t)),
                   NULL,
                   0,
                   &needed,
                   &status))
    {
        Log(L"port: AddPort('%s') ok (status=%lu)", port, status);
    }
    else
    {
        DWORD error = ::GetLastError();
        if (error == ERROR_ALREADY_EXISTS || status == ERROR_ALREADY_EXISTS)
        {
            Log(L"port: '%s' already exists", port);
        }
        else
        {
            std::wstring what(L"port: AddPort('");
            what += port;
            what += L"')";
            LogWin32(what.c_str(), error);
            Log(L"port: AddPort status=%lu", status);
        }
    }

    ::ClosePrinter(hXcv);
}

static int Install(const wchar_t *infPath, const wchar_t *name, const wchar_t *port)
{
    const wchar_t *env = EnvName();
    Log(L"install: inf=%s", infPath);
    Log(L"install: name=%s port=%s env=%s", name, port, env);

    int result = 0;

    if (::GetFileAttributesW(infPath) == INVALID_FILE_ATTRIBUTES)
    {
        Log(L"install: INF not found, error=%lu", ::GetLastError());
        return 4;
    }

    AddLocalPort(port);

    DRIVER_INFO_3W di3;
    ::ZeroMemory(&di3, sizeof(di3));
    di3.cVersion = 3;
    di3.pName = const_cast<wchar_t *>(name);
    di3.pEnvironment = const_cast<wchar_t *>(env);
    di3.pDriverPath = const_cast<wchar_t *>(L"");
    di3.pDataFile = const_cast<wchar_t *>(infPath);
    di3.pConfigFile = NULL;
    di3.pHelpFile = NULL;
    di3.pDependentFiles = NULL;
    di3.pMonitorName = NULL;
    di3.pDefaultDataType = const_cast<wchar_t *>(L"RAW");

    const DWORD flags = APD_COPY_ALL_FILES | APD_INSTALL_WARNED_DRIVER;
    if (::AddPrinterDriverExW(NULL, 3, reinterpret_cast<LPBYTE>(&di3), flags))
    {
        Log(L"driver: AddPrinterDriverEx ok");
    }
    else
    {
        DWORD error = ::GetLastError();
        if (error == ERROR_PRINTER_DRIVER_ALREADY_INSTALLED)
        {
            Log(L"driver: already installed");
        }
        else
        {
            LogWin32(L"driver: AddPrinterDriverEx", error);
            result = 1;
        }
    }

    if (result == 0)
    {
        PRINTER_INFO_2W pi2;
        ::ZeroMemory(&pi2, sizeof(pi2));
        pi2.pPrinterName = const_cast<wchar_t *>(name);
        pi2.pDriverName = const_cast<wchar_t *>(name);
        pi2.pPortName = const_cast<wchar_t *>(port);
        pi2.pPrintProcessor = const_cast<wchar_t *>(L"winprint");
        pi2.pDatatype = const_cast<wchar_t *>(L"RAW");
        pi2.Attributes = PRINTER_ATTRIBUTE_LOCAL;

        HANDLE hPrinter = ::AddPrinterW(NULL, 2, reinterpret_cast<LPBYTE>(&pi2));
        if (hPrinter != NULL)
        {
            Log(L"queue: AddPrinter ok");
            ::ClosePrinter(hPrinter);
        }
        else
        {
            DWORD error = ::GetLastError();
            if (error == ERROR_PRINTER_ALREADY_EXISTS)
            {
                Log(L"queue: already exists");
            }
            else
            {
                LogWin32(L"queue: AddPrinter", error);
                result = 2;
            }
        }
    }

    Log(L"install: result=%d", result);
    return result;
}

// does the file contain the needle as ANSI text or as UTF-16LE text?
static bool FileMentions(const std::wstring &path, const char *needle)
{
    HANDLE file = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    LARGE_INTEGER size = {0};
    if (!::GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > (16 << 20))
    {
        ::CloseHandle(file);
        return false;
    }

    std::string data(static_cast<size_t>(size.QuadPart), 0);
    DWORD read = 0;
    BOOL ok = ::ReadFile(file, &data[0], static_cast<DWORD>(data.size()), &read, NULL);
    ::CloseHandle(file);
    if (!ok)
    {
        return false;
    }
    data.resize(read);

    const size_t n = strlen(needle);
    if (data.find(needle) != std::string::npos)
    {
        return true;
    }

    std::string wide;
    wide.reserve(n * 2);
    for (size_t i = 0; i < n; i++)
    {
        wide.push_back(needle[i]);
        wide.push_back(0);
    }
    return data.find(wide) != std::string::npos;
}

// uninstalls every OEM INF that references our catalogue (SetupUninstallOEMInf is native)
static int RemovePackage(const char *catalog)
{
    wchar_t windowsDir[MAX_PATH];
    if (::GetWindowsDirectoryW(windowsDir, MAX_PATH) == 0)
    {
        LogWin32(L"package: GetWindowsDirectory", ::GetLastError());
        return 2;
    }

    std::wstring infDir(windowsDir);
    infDir += L"\\INF\\";

    std::wstring pattern(infDir);
    pattern += L"oem*.inf";

    WIN32_FIND_DATAW entry;
    HANDLE search = ::FindFirstFileW(pattern.c_str(), &entry);
    if (search == INVALID_HANDLE_VALUE)
    {
        LogWin32(L"package: FindFirstFile", ::GetLastError());
        return 3;
    }

    int removed = 0;
    do
    {
        std::wstring path(infDir);
        path += entry.cFileName;
        if (!FileMentions(path, catalog))
        {
            continue;
        }

        Log(L"package: candidate %s", entry.cFileName);
        if (::SetupUninstallOEMInfW(entry.cFileName, SUOI_FORCEDELETE, NULL))
        {
            removed++;
            Log(L"package: removed %s", entry.cFileName);
        }
        else
        {
            LogWin32(L"package: SetupUninstallOEMInf", ::GetLastError());
        }
    } while (::FindNextFileW(search, &entry));

    ::FindClose(search);
    Log(L"package: removed %d package(s)", removed);
    return 0;
}

static int Uninstall(const wchar_t *name)
{
    const wchar_t *env = EnvName();
    int result = 0;

    HANDLE hPrinter = NULL;
    if (::OpenPrinterW(const_cast<wchar_t *>(name), &hPrinter, NULL))
    {
        if (::DeletePrinter(hPrinter))
        {
            Log(L"uninstall: queue removed");
        }
        else
        {
            LogWin32(L"uninstall: DeletePrinter", ::GetLastError());
        }
        ::ClosePrinter(hPrinter);
    }
    else
    {
        Log(L"uninstall: queue not present (%lu)", ::GetLastError());
    }

    if (!::DeletePrinterDriverExW(NULL, const_cast<wchar_t *>(env), const_cast<wchar_t *>(name), DPD_DELETE_ALL_FILES, 1))
    {
        DWORD error = ::GetLastError();
        LogWin32(L"uninstall: DeletePrinterDriverEx", error);
        result = 2;
    }
    else
    {
        Log(L"uninstall: driver removed");
    }

    Log(L"uninstall: result=%d", result);
    return result;
}

int wmain(int argc, wchar_t **argv)
{
    if (argc < 3)
    {
        ::wprintf(L"usage: ptimageinstall.exe install <inf> <name> <port> <log>\n"
                  L"       ptimageinstall.exe uninstall <name> <log>\n");
        return 64;
    }

    // the log file is always the last argument
    if (_wfopen_s(&g_log, argv[argc - 1], L"a") != 0 || g_log == NULL)
    {
        g_log = NULL;
    }

    const std::wstring action(argv[1]);
    int result = 64;

    if (action == L"install" && argc >= 6)
    {
        result = Install(argv[2], argv[3], argv[4]);
    }
    else if (action == L"uninstall" && argc >= 4)
    {
        result = Uninstall(argv[2]);
    }
    else if (action == L"remove-package" && argc >= 3)
    {
        result = RemovePackage("ptimage.cat");
    }
    else
    {
        ::wprintf(L"invalid arguments\n");
    }

    if (g_log != NULL)
    {
        fclose(g_log);
        g_log = NULL;
    }

    return result;
}
