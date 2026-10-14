param([string]$InfDir = "..\x64\Release")
$inf = Join-Path $InfDir "vhidlab.inf"
pnputil /add-driver $inf /install
Write-Host "若设备管理器里还没有设备，用 WDK 的 devgen 创建："
Write-Host '  devgen /add /hardwareid "root\vhidlab"'
