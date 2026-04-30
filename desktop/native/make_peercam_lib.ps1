# Generates peercam_node.def and peercam_node.lib pointing to PeerCam.exe
$nodeLib = "$env:USERPROFILE\AppData\Local\node-gyp\Cache\34.5.8\x64\node.lib"
$vsBase  = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC"
$msvcVer = Get-ChildItem $vsBase | Sort-Object Name -Descending | Select-Object -First 1
$lib     = "$vsBase\$($msvcVer.Name)\bin\Hostx64\x64\lib.exe"

# Extract all napi_, node_api_, uv_ symbols
$raw = & dumpbin /exports $nodeLib 2>&1
$symbols = $raw | Where-Object { $_ -match '^\s+(napi_|node_api_|uv_)\S+$' } |
           ForEach-Object { $_.Trim() }

$def = @("LIBRARY PeerCam.exe", "EXPORTS")
$def += $symbols
$def | Set-Content -Path "peercam_node.def" -Encoding ASCII

Write-Host "Generated peercam_node.def with $($symbols.Count) symbols"

# Generate the import lib
& $lib /def:peercam_node.def /out:peercam_node.lib /machine:x64
Write-Host "Generated peercam_node.lib"
