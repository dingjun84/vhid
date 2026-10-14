# 管理员运行。开启测试签名，重启后生效。
bcdedit /set testsigning on
Write-Host "已开启测试签名，请重启。桌面右下角会出现 Test Mode。"
