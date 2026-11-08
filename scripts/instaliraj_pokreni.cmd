@echo off
rem Jednom pokrenuti: dodaje korijen projekta u KORISNICKI PATH, pa komanda "Pokreni" radi u svakom novom
rem terminalu (PowerShell ili cmd), iz bilo kojeg foldera. Ne treba administrator. Ponisti: scripts\ukloni_pokreni.cmd
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command ^
  "$root = (Resolve-Path -LiteralPath '%~dp0..').Path;" ^
  "$p = [Environment]::GetEnvironmentVariable('Path', 'User'); if (-not $p) { $p = '' };" ^
  "if (($p -split ';') -contains $root) { Write-Host ('Vec je u PATH-u: ' + $root) }" ^
  "else { [Environment]::SetEnvironmentVariable('Path', (($p.TrimEnd(';') + ';' + $root).TrimStart(';')), 'User'); Write-Host ('Dodano u PATH: ' + $root) };" ^
  "Write-Host 'Zatvorite ovaj terminal, otvorite novi i ukucajte: Pokreni'"
exit /b %ERRORLEVEL%
