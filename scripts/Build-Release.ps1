# Release-Paket KwlControl, nach der Vorlage
# lib/OGM-Common/scripts/setup/templates/Build-Release.ps1.
#
#   scripts/Build-Release.ps1 Release   - Release-Paket
#   scripts/Build-Release.ps1           - Beta-Paket
#
# Ergebnis: release/KwlControl-<Release|Beta>-<Version>.zip mit knxprod-Daten,
# Firmware (.uf2), Upload-Skripten und Readme. Beide Pakete enthalten die
# Release-Firmware (Watchdog an, Debug aus); sie unterscheiden sich nur im Namen.
# Der Ordner release/ wird vorher geleert (steht in .gitignore).
#
# Abweichung von der Vorlage: Build-Release-Postprocess.ps1 loescht
# dependencies.txt und will sie aus den lib/-Repositories neu erzeugen. Die
# stehen hier auf festen Commits (detached HEAD) - dann bricht das Skript ab, und
# die Datei fehlt. Ausserdem schreibt es restore/ neu. Beides ist in diesem
# Projekt von Hand gepflegt und eingecheckt. Deshalb wird es vor dem Nachlauf
# gesichert und danach unveraendert zurueckgelegt: ein Release-Build aendert
# nichts im Repository.

$keep = @("dependencies.txt", "restore/Restore-Dependencies.ps1", "restore/Restore-Dependencies-Branch.ps1")
$backup = Join-Path ([System.IO.Path]::GetTempPath()) "kwl-release-keep"
function Save-Kept {
  if (Test-Path $backup) { Remove-Item -Recurse -Force $backup }
  foreach ($f in $keep) {
    if (Test-Path $f) {
      $target = Join-Path $backup $f
      New-Item -ItemType Directory -Force -Path (Split-Path $target) | Out-Null
      Copy-Item $f $target
    }
  }
}
function Restore-Kept {
  foreach ($f in $keep) {
    $source = Join-Path $backup $f
    if (Test-Path $source) { Copy-Item -Force $source $f }
  }
  # Erzeugt der Nachlauf zusaetzliche Restore-Skripte, gehoeren sie nicht dazu.
  if (Test-Path "restore/Restore-Dependencies-LinksOnly.ps1") {
    Remove-Item "restore/Restore-Dependencies-LinksOnly.ps1"
  }
}

# knxprod und Vorbereitung
lib/OGM-Common/scripts/setup/reusable/Build-Release-Preprocess.ps1 $args[0]
if (!$?) { exit 1 }

# Firmware KNXFANDRV Rev 0.1: RP2040 mit KNX-TP, ohne OTA
lib/OGM-Common/scripts/setup/reusable/Build-Step.ps1 release_KnxFanDrv firmware-KNXFANDRV-Rev01 rp2040-tp KNXFANDRV-Rev01
if (!$?) { exit 1 }

# Paket schnueren
Save-Kept
lib/OGM-Common/scripts/setup/reusable/Build-Release-Postprocess.ps1 $args[0]
$ok = $?
Restore-Kept
if (!$ok) { exit 1 }
