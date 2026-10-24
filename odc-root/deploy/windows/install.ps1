# ── odc-hook Windows Install Script ──────────────────────────────────────────
param(
    [switch]$InstallService,
    [string]$TargetDir = "C:\Program Files\odc-root",
    [string]$ConfigDir = "C:\ProgramData\odc-root"
)

$ErrorActionPreference = "Stop"
$Binary = "odc-hook.exe"
$Config = "odc-hook.yaml"

Write-Host "=== Installing odc-hook (Windows) ==="

New-Item -ItemType Directory -Force -Path $TargetDir, "$ConfigDir\hook", "$ConfigDir\ca" | Out-Null

Write-Host "  Binary: $TargetDir\$Binary"
Copy-Item $Binary "$TargetDir\$Binary" -Force

Write-Host "  Config: $ConfigDir\$Config"
Copy-Item $Config "$ConfigDir\$Config" -Force

if ($InstallService) {
    Write-Host "  Registering Windows service..."
    New-Service -Name "odc-hook" `
        -BinaryPathName "`"$TargetDir\$Binary`" --config `"$ConfigDir\$Config`" --daemon" `
        -DisplayName "odc-hook — mTLS WebSocket event hook" `
        -Description "Cross-platform event hook client with mTLS WebSocket" `
        -StartupType Automatic `
        -ErrorAction SilentlyContinue
    Write-Host "  Service installed. Start with: Start-Service odc-hook"
} else {
    Write-Host ""
    Write-Host "  To run manually:"
    Write-Host "    & `"$TargetDir\$Binary`" --config `"$ConfigDir\$Config`""
    Write-Host ""
    Write-Host "  To install as a Windows service:"
    Write-Host "    .\install.ps1 -InstallService"
}

Write-Host "=== odc-hook installation complete ==="
