# SPDX-License-Identifier: GPL-3.0-or-later
# Downloads a tournament word list for Tilefish, with its leave values (Windows; the
# easiest way is to double-click get-lexicon.bat).  CSW24 (Collins Scrabble Words 2024,
# the World Scrabble Championship's list) is the default; NWL23 is North America's.
# The files come from the MAGPIE project's public data (github.com/jvc56/MAGPIE-DATA),
# pinned to one commit and checked against their SHA-256 sums.  The word lists are
# copyrighted by their publishers, which is why they are not part of Tilefish itself.
param([string]$Lexicon = "CSW24")
$ErrorActionPreference = "Stop"
if ($Lexicon -ne "CSW24" -and $Lexicon -ne "NWL23") {
  Write-Host "usage: get-lexicon.bat [CSW24|NWL23]"
  exit 1
}
Set-Location $PSScriptRoot
# Windows PowerShell 5.1 may default to TLS 1.0, which GitHub refuses.
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$url = "https://github.com/jvc56/MAGPIE-DATA/raw/adf29316fcb2d7bd78a832e198c1bdfc112efa89/data/lexica"
$sums = @{
  "CSW24.kwg"  = "62ca7a84f07429a9976f77a4f74b94911aca5d49cce050dce72dd0e032c0566f"
  "CSW24.klv2" = "b0ef5f6637cca0cd8e0962d6fb7d35b2d22f9bf648015f48fa8f80e24a60610b"
  "NWL23.kwg"  = "3e74af981fdd974e107283f686da0fe4b7ec84ad0d825d444330c338c33b91ba"
  "NWL23.klv2" = "37dea945c29c3773eb4cd5a4117f3d3256c8b548cd3bf3b5ce8a219cb5e0a3fa"
}
# SHA-256 through .NET: Get-FileHash is missing from some Windows PowerShell 5.1 setups.
function Get-Sha256([string]$Path) {
  $stream = [System.IO.File]::OpenRead((Resolve-Path $Path).Path)
  try {
    $sha = [System.Security.Cryptography.SHA256]::Create()
    return ([System.BitConverter]::ToString($sha.ComputeHash($stream)) -replace "-", "").ToLower()
  } finally {
    $stream.Dispose()
  }
}
foreach ($f in @("$Lexicon.kwg", "$Lexicon.klv2")) {
  if ((Test-Path $f) -and ((Get-Sha256 $f) -eq $sums[$f])) {
    Write-Host "$f is already here"
    continue
  }
  Write-Host "downloading $f"
  Invoke-WebRequest -Uri "$url/$f" -OutFile "$f.part" -UseBasicParsing
  if ((Get-Sha256 "$f.part") -ne $sums[$f]) {
    Remove-Item "$f.part"
    Write-Host "error: $f did not download correctly (checksum mismatch)"
    exit 1
  }
  Move-Item -Force "$f.part" $f
}
if ($Lexicon -eq "CSW24") {
  Write-Host "Done. Start tilefish.exe: it now plays CSW24."
} else {
  Write-Host "Done. Start tilefish.exe --lexicon NWL23.kwg to play NWL23 (CSW24 wins when both are here)."
}
