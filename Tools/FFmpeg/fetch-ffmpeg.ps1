# Recorder (249) için ffmpeg + ffprobe indirir (Windows): depoya konmaz (büyük ve GPL; motor onu ayrı bir program olarak boru üzerinden
# çalıştırır). Varsayılan hedef Recorder'ın baktığı %APPDATA%\ConquerorEngine\ffmpeg; sürüm paketi için editörün yanındaki Tools\ffmpeg:
#
#   powershell -ExecutionPolicy Bypass -File Tools\FFmpeg\fetch-ffmpeg.ps1 [hedef klasör]
#
# Kaynak: gyan.dev "release essentials" derlemesi, yayımlanan SHA-256 ile doğrulanır.
param([string]$Dest = (Join-Path $env:APPDATA "ConquerorEngine\ffmpeg"))
$ErrorActionPreference = "Stop"
$base = "https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-essentials.zip"
$tmp = Join-Path ([System.IO.Path]::GetTempPath()) ("cq-ffmpeg-" + [guid]::NewGuid())
New-Item -ItemType Directory -Force -Path $tmp, $Dest | Out-Null
try {
    $zip = Join-Path $tmp "ffmpeg.zip"
    Invoke-WebRequest -UseBasicParsing -Uri $base -OutFile $zip
    $want = ((Invoke-WebRequest -UseBasicParsing -Uri "$base.sha256").Content.Trim() -split '\s+')[0].ToLower()
    $got = (Get-FileHash -Algorithm SHA256 $zip).Hash.ToLower()
    if ($want -ne $got) { throw "Checksum mismatch ($got, expected $want): download not used." }
    Expand-Archive -Force -Path $zip -DestinationPath $tmp
    $bin = Get-ChildItem -Path $tmp -Recurse -Filter ffmpeg.exe | Select-Object -First 1
    Copy-Item -Force (Join-Path $bin.DirectoryName "ffmpeg.exe"), (Join-Path $bin.DirectoryName "ffprobe.exe") $Dest
    $license = Get-ChildItem -Path $tmp -Recurse -Filter LICENSE | Select-Object -First 1
    if ($license) { Copy-Item -Force $license.FullName (Join-Path $Dest "LICENSE.txt") }
    Write-Host "ffmpeg is in $Dest"
} finally {
    Remove-Item -Recurse -Force $tmp
}
