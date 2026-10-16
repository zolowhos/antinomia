# format.ps1 - run clang-format over the tree without typing the Visual Studio path.
# Module: tool (PowerShell).
# Owns: locating clang-format and forwarding check or fix mode to it.
# Depends: clang-format 20.x under VC/Tools/Llvm/x64/bin. No writes outside src.

[CmdletBinding()]
param(
    [switch]$Check
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$name = 'clang-format.exe'
$found = Get-Command $name -ErrorAction SilentlyContinue

if (-not $found) {
    $hint = Join-Path $env:ProgramFiles 'Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin'
    if (Test-Path -LiteralPath (Join-Path $hint $name)) {
        $found = @{ Source = (Join-Path $hint $name) }
    }
}

if (-not $found) {
    Write-Error "clang-format not found. Add it to PATH or install the VS LLVM tools."
    exit 2
}

$cf = if ($found -is [string]) { $found } else { $found.Source }
$sources = Get-ChildItem -Path (Join-Path $root 'src'), (Join-Path $root 'tests') `
    -Recurse -Include *.c, *.h, *.cpp, *.hpp |
    ForEach-Object { $_.FullName }

if (-not $sources) {
    Write-Error "no sources found under $root\src"
    exit 2
}

Push-Location $root
try {
    if ($Check) {
        & $cf -n --Werror --style=file @sources
        $code = $LASTEXITCODE
        if ($code -eq 0) {
            Write-Host "format: clean ($($sources.Count) files)"
        } else {
            Write-Host "format: drift found, run scripts\format.ps1 to fix"
        }
        exit $code
    }
    & $cf -i --style=file @sources
    Write-Host "format: rewrote $($sources.Count) files"
    exit 0
}
finally {
    Pop-Location
}
