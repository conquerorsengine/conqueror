# .cqspace dosyalarını Conqueror Player ile açtırır (264): yalnız bu kullanıcı için (HKCU, yönetici izni gerekmez).
# usage: powershell -ExecutionPolicy Bypass -File Tools\Windows\register-cqspace.ps1 <ConquerorRuntime.exe'nin klasörü>
param([Parameter(Mandatory = $true)][string]$Bin)
$exe = Join-Path (Resolve-Path $Bin) "ConquerorRuntime.exe"
if (-not (Test-Path $exe)) { Write-Error "no ConquerorRuntime.exe in $Bin"; exit 1 }
$classes = "HKCU:\Software\Classes"
New-Item -Path "$classes\.cqspace" -Force | Out-Null
Set-ItemProperty -Path "$classes\.cqspace" -Name "(Default)" -Value "ConquerorEngine.Game"
Set-ItemProperty -Path "$classes\.cqspace" -Name "Content Type" -Value "application/x-cqspace"
New-Item -Path "$classes\ConquerorEngine.Game\DefaultIcon" -Force | Out-Null
Set-ItemProperty -Path "$classes\ConquerorEngine.Game" -Name "(Default)" -Value "Conqueror game"
Set-ItemProperty -Path "$classes\ConquerorEngine.Game\DefaultIcon" -Name "(Default)" -Value "`"$exe`",0"
New-Item -Path "$classes\ConquerorEngine.Game\shell\open\command" -Force | Out-Null
Set-ItemProperty -Path "$classes\ConquerorEngine.Game\shell\open\command" -Name "(Default)" -Value "`"$exe`" `"%1`""
Write-Output ".cqspace files now open with $exe"
