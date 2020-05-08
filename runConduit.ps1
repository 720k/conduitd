
param(
    [switch]$Debug,
    [int]$Port=60999
)
$ScriptDir = Split-Path $script:MyInvocation.MyCommand.Path
Write-Host "Dir = $ScriptDir"
if ($Debug) {
    $env:g_messages_debug="all"
    write-host("Launch with DEBUG")
} else {
    write-host("Launch NO DEBUG")
}
Write-Host("Listening to port: $Port")

$cmd ="$ScriptDir\conduitd.exe --no-service --port $Port"
Invoke-Expression $cmd
