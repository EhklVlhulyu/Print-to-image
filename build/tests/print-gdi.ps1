#requires -version 5
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string] $OutFile,
    [int] $Pages = 1,
    [string] $PrinterName = 'Print to image'
)

$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.Drawing

$doc = New-Object System.Drawing.Printing.PrintDocument
$doc.PrinterSettings.PrinterName = $PrinterName
if (-not $doc.PrinterSettings.IsValid) {
    throw "Printer '$PrinterName' is not available."
}

$doc.PrinterSettings.PrintToFile = $true
$doc.PrinterSettings.PrintFileName = $OutFile
$doc.DocumentName = 'ptimage test document'

$script:pageNo = 0
$doc.add_PrintPage({
    param($sender, $e)
    $script:pageNo++
    $g = $e.Graphics
    $g.FillRectangle([System.Drawing.Brushes]::White, $e.MarginBounds)
    $font = New-Object System.Drawing.Font('Arial', 24)
    $g.DrawString("Page $script:pageNo", $font, [System.Drawing.Brushes]::Black, 100, 100)
    $g.DrawRectangle([System.Drawing.Pens]::Red, 100, 200, 400, 200)
    $g.DrawString("Print to image test", $font, [System.Drawing.Brushes]::Blue, 100, 450)
    $e.HasMorePages = ($script:pageNo -lt $Pages)
})

$doc.Print()
Write-Output ("GDI job sent: {0} page(s) -> {1}" -f $Pages, $OutFile)
