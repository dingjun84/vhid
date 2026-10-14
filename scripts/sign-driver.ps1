param([string]$SysDir = "..\x64\Release")
$pfx = Join-Path $PSScriptRoot "vhidlab.pfx"
$sys = Join-Path $SysDir "vhidlab.sys"
$cat = Join-Path $SysDir "vhidlab.cat"
if (-not (Test-Path $pfx)) { throw "先运行 make-testcert.ps1" }
if (-not (Test-Path $sys)) { throw "找不到 $sys" }
signtool sign /fd sha256 /f $pfx /p vhidlab $sys
if (Test-Path $cat) { signtool sign /fd sha256 /f $pfx /p vhidlab $cat }
Write-Host "签名完成: $sys"
