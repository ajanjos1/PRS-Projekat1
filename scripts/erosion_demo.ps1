# Demonstracija faze 2 (Windows): build -> testovi -> simulacija erozije na primjerima height mapa -> slike.
#
# Pokretanje (iz bilo kojeg foldera):
#     scripts\erosion_demo.cmd                      primjer_1 (412x221), 100 L/m2 za 1 h  (~10-20 s simulacije)
#     scripts\erosion_demo.cmd -AllMaps             sva tri primjera (primjer_3 je 736x736: oko 1 min za 1 h kise)
#     scripts\erosion_demo.cmd -Rain 50 -Hours 2    druga kisa
#     scripts\erosion_demo.cmd -SkipBuild -SkipTests -NoOpen
#
# Izlaz: output\<ime_mape>\ (erodirana 16-bitna PNG height mapa, razlika, poredjenje, izvjestaj).
# Napomena: ASCII tekst je namjeran (PowerShell 5.1 pogresno cita UTF-8 bez BOM-a).

param(
    [double]$Rain = 100,
    [double]$Hours = 1,
    [switch]$AllMaps,
    [switch]$SkipBuild,
    [switch]$SkipTests,
    [switch]$NoOpen
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
$buildDir = Join-Path $root 'build-vs'
$exe = Join-Path $buildDir 'Release\prs_erosion.exe'
$exeTests = Join-Path $buildDir 'Release\prs_erosion_tests.exe'
$clock = [Diagnostics.Stopwatch]::StartNew()

function Step($n, $text) {
    Write-Host ""
    Write-Host ("=== [{0}] {1}  (proteklo: {2:mm\:ss}) ===" -f $n, $text, $clock.Elapsed) -ForegroundColor Cyan
}

function Check($what) {
    if ($LASTEXITCODE -ne 0) { throw "$what nije uspjelo (exit code $LASTEXITCODE)" }
}

function Find-Python {
    $cands = New-Object System.Collections.Generic.List[string]
    if (Get-Command py -ErrorAction SilentlyContinue) {
        $p = & py -3 -c "import sys; print(sys.executable)"
        if ($LASTEXITCODE -eq 0 -and $p) { $cands.Add(([string]$p).Trim()) }
    }
    Get-Command python -All -ErrorAction SilentlyContinue | Where-Object { $_.Source -notlike '*\WindowsApps\*' } | ForEach-Object { $cands.Add($_.Source) }
    foreach ($c in $cands) {
        if (Test-Path $c) {
            & $c -c "import numpy, matplotlib" 2>$null
            if ($LASTEXITCODE -eq 0) { return $c }
        }
    }
    return $null
}

try {
    # ---------------------------------------------------------------- 1) build
    Step 1 'Izgradnja (Release; serijski kod, bez OpenMP/AVX2)'
    if ($SkipBuild) {
        Write-Host 'preskoceno (-SkipBuild)'
    } else {
        if (-not (Test-Path (Join-Path $buildDir 'CMakeCache.txt'))) {
            $help = (cmake --help | Out-String)
            if ($help -match 'Visual Studio 18 2026') { $gen = 'Visual Studio 18 2026' }
            elseif ($help -match 'Visual Studio 17 2022') { $gen = 'Visual Studio 17 2022' }
            else { throw 'Nije pronadjen Visual Studio 2022/2026 generator u CMake-u.' }
            cmake -G $gen -A x64 -S $root -B $buildDir
            Check 'CMake konfiguracija'
        }
        cmake --build $buildDir --config Release --target prs_erosion prs_erosion_tests -j
        Check 'CMake build'
    }
    if (-not (Test-Path $exe)) { throw "Nema $exe - pokrenite skriptu bez -SkipBuild." }

    # ---------------------------------------------------------------- 2) testovi
    Step 2 'Testovi 1-10 (prs_erosion_tests)'
    if ($SkipTests) {
        Write-Host 'preskoceno (-SkipTests)'
    } else {
        & $exeTests
        Check 'Testovi'
    }

    # ---------------------------------------------------------------- 3) simulacije
    $maps = @('assets\heightmaps\primjer_1\height-map-input-try-1-height-map-16bit-texturemap-app-2026-11-08.png')
    if ($AllMaps) {
        $maps += 'assets\heightmaps\primjer_2\mocvarno-tlo-height-map-16bit-texturemap-app-2026-11-08.png'
        $maps += 'assets\heightmaps\primjer_3\mali-otocic-height-map-16bit-texturemap-app-2026-11-08.png'
    }
    $i = 0
    $outDirs = @()
    foreach ($m in $maps) {
        $i++
        $name = 'demo_' + [IO.Path]::GetFileNameWithoutExtension($m).Substring(0, [Math]::Min(20, [IO.Path]::GetFileNameWithoutExtension($m).Length))
        $outDir = Join-Path 'output' $name
        Step (2 + $i) "Simulacija: $m, $Rain L/m2 za $Hours h"
        & $exe --input $m --rain $Rain --hours $Hours --out $outDir
        Check 'prs_erosion'
        $outDirs += $outDir
    }

    # ---------------------------------------------------------------- 4) slike (opciono)
    Step (3 + $i) 'Uporedni prikaz (Python: numpy + matplotlib, opciono)'
    $py = Find-Python
    if (-not $py) {
        Write-Host 'Python sa numpy/matplotlib nije pronadjen - preskoceno (PNG izlazi programa su vec napravljeni).' -ForegroundColor Yellow
    } else {
        foreach ($d in $outDirs) {
            & $py (Join-Path $root 'scripts\visualize_results.py') $d --save (Join-Path $d 'figure_original_vs_eroded.png')
            Check 'visualize_results.py'
        }
    }

    Write-Host ""
    Write-Host ("Gotovo za {0:mm\:ss}. Rezultati: {1}" -f $clock.Elapsed, (Join-Path $root 'output')) -ForegroundColor Green
    if (-not $NoOpen) { Invoke-Item (Join-Path $root 'output') }
}
catch {
    Write-Host ""
    Write-Host "GRESKA: $_" -ForegroundColor Red
    exit 1
}
