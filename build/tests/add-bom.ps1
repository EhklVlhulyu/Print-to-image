[CmdletBinding()]
param([string[]] $Path)
$ErrorActionPreference = 'Stop'
foreach ($p in $Path) {
    foreach ($one in ($p -split ',')) {
        $full = (Resolve-Path -LiteralPath $one.Trim()).Path
        $text = [System.IO.File]::ReadAllText($full, (New-Object System.Text.UTF8Encoding $false))
        [System.IO.File]::WriteAllText($full, $text, (New-Object System.Text.UTF8Encoding $true))
        $bytes = [System.IO.File]::ReadAllBytes($full)
        "{0}: {1} bytes, BOM={2}" -f (Split-Path -Leaf $full), $bytes.Length, (($bytes[0] -eq 0xEF) -and ($bytes[1] -eq 0xBB) -and ($bytes[2] -eq 0xBF))
    }
}
