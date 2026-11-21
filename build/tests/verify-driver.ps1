#requires -version 5
<#
    End-to-end acceptance test for the "Print to image" driver.

    Prints through the installed queue with PrintToFile so the spooler hands the
    filter a file name directly (no Save As dialog) and then checks what the
    driver produced:

      * PNG, JPEG and BMP each produce a non-empty image
      * a three page job produces multi.png, multi-2.png and multi-3.png
      * the Save As type list has one entry per format (no duplicate JPEG)

    Printing needs no administrator rights; the driver itself must of course be
    installed first (run dist\print-to-images-1.0.0-setup.exe as admin).

    Usage:
      powershell -ExecutionPolicy Bypass -File build\tests\verify-driver.ps1
#>
[CmdletBinding()]
param(
    [string] $PrinterName = 'Print to image',
    [string] $OutDir,
    [int]    $TimeoutSeconds = 40
)

$ErrorActionPreference = 'Stop'

try { [Console]::OutputEncoding = [System.Text.Encoding]::UTF8 } catch { }

$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

if (-not $OutDir) {
    $OutDir = Join-Path $root 'build\verify'
}

$script:failures = 0

function Write-Step([string] $text) { Write-Host ''; Write-Host "== $text" -ForegroundColor Cyan }
function Write-Ok([string] $text)   { Write-Host "  [ ok ] $text" -ForegroundColor Green }
function Write-Bad([string] $text)  { Write-Host "  [FAIL] $text" -ForegroundColor Red; $script:failures++ }
function Write-Info([string] $text) { Write-Host "  [info] $text" -ForegroundColor DarkGray }

function Wait-ForFile([string] $path, [int] $seconds) {
    $deadline = (Get-Date).AddSeconds($seconds)

    while ((Get-Date) -lt $deadline) {
        if (Test-Path -LiteralPath $path) {
            $item = Get-Item -LiteralPath $path -ErrorAction SilentlyContinue

            if ($item -and $item.Length -gt 0) {
                return $item.Length
            }
        }

        Start-Sleep -Milliseconds 500
    }

    return 0
}

function Print-Pages([string] $outFile, [int] $pages) {
    if (Test-Path -LiteralPath $outFile) {
        Remove-Item -LiteralPath $outFile -Force
    }

    & (Join-Path $PSScriptRoot 'print-gdi.ps1') -OutFile $outFile -Pages $pages -PrinterName $PrinterName |
        ForEach-Object { Write-Info $_ }

    return (Wait-ForFile $outFile $TimeoutSeconds)
}

function Get-ImageHeader([string] $path) {
    $bytes = [System.IO.File]::ReadAllBytes($path)
    $ext = [System.IO.Path]::GetExtension($path).ToLowerInvariant()

    if ($ext -eq '.png') {
        $signature = 0x89, 0x50, 0x4E, 0x47

        for ($i = 0; $i -lt 4; $i++) {
            if ($bytes[$i] -ne $signature[$i]) { return $null }
        }

        return 'PNG'
    }

    if ($ext -eq '.jpg' -or $ext -eq '.jpeg') {
        if ($bytes[0] -ne 0xFF -or $bytes[1] -ne 0xD8) { return $null }

        return 'JPEG'
    }

    if ($ext -eq '.bmp') {
        if ($bytes[0] -ne 0x42 -or $bytes[1] -ne 0x4D) { return $null }

        return 'BMP'
    }

    return $null
}

#
# 1. the queue and its driver
#
Write-Step "printer and driver"

$printer = Get-Printer -Name $PrinterName -ErrorAction SilentlyContinue

if (-not $printer) {
    Write-Bad "printer '$PrinterName' is not installed - run dist\print-to-images-1.0.0-setup.exe as administrator"
    Write-Host ''
    Write-Host "$($script:failures) failure(s)" -ForegroundColor Red
    exit 1
}

Write-Ok "printer '$PrinterName' present (port $($printer.PortName), driver $($printer.DriverName))"

if ($printer.PortName -ne 'PORTPROMPT:') {
    Write-Info "port is '$($printer.PortName)'; PORTPROMPT: is what shows the Save As dialog"
}

$driver = Get-PrinterDriver -Name $printer.DriverName -ErrorAction SilentlyContinue

if ($driver) {
    Write-Ok "driver version $($driver.MajorVersion).$($driver.MinorVersion) loaded"
} else {
    Write-Bad "driver '$($printer.DriverName)' is not registered"
}

#
# 2. one page per format
#
Write-Step "single page output"

if (-not (Test-Path -LiteralPath $OutDir)) {
    New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
}

