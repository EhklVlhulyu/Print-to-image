#  Print to image - build + package script
#
#  Produces a complete, signed v4 print driver package in build\package:
#     ptimage.inf, ptimage-manifest.ini, ptimage-pdc.xml,
#     ptimage-pipelineconfig.xml, ptimagefilter.dll, ptimageres.dll,
#     x86\ptimagefilter.dll, x86\ptimageres.dll,
#     ptimage.cat (SHA-256 catalog), ptimage.cer (self-signed certificate)
#
#  Usage (normal PowerShell window, no admin needed):
#     powershell -ExecutionPolicy Bypass -File scripts\build-package.ps1
#
#  Parameters:
#     -SkipSign        build and stage the package without signing it
#     -SkipBuild       reuse the DLLs already in build\ (skip MSBuild/cl)
#     -CertSubject X   subject name of the self-signed certificate to use

[CmdletBinding()]
param(
    [switch] $SkipSign,
    [switch] $SkipBuild,
    [string] $CertSubject = 'CN=Print to image self-signed driver'
)

$ErrorActionPreference = 'Stop'

try { [Console]::OutputEncoding = [System.Text.Encoding]::UTF8 } catch { }

$root    = Split-Path -Parent $PSScriptRoot
$build   = Join-Path $root 'build'
$driver  = Join-Path $root 'driver'
$package = Join-Path $build 'package'
$x86     = Join-Path $build 'x86'

function Write-Step([string] $text)
{
    Write-Host ''
    Write-Host "==> $text" -ForegroundColor Cyan
}

# ---------------------------------------------------------------------------
# 1. build the binaries
# ---------------------------------------------------------------------------
if (-not $SkipBuild)
{
    Write-Step 'Building ptimagefilter.dll and ptimageres.dll'

    $buildCmd = Join-Path $build 'build.cmd'
    if (-not (Test-Path $buildCmd))
    {
        throw "build.cmd not found at $buildCmd"
    }

    & cmd.exe /c "`"$buildCmd`""
    if ($LASTEXITCODE -ne 0)
    {
        throw "build.cmd failed with exit code $LASTEXITCODE"
    }
}

# x86 binaries live in build\x86 and are staged into the package's x86 folder,
# which is what [SourceDisksFiles] refers to in ptimage_x86.inf.
$files = @(
    @{ Name = 'ptimagefilter.dll';          From = $build;      Sub = ''    },
    @{ Name = 'ptimageres.dll';             From = $build;      Sub = ''    },
    @{ Name = 'ptimagefilter.dll';          From = $x86;        Sub = 'x86' },
    @{ Name = 'ptimageres.dll';             From = $x86;        Sub = 'x86' },
    @{ Name = 'ptimage.inf';                From = $driver;     Sub = ''    },
    @{ Name = 'ptimage_x86.inf';            From = $driver;     Sub = ''    },
    @{ Name = 'ptimage-manifest.ini';       From = $driver;     Sub = ''    },
    @{ Name = 'ptimage-pdc.xml';            From = $driver;     Sub = ''    },
    @{ Name = 'ptimage-pipelineconfig.xml'; From = $driver;     Sub = ''    }
)

foreach ($file in $files)
{
    $source = Join-Path $file.From $file.Name
    if (-not (Test-Path $source))
    {
        throw "missing build input: $source"
    }
}

# ---------------------------------------------------------------------------
# 2. stage the package directory
# ---------------------------------------------------------------------------
Write-Step "Staging $package"

if (Test-Path $package)
{
    Remove-Item $package -Recurse -Force
}

New-Item -ItemType Directory -Path $package -Force | Out-Null

New-Item -ItemType Directory -Path (Join-Path $package 'x86') -Force | Out-Null

foreach ($file in $files)
{
    $targetDir = if ($file.Sub) { Join-Path $package $file.Sub } else { $package }
    $label     = if ($file.Sub) { "$($file.Sub)\$($file.Name)" } else { $file.Name }

    Copy-Item -Path (Join-Path $file.From $file.Name) -Destination $targetDir -Force
    Write-Host ("    {0,-36} {1,10:N0} bytes" -f $label, (Get-Item (Join-Path $targetDir $file.Name)).Length)
}

# ---------------------------------------------------------------------------
# 3. catalog (SHA-256, version 2.0)
# ---------------------------------------------------------------------------
$catalogPath = Join-Path $package 'ptimage.cat'

Write-Step 'Generating ptimage.cat (SHA-256 catalog)'

New-FileCatalog -Path $package -CatalogFilePath $catalogPath -CatalogVersion 2.0 | Out-Null
Write-Host ("    ptimage.cat {0:N0} bytes" -f (Get-Item $catalogPath).Length)

# ---------------------------------------------------------------------------
# 4. self-signed certificate + signature
# ---------------------------------------------------------------------------
$cerPath = Join-Path $package 'ptimage.cer'

if ($SkipSign)
{
    Write-Step 'Skipping signing (-SkipSign)'
    Write-Host '    The package is NOT signed; the installer cannot load an unsigned driver.' -ForegroundColor Yellow
    exit 0
}

Write-Step 'Preparing a self-signed driver signing certificate'

$cert = Get-ChildItem -Path Cert:\CurrentUser\My |
        Where-Object { $_.Subject -eq $CertSubject } |
        Select-Object -First 1

if ($null -eq $cert)
{
    $cert = New-SelfSignedCertificate `
                -Type CodeSigningCert `
                -Subject $CertSubject `
                -KeyUsage DigitalSignature `
                -KeyExportPolicy Exportable `
                -NotAfter (Get-Date).AddYears(10) `
                -CertStoreLocation Cert:\CurrentUser\My `
                -TextExtension @(
                    '2.5.29.37={text}1.3.6.1.4.1.311.61.1.1,1.3.6.1.5.5.7.3.3'
                )

    Write-Host "    created certificate $($cert.Thumbprint)"
}
else
{
    Write-Host "    reusing certificate $($cert.Thumbprint)"
}

