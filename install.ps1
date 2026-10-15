# Kodas one-line installer for Windows (PowerShell)
# Usage: iwr -useb https://raw.githubusercontent.com/MudasirJamali777/kodas/main/install.ps1 | iex

$ErrorActionPreference = "Stop"

$Repo = "MudasirJamali777/kodas"
$InstallDir = "$env:LOCALAPPDATA\Kodas"

Write-Host "Installing Kodas..." -ForegroundColor Cyan

# Check for C compiler
$cc = $null
if (Get-Command clang -ErrorAction SilentlyContinue) {
    $cc = "clang"
} elseif (Get-Command gcc -ErrorAction SilentlyContinue) {
    $cc = "gcc"
} else {
    Write-Host "Error: Please install clang (via LLVM) or gcc (via MinGW)" -ForegroundColor Red
    Write-Host "  winget install LLVM" -ForegroundColor Yellow
    exit 1
}

$TmpDir = Join-Path $env:TEMP "kodas-install-$(Get-Random)"
New-Item -ItemType Directory -Path $TmpDir | Out-Null

Push-Location $TmpDir
Write-Host "  Downloading source..."
Invoke-WebRequest -Uri "https://github.com/$Repo/archive/main.zip" -OutFile "kodas.zip"
Expand-Archive -Path "kodas.zip" -DestinationPath .
Set-Location "kodas-main\seed"

Write-Host "  Building..."
if ($cc -eq "clang") {
    & clang -I src -std=c11 -O2 src\*.c -o kodas.exe
} else {
    & gcc -I src -std=c11 -O2 src\*.c -o kodas.exe -lm
}

New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
Copy-Item kodas.exe $InstallDir\kodas.exe

# Add to PATH
$UserPath = [Environment]::GetEnvironmentVariable("Path", "User")
if ($UserPath -notlike "*$InstallDir*") {
    [Environment]::SetEnvironmentVariable("Path", "$UserPath;$InstallDir", "User")
    $env:Path += ";$InstallDir"
}

Pop-Location
Remove-Item -Recurse -Force $TmpDir

Write-Host ""
Write-Host "  Kodas installed to $InstallDir\kodas.exe" -ForegroundColor Green
Write-Host "  (Restart your terminal for PATH changes to take effect)"
Write-Host ""
Write-Host "Get started:"
Write-Host '  kodas repl'
Write-Host '  ''show "Hello, Kodas!"'' | Out-File hello.kds; kodas run hello.kds'
