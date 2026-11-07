# Store-and-forward test: stop the MQTT broker, start it again, then ask InfluxDB
# whether any reading is missing. Needs the server stack and a running device.
#
#   .\tools\outage-test.ps1               # 90 s outage
#   .\tools\outage-test.ps1 -Seconds 300
param(
    [int] $Seconds = 90,
    [string] $Node = "node1"
)

$server = Join-Path (Split-Path $PSScriptRoot) "server"
$period = 5   # PUBLISH_PERIOD_MS / 1000 in main/app_config.h

$stopped = (Get-Date).ToUniversalTime()
docker compose --project-directory $server stop mosquitto
Write-Host "Broker stopped at $($stopped.ToLocalTime().ToString('HH:mm:ss')), the device is buffering now..."
Start-Sleep -Seconds $Seconds
docker compose --project-directory $server start mosquitto
$started = (Get-Date).ToUniversalTime()
Write-Host "Broker started at $($started.ToLocalTime().ToString('HH:mm:ss')). Waiting 60 s for the re-send..."
Start-Sleep -Seconds 60

# Largest time step between two consecutive readings, from 1 min before the outage until now
$from = $stopped.AddMinutes(-1).ToString("yyyy-MM-ddTHH:mm:ssZ")
$flux = @"
from(bucket: "sensors")
  |> range(start: $from)
  |> filter(fn: (r) => r._measurement == "environment" and r._field == "temperature" and r.node == "$Node")
  |> elapsed(unit: 1s)
  |> group()
  |> reduce(identity: {n: 0, gap: 0}, fn: (r, accumulator) => ({
       n: accumulator.n + 1,
       gap: if r.elapsed > accumulator.gap then r.elapsed else accumulator.gap }))
"@
$csv = Invoke-RestMethod -Method Post -Uri "http://localhost:8086/api/v2/query?org=iot" `
    -Headers @{ Authorization = "Token iot-node-dev-token"; Accept = "application/csv" } `
    -ContentType "application/vnd.flux" -Body $flux
$result = $csv | ConvertFrom-Csv -WarningAction SilentlyContinue | Select-Object -First 1
if ($null -eq $result -or $result.n -eq "") {
    Write-Host "No readings found for node '$Node' - is the device running?" -ForegroundColor Red
    exit 1
}

$gap = [int]$result.gap
Write-Host "$($result.n) readings since $($stopped.AddMinutes(-1).ToLocalTime().ToString('HH:mm:ss')), largest step between two of them: $gap s"
if ($gap -le $period + 1) {
    Write-Host "PASS: no gap, the readings from the outage were delivered" -ForegroundColor Green
} else {
    Write-Host "FAIL: $gap s without readings (expected about $period s)" -ForegroundColor Red
    exit 1
}
