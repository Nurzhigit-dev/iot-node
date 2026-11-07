# Build the firmware as a new version and offer it to the device over MQTT.
# Run it from an ESP-IDF PowerShell (VS Code: "ESP-IDF: Open ESP-IDF Terminal"),
# with the server stack running.
#
#   .\tools\ota.ps1 -Version 1.0.1                        # Wokwi simulator
#   .\tools\ota.ps1 -Version 1.0.1 -Server 192.168.30.7   # real board: your PC's IP
param(
    [Parameter(Mandatory = $true)] [string] $Version,
    [string] $Server = "host.wokwi.internal",
    [string] $Topic = "iot-node-6g7l/node1/ota"
)

$root = Split-Path $PSScriptRoot
Set-Location $root

# Separate build folder: rebuilding build\ would restart a running Wokwi simulation.
# The quotes matter: PowerShell splits -DPROJECT_VER=1.0.1 at the dots otherwise.
idf.py -B build-ota "-DPROJECT_VER=$Version" build
if ($LASTEXITCODE -ne 0) { Write-Host "Build failed" -ForegroundColor Red; exit 1 }

$name = "iot-node-$Version.bin"
Copy-Item build-ota\iot-node.bin "server\firmware\$name"
$url = "http://${Server}:8000/$name"

docker exec mosquitto mosquitto_pub -q 1 -t $Topic -m $url
if ($LASTEXITCODE -ne 0) { Write-Host "Publishing failed: is the server stack running?" -ForegroundColor Red; exit 1 }

Write-Host "Offered $url to the device."
Write-Host "Its log should show: ota: Updating <old> -> $Version ... New firmware confirmed"