Export-Certificate -Cert $cert -FilePath $cerPath -Type CERT | Out-Null

# ---------------------------------------------------------------------------
# 5. sign the catalog
# ---------------------------------------------------------------------------
Write-Step 'Signing ptimage.cat'

$signtool = Get-ChildItem -Path "${env:ProgramFiles(x86)}\Windows Kits\10\bin" -Recurse -Filter signtool.exe -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match '\\x64\\signtool\.exe$' } |
            Sort-Object FullName -Descending |
            Select-Object -First 1

if ($null -eq $signtool)
{
    $signtool = Get-ChildItem -Path 'E:\Windows Kits\10\bin' -Recurse -Filter signtool.exe -ErrorAction SilentlyContinue |
                Where-Object { $_.FullName -match '\\x64\\signtool\.exe$' } |
                Sort-Object FullName -Descending |
                Select-Object -First 1
}

if ($null -eq $signtool)
{
    throw 'signtool.exe was not found. Install the Windows SDK (Signing Tools) or sign ptimage.cat manually.'
}

Write-Host "    signtool: $($signtool.FullName)"

& $signtool.FullName sign /fd SHA256 /s My /sha1 $cert.Thumbprint ($catalogPath)
if ($LASTEXITCODE -ne 0)
{
    throw "signtool sign failed with exit code $LASTEXITCODE"
}

& $signtool.FullName verify /pa /v ($catalogPath) | Out-Host
Write-Host '    note: "verify /pa" only passes once the certificate is trusted, which the installer does.' -ForegroundColor DarkGray

Write-Step 'Done'
Write-Host "Package directory: $package"
Get-ChildItem $package | Format-Table Name, Length -AutoSize | Out-String | Write-Host
Write-Host 'Next: run installer\build-nsis.ps1 to build the setup program.' -ForegroundColor Green
exit 0
