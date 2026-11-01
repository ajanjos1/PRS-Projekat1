# Demonstracija projekta (Windows): build -> info -> testovi -> benchmark -> STREAM -> simulacija -> grafikoni.
#
# Pokretanje (iz bilo kojeg foldera):
#     scripts\demo.cmd                 brza verzija, ~2-3 min (mreze 128,256,512; 3 ponavljanja)
#     scripts\demo.cmd -Full           kao u radu (mreze 128..1024; 10 ponavljanja), nekoliko minuta duze
#     scripts\demo.cmd -SkipBuild      preskace izgradnju (ako je build-vs\ vec izgradjen)
#     scripts\demo.cmd -SkipTests      preskace testove ispravnosti (~35 s)
#     scripts\demo.cmd -NoOpen         ne otvara folder sa grafikonima na kraju
#
# Sav izlaz ide u demo_out\ - folder results\ (podaci iz rada, mjereni na Linux VM) se NE dira.
# Napomena: ASCII tekst je namjeran (PowerShell 5.1 pogresno cita UTF-8 bez BOM-a).

param(
    [switch]$Full,
    [switch]$SkipBuild,
    [switch]$SkipTests,
    [switch]$NoOpen
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
$buildDir = Join-Path $root 'build-vs'
$out = Join-Path $root 'demo_out'
$figs = Join-Path $out 'figures'
$exeSwe = Join-Path $buildDir 'Release\prs_swe.exe'
$exeTests = Join-Path $buildDir 'Release\prs_tests.exe'
$env:PYTHONUTF8 = '1'
$clock = [Diagnostics.Stopwatch]::StartNew()

function Step($n, $text) {
    Write-Host ""
    Write-Host ("=== [{0}] {1}  (proteklo: {2:mm\:ss}) ===" -f $n, $text, $clock.Elapsed) -ForegroundColor Cyan
}

function Check($what) {
    if ($LASTEXITCODE -ne 0) { throw "$what nije uspjelo (exit code $LASTEXITCODE)" }
}

# Python sa numpy + matplotlib: py launcher, tipicne lokacije, pa PATH (bez Microsoft Store stuba).
function Find-Python {
    $cands = New-Object System.Collections.Generic.List[string]
    if (Get-Command py -ErrorAction SilentlyContinue) {
        $p = & py -3 -c "import sys; print(sys.executable)"
        if ($LASTEXITCODE -eq 0 -and $p) { $cands.Add(([string]$p).Trim()) }
    }
    $pattern = Join-Path $env:LOCALAPPDATA 'Programs\Python\Python3*\python.exe'
    Get-ChildItem $pattern -ErrorAction SilentlyContinue | Sort-Object FullName -Descending | ForEach-Object { $cands.Add($_.FullName) }
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
    # ---------------------------------------------------------------- hardver i parametri
    $cpu = Get-CimInstance Win32_Processor
    $cores = [int](($cpu | Measure-Object -Property NumberOfCores -Sum).Sum)
    $logical = [int](($cpu | Measure-Object -Property NumberOfLogicalProcessors -Sum).Sum)
    $cpuName = ($cpu | Select-Object -First 1).Name.Trim()
    $tl = @(1, 2, 4) | Where-Object { $_ -lt $cores }
    $threadList = ((@($tl) + $cores) | Sort-Object -Unique) -join ','
    if ($Full) { $sizes = '128,256,512,1024'; $reps = 10; $warm = 2 } else { $sizes = '128,256,512'; $reps = 3; $warm = 1 }

    Write-Host "Procesor: $cpuName ($cores fizickih / $logical logickih jezgri)"
    Write-Host "Mod: $(if ($Full) { 'PUNI' } else { 'BRZI' }), mreze $sizes, dretve $threadList, ponavljanja $reps"

    # ---------------------------------------------------------------- 1) build
    Step 1 'Izgradnja (Release, MSVC + OpenMP + AVX2)'
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
        cmake --build $buildDir --config Release -j
        Check 'CMake build'
    }
    if (-not (Test-Path $exeSwe)) { throw "Nema $exeSwe - pokrenite skriptu bez -SkipBuild." }

    New-Item -ItemType Directory -Force $out, $figs | Out-Null
    # plot_results.py cita "Core(s) per socket" iz system_info.txt (na Linuxu to daje lscpu)
    @("Datum: $(Get-Date -Format s)", "CPU: $cpuName", "Core(s) per socket: $cores", "CPU(s): $logical") |
        Set-Content -Path (Join-Path $out 'system_info.txt') -Encoding ASCII

    # ---------------------------------------------------------------- 2) info
    Step 2 'prs_swe info (CPU, AVX2, OpenMP, varijante)'
    & $exeSwe info
    Check 'prs_swe info'

    # ---------------------------------------------------------------- 3) testovi
    Step 3 'Testovi ispravnosti (sve varijante bit-identicne skalarnoj referenci)'
    if ($SkipTests) {
        Write-Host 'preskoceno (-SkipTests)'
    } else {
        & $exeTests (Join-Path $out 'correctness_report.csv') | Tee-Object -FilePath (Join-Path $out 'correctness_tests_output.txt')
        Check 'Testovi ispravnosti'
    }

    # ---------------------------------------------------------------- 4) benchmark
    Step 4 "Benchmark (A-G + ablacije), mreze $sizes"
    & $exeSwe bench --sizes $sizes --threads $threadList --reps $reps --warmup $warm --e2e-threads $cores `
        --out (Join-Path $out 'benchmarks.csv') --runs-out (Join-Path $out 'benchmark_runs.csv') |
        Tee-Object -FilePath (Join-Path $out 'benchmark_output.txt')
    Check 'prs_swe bench'

    # ---------------------------------------------------------------- 5) STREAM
    Step 5 'STREAM triad (propusnost memorije)'
    & $exeSwe stream --threads "1,$cores" --out (Join-Path $out 'stream.csv')
    Check 'prs_swe stream'

    # ---------------------------------------------------------------- 6) simulacija
    Step 6 'Simulacija 512x512, 6000 koraka (30 s simuliranog vremena), varijanta G'
    & $exeSwe simulate --size 512 --steps 6000 --variant G_omp_avx2 --threads $cores --snap 1500 --out (Join-Path $out 'sim512')
    Check 'prs_swe simulate'

    # ---------------------------------------------------------------- 7) grafikoni
    Step 7 'Grafikoni (Python: numpy + matplotlib)'
    $py = Find-Python
    if (-not $py) {
        Write-Host 'Python sa numpy/matplotlib nije pronadjen - grafikoni se preskacu.' -ForegroundColor Yellow
        Write-Host 'Instalacija: winget install Python.Python.3.13 ; zatim: py -m pip install numpy matplotlib' -ForegroundColor Yellow
    } else {
        Write-Host "Python: $py"
        & $py (Join-Path $root 'scripts\visualize_simulation.py') (Join-Path $out 'sim512') $figs
        Check 'visualize_simulation.py'
        & $py (Join-Path $root 'scripts\plot_results.py') $out $figs
        Check 'plot_results.py'
    }

    # ---------------------------------------------------------------- kraj
    Write-Host ""
    Write-Host ("=== GOTOVO za {0:mm\:ss} ===" -f $clock.Elapsed) -ForegroundColor Green
    Write-Host "Izlaz: $out"
    if (Test-Path $figs) {
        Get-ChildItem $figs -File | ForEach-Object { Write-Host "  figures\$($_.Name)" }
        if (-not $NoOpen) { Start-Process explorer.exe $figs }
    }
}
catch {
    Write-Host ""
    Write-Host "GRESKA: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
