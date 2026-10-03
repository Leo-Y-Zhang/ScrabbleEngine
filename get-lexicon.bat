@echo off
rem Downloads the CSW24 word list (or NWL23: get-lexicon.bat NWL23) and its leave values
rem for Tilefish.  Double-click it once; see get-lexicon.ps1 for where the files come from.
setlocal
rem Started from a PowerShell 7 window, this file inherits PowerShell 7's module path,
rem and Windows PowerShell 5.1 would then load PowerShell 7's modules instead of its own
rem (Get-FileHash, for one, goes missing).  With the variable unset, 5.1 uses its own.
set "PSModulePath="
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0get-lexicon.ps1" %1
pause
