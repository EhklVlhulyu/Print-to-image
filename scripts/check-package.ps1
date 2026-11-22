#  Print to image - static package consistency check
#
#  Verifies build\package before installing it:
#    * every file the INF copies exists on disk
#    * the filter CLSID in ptimage-pipelineconfig.xml matches dllentry.cpp
#    * PrinterDriverID matches between ptimage.inf and ptimage-manifest.ini
#    * the [FileSave] resource IDs exist in ptimageres.dll
#    * the catalog lists exactly the package files and carries a signature
#
#  Usage: powershell -ExecutionPolicy Bypass -File scripts\check-package.ps1

[CmdletBinding()]
param(
    [string] $PackageDir,
    [string] $SourceDir
)

$ErrorActionPreference = 'Stop'

try { [Console]::OutputEncoding = [System.Text.Encoding]::UTF8 } catch { }

if (-not ('NativeMethods' -as [type]))
{
    Add-Type -Namespace '' -Name NativeMethods -MemberDefinition @'
[DllImport("kernel32", SetLastError = true, CharSet = CharSet.Unicode)]
public static extern IntPtr LoadLibraryEx(string fileName, IntPtr file, uint flags);

[DllImport("user32", SetLastError = true, CharSet = CharSet.Unicode)]
public static extern int LoadString(IntPtr module, uint id, System.Text.StringBuilder buffer, int bufferMax);

[DllImport("kernel32")]
public static extern bool FreeLibrary(IntPtr module);
'@
}

$root = Split-Path -Parent $PSScriptRoot

# 从源码树运行时包在 build\package，解压发行包时包就在 scripts 的上一级
$defaultPackage = Join-Path $root 'build\package'
if (-not (Test-Path (Join-Path $defaultPackage 'ptimage.inf'))) { $defaultPackage = $root }

if (-not $PackageDir) { $PackageDir = $defaultPackage }
if (-not $SourceDir)  { $SourceDir  = Join-Path $root 'src' }

$failures = 0

function Check([bool] $condition, [string] $text)
{
    if ($condition)
    {
        Write-Host "  [ ok ] $text" -ForegroundColor Green
    }
    else
    {
        Write-Host "  [FAIL] $text" -ForegroundColor Red
        $script:failures++
    }
}

function Read-Text([string] $path)
{
    return [System.IO.File]::ReadAllText($path)
}

$infPath      = Join-Path $PackageDir 'ptimage.inf'
$manifestPath = Join-Path $PackageDir 'ptimage-manifest.ini'
$pipelinePath = Join-Path $PackageDir 'ptimage-pipelineconfig.xml'
$pdcPath      = Join-Path $PackageDir 'ptimage-pdc.xml'
$resPath      = Join-Path $PackageDir 'ptimageres.dll'
$filterPath   = Join-Path $PackageDir 'ptimagefilter.dll'
$catPath      = Join-Path $PackageDir 'ptimage.cat'
$cerPath      = Join-Path $PackageDir 'ptimage.cer'

Write-Host ''
Write-Host "Checking $PackageDir" -ForegroundColor White

# ---------------------------------------------------------------------------
Write-Host ''
Write-Host 'Files' -ForegroundColor Cyan

foreach ($file in @($infPath, $manifestPath, $pipelinePath, $pdcPath, $resPath, $filterPath, $catPath))
{
    Check (Test-Path $file) (Split-Path -Leaf $file)
}

# 驱动包里只应有驱动本体 10 个文件（x64 + x86\）：既不含脚本/文档，也不含构建残留
$allowedFiles = @(
    'ptimagefilter.dll', 'ptimageres.dll', 'ptimage.inf', 'ptimage_x86.inf', 'ptimage.cat',
    'ptimage.cer', 'ptimage-manifest.ini', 'ptimage-pdc.xml', 'ptimage-pipelineconfig.xml',
    'x86/ptimagefilter.dll', 'x86/ptimageres.dll'
)
$onDisk = @(Get-ChildItem -Path $PackageDir -Recurse -File |
            ForEach-Object { $_.FullName.Substring($PackageDir.Length).TrimStart([char]92) } |
            ForEach-Object { $_.Replace([char]92, [char]47) } |
            Sort-Object)
