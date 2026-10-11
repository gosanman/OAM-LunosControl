# Produktnamen fuer den Release-Build, nach der Vorlage
# lib/OGM-Common/scripts/setup/templates/OpenKNX-Build-Settings.ps1.
#
#   sourceName  "Kwl"         - Name der Applikation im Projekt
#   targetName  "KwlControl"  - Name im Release-Paket (KwlControl-Release-x.y.z.zip)
#
# Welche Produktdefinition gebaut wird, folgt der OpenKNX-Regel:
#   Release  -> src/Kwl-Release.xml  (ohne BuildSuffix, Name ohne "(Dev)")
#   Beta     -> src/Kwl.xml          (BuildSuffix "-dev", Name mit "(Dev)")
# Beide haben dieselbe ApplicationNumber und -Version, also dieselbe ETS-ID.
$settings = lib/OGM-Common/scripts/build/OpenKNX-Build-Settings.ps1 $args[0] "Kwl" "KwlControl"
Return $settings
