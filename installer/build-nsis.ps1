# ---------------------------------------------------------------------------
# 构建 NSIS 安装程序：dist\print-to-images-1.0.0-setup.exe
#
# 步骤：
#   1. （默认）调用 scripts\build-package.ps1：编译驱动、生成目录文件并签名
#   2. 把 build\package\* 收集到 build\nsis\payload（只有驱动本体，无脚本/文档）
#   3. 从 ptimage.cer 取证书指纹，连同 payload 一起交给 makensis
#
# 用法：
#   powershell -ExecutionPolicy Bypass -File installer\build-nsis.ps1
#   powershell -ExecutionPolicy Bypass -File installer\build-nsis.ps1 -SkipPackage
# ---------------------------------------------------------------------------

[CmdletBinding()]
param(
    [string] $NsisDir,
    [string] $SetupName = 'print-to-images-1.0.0-setup.exe',
    [switch] $SkipPackage
)

$ErrorActionPreference = 'Stop'
try { [Console]::OutputEncoding = [System.Text.Encoding]::UTF8 } catch { }

function Write-Ok   { param([string] $m) Write-Host "[ ok ] $m"   -ForegroundColor Green }
function Write-Step { param([string] $m) Write-Host "[step] $m"   -ForegroundColor Cyan }
function Write-Warn2{ param([string] $m) Write-Host "[warn] $m"   -ForegroundColor Yellow }
function Fail       { param([string] $m) Write-Host "[fail] $m"   -ForegroundColor Red; exit 1 }

$root = Split-Path -Parent $PSScriptRoot

if (-not $NsisDir) {
    $candidates = @(
        (Join-Path $root 'tools\nsis\nsis-3.10'),
        (Join-Path $root 'tools\nsis'),
        'C:\Program Files (x86)\NSIS',
        'C:\Program Files\NSIS'
    )
    foreach ($c in $candidates) {
        if (Test-Path (Join-Path $c 'makensis.exe')) { $NsisDir = $c; break }
    }
}
if (-not $NsisDir -or -not (Test-Path (Join-Path $NsisDir 'makensis.exe'))) {
    Fail "找不到 makensis.exe。请把 NSIS 3.x 解压到 tools\nsis\nsis-3.10，或使用 -NsisDir 指定目录。"
}
$makensis = Join-Path $NsisDir 'makensis.exe'
Write-Ok "makensis: $makensis"

if (-not $SkipPackage) {
    Write-Step '编译驱动并签名（scripts\build-package.ps1）'
    & (Join-Path $root 'scripts\build-package.ps1')
    if ($LASTEXITCODE -ne 0) { Fail "build-package.ps1 失败（退出码 $LASTEXITCODE）" }
} else {
    Write-Warn2 '跳过驱动编译，使用已有 build\package'
}

$package = Join-Path $root 'build\package'
if (-not (Test-Path (Join-Path $package 'ptimage.inf'))) { Fail "缺少 $package\ptimage.inf" }

# 安装包只放驱动本体这 8 个文件；缺任何一个都视为打包错误
$expected = @(
    'ptimagefilter.dll', 'ptimageres.dll', 'ptimage.inf', 'ptimage_x86.inf', 'ptimage.cat',
    'ptimage.cer', 'ptimage-manifest.ini', 'ptimage-pdc.xml', 'ptimage-pipelineconfig.xml',
    'x86\ptimagefilter.dll', 'x86\ptimageres.dll'
)
foreach ($name in $expected) {
    if (-not (Test-Path (Join-Path $package $name))) { Fail "安装包缺少 $name" }
}
$allowed = @($expected | ForEach-Object { Split-Path $_ -Leaf })
$extra = @(Get-ChildItem -Path $package -Recurse -File |
           Where-Object { $_.FullName -notmatch '\\x86\\' -and $allowed -notcontains $_.Name })
if ($extra.Count -gt 0) {
    foreach ($f in $extra) { Write-Warn2 "忽略额外文件 $($f.Name)（不在安装清单内）" }
}

$stage = Join-Path $root 'build\nsis\payload'
if (Test-Path (Join-Path $root 'build\nsis')) { Remove-Item (Join-Path $root 'build\nsis') -Recurse -Force }
New-Item -ItemType Directory -Force -Path $stage | Out-Null

New-Item -ItemType Directory -Force -Path (Join-Path $stage 'x86') | Out-Null
foreach ($name in $expected) {
    $target = Join-Path $stage $name
    $dir = Split-Path $target -Parent
    if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
    Copy-Item (Join-Path $package $name) $target -Force
}
Write-Ok "payload 已准备：$($expected.Count) 个文件（含 x86\） → $stage"

# 安装辅助程序（用 Win32 API 注册 v4 驱动；安装过程完全不使用 PowerShell 脚本）
$helpers = @(
    @{ From = (Join-Path $root 'build\ptimageinstall.exe');      Name = 'ptimageinstall.exe' },
    @{ From = (Join-Path $root 'build\x86\ptimageinstall.exe');  Name = 'ptimageinstall32.exe' }
)
foreach ($helper in $helpers) {
    if (-not (Test-Path $helper.From)) { Fail "缺少安装辅助程序 $($helper.From)（先运行 build\build.cmd）" }
    Copy-Item $helper.From (Join-Path $stage $helper.Name) -Force
}
Write-Ok "安装辅助程序已加入：ptimageinstall.exe / ptimageinstall32.exe"

$cert = New-Object System.Security.Cryptography.X509Certificates.X509Certificate2 (Join-Path $package 'ptimage.cer')
$thumbprint = $cert.Thumbprint
if (-not $thumbprint) { Fail "无法从 ptimage.cer 读取证书指纹" }
Write-Ok "证书指纹: $thumbprint"

$dist = Join-Path $root 'dist'
New-Item -ItemType Directory -Force -Path $dist | Out-Null
$outFile = Join-Path $dist $SetupName
if (Test-Path $outFile) { Remove-Item $outFile -Force }

$nsiPath = Join-Path $PSScriptRoot 'ptimage.nsi'
if (-not (Test-Path $nsiPath)) { Fail 'ptimage.nsi not found' }

Write-Step '调用 makensis 生成安装程序'
& $makensis '/V2' "/DPAYLOAD=$stage" "/DOUTFILE=$outFile" "/DCERTTHUMB=$thumbprint" $nsiPath
if ($LASTEXITCODE -ne 0) { Fail "makensis 失败（退出码 $LASTEXITCODE）" }
if (-not (Test-Path $outFile)) { Fail "未生成 $outFile" }

$size = (Get-Item $outFile).Length
Write-Ok ("BUILD OK: {0} ({1:N0} 字节)" -f $outFile, $size)
exit 0
