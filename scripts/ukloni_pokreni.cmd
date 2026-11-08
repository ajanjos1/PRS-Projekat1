@echo off
rem Ponistava scripts\instaliraj_pokreni.cmd: uklanja korijen projekta iz KORISNICKOG PATH-a.
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command ^
  "$root = (Resolve-Path -LiteralPath '%~dp0..').Path;" ^
  "$p = [Environment]::GetEnvironmentVariable('Path', 'User'); if (-not $p) { $p = '' };" ^
  "$rest = ($p -split ';' | Where-Object { $_ -and ($_ -ne $root) }) -join ';';" ^
  "[Environment]::SetEnvironmentVariable('Path', $rest, 'User'); Write-Host ('Uklonjeno iz PATH-a: ' + $root)"
exit /b %ERRORLEVEL%
