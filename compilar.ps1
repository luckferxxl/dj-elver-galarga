# Compila DJ Elver Galarga, ejecuta las pruebas y genera el instalador y el ZIP de código.
# Uso:  powershell -ExecutionPolicy Bypass -File compilar.ps1 [-SinPruebas] [-SinInstalador]
param([switch]$SinPruebas, [switch]$SinInstalador)
$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

function Paso($t) { Write-Host "`n== $t" -ForegroundColor Cyan }

Paso "Configurar (CMake + Ninja + MinGW-w64)"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE) { throw "Fallo al configurar" }

Paso "Compilar"
cmake --build build
if ($LASTEXITCODE) { throw "Fallo al compilar" }

if (-not $SinPruebas) {
    Paso "Pruebas automáticas"
    ctest --test-dir build --output-on-failure
    if ($LASTEXITCODE) { throw "Hay pruebas que fallan" }
}

if (-not $SinInstalador) {
    Paso "Instalador (Inno Setup 6)"
    $iscc = @("$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe", "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:ProgramFiles\Inno Setup 6\ISCC.exe") |
        Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $iscc) { throw "No se encuentra Inno Setup 6 (winget install JRSoftware.InnoSetup)" }
    & $iscc /Q installer\DJElverGalarga.iss
    if ($LASTEXITCODE) { throw "Fallo al crear el instalador" }

    Paso "ZIP con el código fuente"
    $zip = "dist\DJElverGalarga-codigo-fuente-0.14.0.zip"
    if (Test-Path $zip) { Remove-Item $zip }
    $tmp = Join-Path $env:TEMP "DJElverGalarga-src"
    if (Test-Path $tmp) { Remove-Item $tmp -Recurse -Force }
    New-Item -ItemType Directory $tmp | Out-Null
    $incluir = @("src", "tests", "tools", "res", "installer", "licencias", "docs", "third_party", "CMakeLists.txt", "compilar.ps1", "LEEME.md", "PROGRESO.md", "COPYING.txt")
    foreach ($i in $incluir) { if (Test-Path $i) { Copy-Item $i -Destination $tmp -Recurse } }
    Get-ChildItem $tmp -Recurse -Force -Directory -Filter ".git" | Remove-Item -Recurse -Force
    # Partes de terceros que no hacen falta para compilar
    foreach ($d in @("third_party\imgui\examples", "third_party\imgui\docs", "third_party\miniaudio\tests", "third_party\miniaudio\website", "third_party\miniaudio\research", "third_party\miniaudio\examples", "third_party\miniaudio\data")) {
        $p = Join-Path $tmp $d
        if (Test-Path $p) { Remove-Item $p -Recurse -Force }
    }
    Compress-Archive -Path "$tmp\*" -DestinationPath $zip
    Remove-Item $tmp -Recurse -Force
}
Write-Host "`nListo. Resultados en .\dist" -ForegroundColor Green