foreach ($format in @('png', 'jpg', 'bmp')) {
    $path = Join-Path $OutDir "one.$format"
    $size = Print-Pages $path 1

    if ($size -le 0) {
        Write-Bad "*.${format}: no output file was written (see %TEMP%\ptimage*.png leftovers and the spooler log)"
        continue
    }

    $kind = Get-ImageHeader $path

    if (-not $kind) {
        Write-Bad "*.${format}: $size bytes but the file is not a valid $($format.ToUpperInvariant()) image"
        continue
    }

    Write-Ok "*.${format}: $kind, $size bytes"
}

#
# 3. multi page output
#
Write-Step "three page output"

$multi = Join-Path $OutDir 'multi.png'
$firstPageSize = Print-Pages $multi 3

if ($firstPageSize -le 0) {
    Write-Bad 'multi-page: no output file was written'
} else {
    Write-Ok "multi-page: first page written ($firstPageSize bytes)"

    foreach ($n in 2, 3) {
        $sibling = Join-Path $OutDir "multi-$n.png"
        $size = Wait-ForFile $sibling 10

        if ($size -le 0) {
            Write-Bad "multi-page: $([System.IO.Path]::GetFileName($sibling)) is missing"
        } else {
            Write-Ok "multi-page: $([System.IO.Path]::GetFileName($sibling)) written ($size bytes)"
        }
    }
}

#
# 4. the manifest the driver actually uses
#
Write-Step "embedded manifest"

$driverDir = Join-Path $env:SystemRoot 'System32\DriverStore\FileRepository'

$manifest = Get-ChildItem -LiteralPath $driverDir -Filter 'ptimage.inf_amd64_*' -Directory -ErrorAction SilentlyContinue |
    ForEach-Object { Join-Path $_.FullName 'ptimage-manifest.ini' } |
    Where-Object { Test-Path -LiteralPath $_ } |
    Select-Object -First 1

if (-not $manifest) {
    Write-Info 'installed manifest not found (the driver may be staged under another name)'
} else {
    $text = Get-Content -LiteralPath $manifest -Raw

    if ($text -match '(?m)^\s*XpsFormat\s*=\s*XPS\s*$') {
        Write-Ok 'XpsFormat=XPS (the XPS object model can parse the page markup)'
    } else {
        Write-Bad 'XpsFormat is not XPS - page markup will be rejected with XPS_E_INVALID_MARKUP and the output stays 0 bytes'
    }

    $entries = [regex]::Matches($text, '(?m)^\s*([A-Za-z0-9]+)\s*=\s*(\d+)\s*$') |
        Where-Object { $_.Groups[1].Value -notin @('DataFile', 'DataFileType', 'ResourceFile', 'PrinterDriverID', 'Flags', 'DriverCategory', 'OutputFormat', 'XpsFormat', 'SaveAsTitle') }

    $extensions = @($entries | ForEach-Object { $_.Groups[1].Value.ToLowerInvariant() })

    #
    # The Save As dialog lists one entry per resource ID, so two extensions that
    # share an ID show up as one duplicated entry (jpg and jpeg used to both use 100).
    #
    $byId = @($entries | ForEach-Object { $_.Groups[2].Value } | Group-Object | Where-Object { $_.Count -gt 1 })

    if ($byId.Count -eq 0) {
        Write-Ok "save-as types: $($extensions -join ', ') (one entry per format)"
    } else {
        $detail = $byId | ForEach-Object {
            $id = $_.Name
            $shared = @($entries | Where-Object { $_.Groups[2].Value -eq $id } | ForEach-Object { $_.Groups[1].Value })
            "resource $id shared by $($shared -join '/')"
        }
        Write-Bad "duplicate save-as entries: $($detail -join '; ')"
    }
}

Write-Host ''

if ($script:failures -eq 0) {
    Write-Host 'VERIFY PASSED - the driver writes images and splits multi-page jobs' -ForegroundColor Green
    exit 0
}

Write-Host "$($script:failures) failure(s)" -ForegroundColor Red
Write-Host 'Check: Get-WinEvent -LogName Microsoft-Windows-PrintService/Admin -MaxEvents 5' -ForegroundColor Yellow

$filterLogs = @(
    (Join-Path $env:ProgramData 'Print to image'),
    (Join-Path $env:LOCALAPPDATA 'Print to image'),
    (Join-Path $env:TEMP 'Print to image')
) | Where-Object { Test-Path -LiteralPath $_ } | ForEach-Object {
    Get-ChildItem -LiteralPath $_ -Filter 'filter-*.log' -ErrorAction SilentlyContinue
}

if ($filterLogs) {
    Write-Host 'Filter log (newest first):' -ForegroundColor Yellow
    $filterLogs | Sort-Object LastWriteTime -Descending | Select-Object -First 3 | ForEach-Object {
        Write-Host "  $($_.FullName)" -ForegroundColor Yellow
        Get-Content -LiteralPath $_.FullName -Tail 25 | ForEach-Object { Write-Host "    $_" }
    }
} else {
    Write-Host 'No filter log found - logs are written only when a job fails, and only by the current build.' -ForegroundColor Yellow
}
exit 1
