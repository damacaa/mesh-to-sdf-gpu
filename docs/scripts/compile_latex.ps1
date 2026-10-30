# Compile script for LaTeX documents on Windows using WSL or local xelatex
param (
    [string]$Target = "docs/plantilla_memoria.tex"
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")

Write-Host "=== Compiling $Target using XeLaTeX ===" -ForegroundColor Cyan

# Check if local xelatex exists on Windows
$xelatexCmd = Get-Command xelatex -ErrorAction SilentlyContinue
if ($xelatexCmd) {
    Write-Host "Using local xelatex on Windows..." -ForegroundColor Green
    $TargetDir = Split-Path -Parent $Target
    $env:TEXINPUTS = ".;docs;"
    & xelatex -interaction=nonstopmode -output-directory="$TargetDir" "$Target"
    & xelatex -interaction=nonstopmode -output-directory="$TargetDir" "$Target"
} else {
    # Check if WSL is available
    $wslCmd = Get-Command wsl -ErrorAction SilentlyContinue
    if ($wslCmd) {
        Write-Host "Using containerized XeLaTeX in WSL via PistachioLinux/Debian..." -ForegroundColor Green
        # Convert path for WSL
        $drive = ($ProjectRoot -replace '^([A-Za-z]):.*', '$1').ToLower()
        $wslPath = $ProjectRoot -replace '^[A-Za-z]:', "/mnt/$drive" -replace '\\', '/'
        $wslTarget = $Target -replace '\\', '/'
        
        # Run compile_latex.sh in WSL
        wsl -d PistachioLinux bash -c "cd '$wslPath' && ./docs/scripts/compile_latex.sh '$wslTarget'"
    } else {
        Write-Error "Neither local xelatex nor WSL with podman was found."
    }
}

Write-Host "=== Compilation finished ===" -ForegroundColor Cyan
