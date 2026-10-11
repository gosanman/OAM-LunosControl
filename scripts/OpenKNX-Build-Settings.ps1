# Produktnamen fuer den Release-Build, nach der Vorlage
# lib/OGM-Common/scripts/setup/templates/OpenKNX-Build-Settings.ps1.
#
#   sourceName  "Kwl"         - Name der Applikation im Projekt (src/Kwl.xml)
#   targetName  "KwlControl"  - Name im Release-Paket (KwlControl-Release-x.y.z.zip)
#
# Abweichung von der Vorlage: Die Vorlage sucht fuer "Release" eine eigene
# src/Kwl-Release.xml. Dieses Projekt hat nur EINE Applikation, src/Kwl.xml;
# Beta und Release unterscheiden sich nur in der Firmware-Umgebung
# (develop_ vs. release_KnxFanDrv). Deshalb zeigt releaseName immer auf Kwl.
$settings = lib/OGM-Common/scripts/build/OpenKNX-Build-Settings.ps1 $args[0] "Kwl" "KwlControl"
$settings.releaseName = "Kwl"
Return $settings