$stray  = @($onDisk | Where-Object { $allowedFiles -notcontains $_ })

Check ($stray.Count -eq 0) "package contains no stray files$(if ($stray.Count -gt 0) { " (found: $($stray -join ', '))" })"
Check ($onDisk.Count -eq $allowedFiles.Count) "package holds exactly $($allowedFiles.Count) files (found $($onDisk.Count))"
Check ((Test-Path (Join-Path $PackageDir 'x86/ptimagefilter.dll')) -and (Test-Path (Join-Path $PackageDir 'x86/ptimageres.dll'))) 'package contains the 32-bit binaries under x86/'

$inf = Read-Text $infPath
$manifest = Read-Text $manifestPath
$pipeline = Read-Text $pipelinePath

# ---------------------------------------------------------------------------
Write-Host ''
Write-Host 'INF / package consistency' -ForegroundColor Cyan

$copyFiles = [regex]::Match($inf, '(?ms)^\[PTIMAGE_FILES\]\s*(.*?)^\[').Groups[1].Value
$listed = @($copyFiles -split "`r?`n" | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne '' })

Check ($listed.Count -ge 5) "INF lists $($listed.Count) files"

foreach ($name in $listed)
{
    Check (Test-Path (Join-Path $PackageDir $name)) "INF copies $name and it exists"
}

Check ($inf -match 'ClassVer\s*=\s*4\.0') 'INF declares ClassVer=4.0 (v4 driver)'
Check ($inf -match 'DestinationDirs(?s).*?DefaultDestDir\s*=\s*66000') 'INF uses DestinationDirs 66000'
Check ($inf -match 'ClassGUID\s*=\s*\{4D36E979-E325-11CE-BFC1-08002BE10318\}') 'INF uses the Printer class GUID'
Check ($inf -match 'PnpLockdown\s*=\s*1') 'INF requests PnpLockdown'
Check ($inf -notmatch '(?m)^\s*(AddReg|AddService|Include|Needs|DataFile|CoreDriverSections)\s*=') 'INF contains no v3-only directives'

$infX86Path = Join-Path $PackageDir 'ptimage_x86.inf'
Check (Test-Path $infX86Path) 'ptimage_x86.inf'
$infX86 = Read-Text $infX86Path

