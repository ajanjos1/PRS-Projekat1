@echo off
rem Omotac za scripts\demo.ps1 (zaobilazi execution policy samo za ovo pokretanje).
rem Upotreba: scripts\demo.cmd [-Full] [-SkipBuild] [-SkipTests] [-NoOpen]
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0demo.ps1" %*
exit /b %ERRORLEVEL%
