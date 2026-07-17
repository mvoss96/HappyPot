# Regenerate the Matter data model from src/default_zap/happypot.zap.
#
# NCS builds the Matter data model with BYPASS_IDL, meaning src/default_zap/zap-generated/
# (endpoint_config.h and friends) is compiled exactly as checked in -- editing happypot.zap
# alone changes nothing. Run this after every ZAP edit and commit the result.
#
# The ZAP tool is not part of NCS; fetch the release pinned by CHIP's MIN_ZAP_VERSION
# (scripts/tools/zap/zap_execution.py) from github.com/project-chip/zap/releases and point
# -ZapDir at it. The .zap's own `package` paths are relative and do not resolve from
# out-of-tree apps, so we pass --zcl / --generationTemplate explicitly (zap-cli ignores the
# in-file packages by default anyway).
#
# --tempState is not optional: matter-idl-server.json and app-templates.json declare the SAME
# package name and version ("CHIP Application templates" / chip-v1). ZAP keys its package cache
# in ~/.zap/zap.sqlite by that pair, so on a second run the first template package is matched
# and the wrong generator silently runs -- the IDL step then quietly emits app-template files
# and no Clusters.matter. A fresh state directory per invocation keeps the two apart.

param(
    [string]$Ncs = "C:\ncs\v3.4.0",
    [string]$ZapDir = "C:\Repos\nrf\_zap\zap"
)

$ErrorActionPreference = "Stop"

$zapCli    = Join-Path $ZapDir "zap-cli.exe"
$matterDir = Join-Path $Ncs "modules\lib\matter"
$zcl       = Join-Path $matterDir "src\app\zap-templates\zcl\zcl.json"
$idlTpl    = Join-Path $matterDir "src\app\zap-templates\matter-idl-server.json"
$appTpl    = Join-Path $matterDir "src\app\zap-templates\app-templates.json"

$here    = Split-Path -Parent $PSScriptRoot
$zapFile = Join-Path $here "src\default_zap\happypot.zap"
$genDir  = Join-Path $here "src\default_zap\zap-generated"
$idlOut  = Join-Path $here "src\default_zap\happypot.matter"

foreach ($p in @($zapCli, $zcl, $idlTpl, $appTpl, $zapFile)) {
    if (-not (Test-Path $p)) { throw "not found: $p" }
}

$tmp = Join-Path ([System.IO.Path]::GetTempPath()) "happypot-zap-idl"
New-Item -ItemType Directory -Force -Path $tmp | Out-Null

Write-Host "==> .matter IDL"
& $zapCli generate --tempState -z $zcl -g $idlTpl -i $zapFile -o $tmp
if ($LASTEXITCODE -ne 0) { throw "zap-cli (idl) failed" }
Move-Item -Force (Join-Path $tmp "Clusters.matter") $idlOut

Write-Host "==> zap-generated/"
& $zapCli generate --tempState -z $zcl -g $appTpl -i $zapFile -o $genDir
if ($LASTEXITCODE -ne 0) { throw "zap-cli (app-templates) failed" }

Write-Host "done: $idlOut + $genDir"
