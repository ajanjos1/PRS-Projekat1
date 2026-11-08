@echo off
setlocal
rem ---------------------------------------------------------------------------------------------
rem  Pokreni - izgradi (samo izmijenjene fajlove) i pokreni prs_erosion iz korijena projekta.
rem
rem    Pokreni                 interaktivni meni (karta, fajl, izlaz)
rem    Pokreni test            samo testovi (prs_erosion_tests)
rem    Pokreni --input X ...   komandna linija; svi argumenti se prosljedjuju programu
rem
rem  Iz PowerShella u folderu projekta: .\Pokreni
rem  Da radi samo "Pokreni" (iz bilo kojeg foldera): jednom pokrenite scripts\instaliraj_pokreni.cmd
rem ---------------------------------------------------------------------------------------------

rem Uvijek radi iz korijena projekta (tu su tools\, tereni\ i output\), bez obzira odakle je pozvano.
cd /d "%~dp0"

set "CMAKE=cmake"
where cmake >nul 2>nul
if errorlevel 1 call :find_vs_cmake
"%CMAKE%" --version >nul 2>nul
if errorlevel 1 goto :no_cmake

if exist "build-vs\CMakeCache.txt" goto :build
echo Prva konfiguracija projekta - CMake bira najnoviji instalirani Visual Studio...
"%CMAKE%" -S . -B build-vs -A x64
if errorlevel 1 goto :fail

:build
echo Gradim projekat - samo izmijenjeni fajlovi...
"%CMAKE%" --build build-vs --config Release --target prs_erosion prs_erosion_tests -- /nologo /verbosity:minimal
if errorlevel 1 goto :fail
echo.

if /i "%~1"=="test" goto :tests
"build-vs\Release\prs_erosion.exe" %*
exit /b %ERRORLEVEL%

:tests
"build-vs\Release\prs_erosion_tests.exe"
exit /b %ERRORLEVEL%

:find_vs_cmake
rem CMake koji dolazi uz Visual Studio (kad cmake nije u PATH-u obicnog terminala).
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto :eof
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -prerelease -products * -find Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`) do set "CMAKE=%%i"
goto :eof

:no_cmake
echo GRESKA: CMake nije pronadjen. Instalirajte CMake ili Visual Studio sa C++ podrskom,
echo         ili pokrenite iz "Developer PowerShell for VS".
exit /b 1

:fail
echo.
echo GRESKA: build nije uspio - pogledajte poruke iznad.
exit /b 1
