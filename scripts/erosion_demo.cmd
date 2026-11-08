@echo off
rem Omotac za scripts\erosion_demo.ps1 (zaobilazi execution policy samo za ovo pokretanje).
rem Upotreba: scripts\erosion_demo.cmd [-Rain 100] [-Hours 1] [-AllMaps] [-SkipBuild] [-SkipTests] [-NoOpen]
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0erosion_demo.ps1" %*
exit /b %ERRORLEVEL%
