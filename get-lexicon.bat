@echo off
rem Downloads the CSW24 word list (or NWL23: get-lexicon.bat NWL23) and its leave values
rem for Tilefish.  Double-click it once; see get-lexicon.ps1 for where the files come from.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0get-lexicon.ps1" %1
pause
