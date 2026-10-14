# 生成自签测试证书并导入 本地计算机 Root + TrustedPublisher，用于给驱动签名。
$cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject "CN=VhidLab Test" `
    -CertStoreLocation Cert:\CurrentUser\My -KeyUsage DigitalSignature -FriendlyName "VhidLab Test"
$pwd = ConvertTo-SecureString -String "vhidlab" -Force -AsPlainText
Export-PfxCertificate -Cert $cert -FilePath "$PSScriptRoot\vhidlab.pfx" -Password $pwd | Out-Null
Export-Certificate -Cert $cert -FilePath "$PSScriptRoot\vhidlab.cer" | Out-Null
Import-Certificate -FilePath "$PSScriptRoot\vhidlab.cer" -CertStoreLocation Cert:\LocalMachine\Root | Out-Null
Import-Certificate -FilePath "$PSScriptRoot\vhidlab.cer" -CertStoreLocation Cert:\LocalMachine\TrustedPublisher | Out-Null
Write-Host "证书已生成并信任：$($cert.Thumbprint)"
