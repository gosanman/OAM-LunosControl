# ============================================================================
# Simulator bauen: Kern (kwlsim.exe) und Metadaten (meta.json).
#
# Voraussetzung: include/knxprod.h und src/Kwl.debug.xml existieren, also
#   OpenKNXproducer create --Debug -h include/knxprod.h src/Kwl
# ist gelaufen. Beide sind generiert und nicht im Repository.
#
# Aufruf aus dem Projektordner:  powershell -File sim/build.ps1
# ============================================================================
param(
    # Anderer Name fuer den Kern, z. B. wenn die App laeuft und kwlsim.exe sperrt:
    #   powershell -File sim/build.ps1 -Exe kwlsim_test.exe
    # Die App nimmt ihn mit der Umgebungsvariable KWLSIM_EXE.
    [string]$Exe = 'kwlsim.exe'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$sim = $PSScriptRoot
$out = Join-Path $sim 'build'
New-Item -ItemType Directory -Force $out | Out-Null

foreach ($f in @('include/knxprod.h', 'src/Kwl.debug.xml')) {
    if (-not (Test-Path (Join-Path $root $f))) {
        throw "$f fehlt - zuerst OpenKNXproducer laufen lassen (siehe Kopf dieser Datei)."
    }
}

# --- Compiler: MinGW-GCC aus winget, sonst g++ im PATH ----------------------
$gpp = Get-ChildItem "$env:LOCALAPPDATA\Microsoft\WinGet\Packages" -Recurse -Filter g++.exe -ErrorAction SilentlyContinue |
    Select-Object -First 1 -ExpandProperty FullName
if (-not $gpp) { $gpp = (Get-Command g++ -ErrorAction SilentlyContinue).Source }
if (-not $gpp) { throw 'Kein g++ gefunden (winget install BrechtSanders.WinLibs.POSIX.UCRT).' }

$ofm = Join-Path $root 'lib/OFM-KwlControl/src'
$knx = Join-Path $root 'lib/knx/src/knx'

# Firmware-Quellen des Lueftungsmoduls - UNVERAENDERT. Gp8413Drive.cpp fehlt
# bewusst: an seiner Stelle steht sim/core/sim_board.cpp.
$ofmSources = @('CableComp', 'KwlCurve', 'KwlFan', 'KwlFanModule', 'KwlGroup', 'KwlOutput',
    'KwlRoom', 'KwlRoomModule', 'MoistAir', 'SendCondition', 'StageArbiter',
    'StageLadder', 'StageMap', 'TempControl') | ForEach-Object { Join-Path $ofm "$_.cpp" }
$knxSources = @('dpt', 'dptconvert', 'knx_value', 'bits') | ForEach-Object { Join-Path $knx "$_.cpp" }
$simSources = Get-ChildItem (Join-Path $sim 'core') -Filter *.cpp | ForEach-Object FullName

$gppArgs = @('-std=gnu++17', '-O2', '-static', '-Wall', '-Wno-unused-function',
    '-DDEVICE_KNXFANDRV_REV01', '-DKWL_SIMULATOR', '-D_POSIX_THREAD_SAFE_FUNCTIONS',
    # gmtime_r fuer knx_value.cpp unter MinGW
    "-I$(Join-Path $sim 'core')", "-I$(Join-Path $root 'include')", "-I$ofm", "-I$knx",
    '-o', (Join-Path $out $Exe)) + $simSources + $ofmSources + $knxSources

Write-Host "g++: $gpp"
& $gpp @gppArgs
if ($LASTEXITCODE -ne 0) { throw "Uebersetzen fehlgeschlagen ($LASTEXITCODE)" }
Write-Host "Kern gebaut: $(Join-Path $out $Exe)"

# --- Metadaten aus der ETS-Applikation ----------------------------------------
$py = Join-Path $env:USERPROFILE '.platformio\penv\Scripts\python.exe'
if (-not (Test-Path $py)) { $py = 'python' }
& $py (Join-Path $sim 'gen_meta.py') (Join-Path $root 'src/Kwl.debug.xml') (Join-Path $out 'meta.json')
if ($LASTEXITCODE -ne 0) { throw "Metadaten fehlgeschlagen ($LASTEXITCODE)" }