foreach ($pair in @(@{ File = 'ptimage.inf'; Text = $inf; Arch = 'NTamd64'; Other = 'NTx86' },
                    @{ File = 'ptimage_x86.inf'; Text = $infX86; Arch = 'NTx86'; Other = 'NTamd64' }))
{
    $name = $pair.File
    $body = $pair.Text
    Check ($body -match ('(?m)^\[Manufacturer\]\s*
\s*"ptimage"=ptimage,' + $pair.Arch + '\s*$')) "$name targets $($pair.Arch) without OS-version decorations"
    Check ($body -notmatch $pair.Other) "$name does not mention $($pair.Other)"
    Check (@($body -split [char]10 | Where-Object { $_ -match '^\[SourceDisksFiles\.' }).Count -eq 0) "$name uses a single [SourceDisksFiles] section"
    Check ($body -match 'ClassVer\s*=\s*4\.0') "$name declares ClassVer=4.0"
    Check ($body -match 'PnpLockdown\s*=\s*1') "$name requests PnpLockdown"
    Check ($body -match 'CatalogFile\s*=\s*ptimage\.cat') "$name references ptimage.cat"
    Check ($body -match ('"Print to image"\s*=\s*PTIMAGE_INSTALL,\{2C5E7A91-4D83-4B6F-9E12-5A7C3D8B1F60\}')) "$name declares the Print to image model"
}

Check ($infX86 -match '(?m)^ptimagefilter\.dll=1,x86\s*$') 'ptimage_x86.inf copies the 32-bit filter from the x86 sub directory'
Check ($infX86 -match '(?m)^ptimageres\.dll=1,x86\s*$') 'ptimage_x86.inf copies the 32-bit resource DLL from the x86 sub directory'

$driverVer = @($inf -split [char]10 | ForEach-Object { $_.Trim() } | Where-Object { $_.StartsWith('DriverVer') })
Check (($driverVer.Count -eq 1) -and $driverVer[0].EndsWith(',1.0.0')) "INF DriverVer is 1.0.0$(if ($driverVer.Count -eq 1) { " ($($driverVer[0]))" })"


# ---------------------------------------------------------------------------
Write-Host ''
Write-Host 'Identifiers' -ForegroundColor Cyan

$infDriverId = [regex]::Match($inf, '= *PTIMAGE_INSTALL, *(\{[0-9A-Fa-f\-]+\})').Groups[1].Value
$manifestDriverId = [regex]::Match($manifest, 'PrinterDriverID\s*=\s*(\{[0-9A-Fa-f\-]+\})').Groups[1].Value

Check ($infDriverId -ne '' -and $infDriverId -eq $manifestDriverId) "PrinterDriverID matches ($infDriverId)"

$pipelineClsid = [regex]::Match($pipeline, 'clsid="(\{[0-9A-Fa-f\-]+\})"').Groups[1].Value
$sourcePath = Join-Path $SourceDir 'dllentry.cpp'
$sourceClsid = ''
if (Test-Path $sourcePath) { $sourceClsid = [regex]::Match((Read-Text $sourcePath), 'CLSID_PtImageFilter\s*=\s*\{([^}]+)').Groups[1].Value }

$tokens = @([regex]::Matches($sourceClsid, '0x([0-9A-Fa-f]+)') | ForEach-Object { $_.Groups[1].Value.ToUpperInvariant() })
$built = ''

if ($tokens.Count -eq 11)
{
    $built = '{0}-{1}-{2}-{3}-{4}' -f `
        $tokens[0], `
        $tokens[1], `
        $tokens[2], `
        ($tokens[3] + $tokens[4]), `
        (($tokens[5..10]) -join '')

    $built = '{' + $built + '}'
}

if (Test-Path $sourcePath) { Check ($built -ne '' -and $built -eq $pipelineClsid.ToUpperInvariant()) "filter CLSID matches source ($pipelineClsid)" }
else
{
    Check ($pipelineClsid -match '^\{[0-9A-F]{8}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{12}\}$') "filter CLSID is a well-formed GUID ($pipelineClsid)"
    Write-Host "  [info] src\dllentry.cpp not found - packaged copy, skipping source comparison" -ForegroundColor DarkGray
}
Check ($pipeline -match 'FilterServiceProvider\s+dll="XpsRasterService\.dll"') 'pipeline config requests XpsRasterService'

# ---------------------------------------------------------------------------
Write-Host ''
Write-Host 'Manifest' -ForegroundColor Cyan

Check ($manifest -match 'DataFile\s*=\s*ptimage-pdc\.xml') 'PDC data file declared'
Check ($manifest -match 'DataFileType\s*=\s*application/vnd\.ms-PrintDeviceCapabilities\+xml') 'PDC MIME type declared'
Check ($manifest -match 'DriverCategory\s*=\s*PrintFax\.Printer\.File') 'DriverCategory is PrintFax.Printer.File'
Check ($manifest -match 'ResourceFile\s*=\s*ptimageres\.dll') 'ResourceFile declared'
Check ($manifest -notmatch '(?m)^\s*RequiredFiles\s*=') 'RequiredFiles omitted (it only pulls files from ntprint.inf)'

$fileSave = [regex]::Match($manifest, '(?ms)^\[FileSave\]\s*(.*?)(^\[|\z)').Groups[1].Value
$extensions = @()
$extIds = @{}

foreach ($line in ($fileSave -split "`r?`n"))
{
    $m = [regex]::Match($line, '^\s*([A-Za-z0-9]+)\s*=\s*(\d+)\s*$')

    if ($m.Success)
    {
        if ($m.Groups[1].Value -ieq 'SaveAsTitle') { continue }
        $ext = $m.Groups[1].Value.ToLowerInvariant()
        $extensions += $ext
        $extIds[$ext] = [int]$m.Groups[2].Value
    }
}

foreach ($expected in @('jpg', 'png', 'bmp'))
{
    Check ($extensions -contains $expected) "[FileSave] lists .$expected"
}

Check (($extensions | Sort-Object -Unique).Count -eq $extensions.Count) '[FileSave] extension names are unique'
Check ($null -ne $extIds['jpg'] -and $extIds['jpg'] -eq 100) '[FileSave] jpg maps to resource 100'
Check ($null -ne $extIds['png'] -and $extIds['png'] -eq 101) '[FileSave] png maps to resource 101'
Check ($null -ne $extIds['bmp'] -and $extIds['bmp'] -eq 102) '[FileSave] bmp maps to resource 102'

$sharedIds = @($extIds.Values | Group-Object | Where-Object { $_.Count -gt 1 })

Check ($sharedIds.Count -eq 0) '[FileSave] every format uses its own resource ID (avoids duplicate entries in the Save As dialog)'

Check ($manifest -match '(?m)^\s*XpsFormat\s*=\s*XPS\s*$') '[DriverRender] XpsFormat=XPS (the XPS OM cannot parse OpenXPS page markup)'

# ---------------------------------------------------------------------------
Write-Host ''
Write-Host 'Resource strings' -ForegroundColor Cyan

$loaded = [NativeMethods]::LoadLibraryEx($resPath, [IntPtr]::Zero, 2)

if ($loaded -eq [IntPtr]::Zero)
{
    Check $false "ptimageres.dll loads (error $([System.Runtime.InteropServices.Marshal]::GetLastWin32Error()))"
}
else
{
    $buffer = New-Object System.Text.StringBuilder 512

    foreach ($id in @(100, 101, 102, 200))
    {
        [void]$buffer.Clear()
        $length = [NativeMethods]::LoadString($loaded, $id, $buffer, $buffer.Capacity)
        Check ($length -gt 0) "string resource $id present: $($buffer.ToString())"
    }

    [void][NativeMethods]::FreeLibrary($loaded)
}

# ---------------------------------------------------------------------------
Write-Host ''
Write-Host 'Catalog' -ForegroundColor Cyan

$catBytes = [System.IO.File]::ReadAllBytes($catPath)

# The catalog stores file names as UTF-16 strings at arbitrary even/odd
# offsets, so decode both alignments before searching.
$catText = [System.Text.Encoding]::Unicode.GetString($catBytes) + [System.Text.Encoding]::Unicode.GetString($catBytes[1..($catBytes.Length - 1)])
$missing = @()

foreach ($name in @('ptimage.inf', 'ptimage-manifest.ini', 'ptimage-pdc.xml', 'ptimage-pipelineconfig.xml', 'ptimagefilter.dll', 'ptimageres.dll', 'x86\ptimagefilter.dll', 'x86\ptimageres.dll'))
{
    if ($catText.IndexOf($name, [System.StringComparison]::OrdinalIgnoreCase) -lt 0) { $missing += $name }
}

Check ($missing.Count -eq 0) "catalog lists every package file$(if ($missing.Count -gt 0) { " (missing: $($missing -join ', '))" })"
Check ($catText.IndexOf('ptimage.cer', [System.StringComparison]::OrdinalIgnoreCase) -lt 0) 'certificate is not part of the catalog'

$signature = Get-AuthenticodeSignature -FilePath $catPath
Check ($signature.SignerCertificate -ne $null) "catalog is signed by $($signature.SignerCertificate.Subject)"

if ($cerPath -and (Test-Path $cerPath))
{
    $signatureError = 0
    foreach ($entry in $signature.AllSignatureErrors) { $signatureError++ }
    Write-Host "  [info] signature status: $($signature.Status) (untrusted root is expected before install)" -ForegroundColor DarkGray
}

Write-Host ''
if ($failures -eq 0)
{
    Write-Host 'Package check passed.' -ForegroundColor Green
    exit 0
}

Write-Host "$failures check(s) failed." -ForegroundColor Red
exit 1
