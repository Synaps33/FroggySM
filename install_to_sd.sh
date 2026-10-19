#!/usr/bin/env bash
set -e

# APP musi byc identyczne jak BIN: menu wyprowadza nazwe rdzenia z nazwy
# folderu ROM i szuka cores/<APP>.sf2k. Roznica tylko w wielkosci liter
# wystarczy, zeby szukanie pliku koncze sie bledem.
APP="froggysm"        # nazwa katalogu ROM
BIN="froggysm"        # nazwa pliku rdzenia (bez rozszerzenia)
OLD_APP="PSPMania"    # poprzednia nazwa, do posprzatania
OLD_APP2="FroggySM"   # wariant z wielkimi literami, tez do posprzatania
LOW_APP="pspmania"    # vfat rozroznie wielkosc liter

SD_ROOT=""

# Wykrywanie katalogu karty SD konsoli (GB300 / SF2000)
for p in /media/Sajnaps/GB300 /media/Sajnaps/* /mnt/* /media/*; do
    if [ -d "$p/system/Deimos/cores" ] || [ -d "$p/cores" ]; then
        SD_ROOT="$p"
        break
    fi
done

if [ -z "$SD_ROOT" ]; then
    echo "Karta SD nie zostala wykryta. Podlacz karte do komputera i sprobuj ponownie."
    exit 1
fi

echo "Wykryto karte SD pod: $SD_ROOT"

SONGS_SRC="/home/Sajnaps/pspm-songs"

# 1. Rdzen Deimos / multicore.  Jedna binarka obsluguje GB300 i SF2000,
#    wiec instalujemy ja pod kazda nazwe, ktorej szuka firmware.
CORE="/home/Sajnaps/stepmania-sf2000/dist/$BIN.sf2k"

if [ ! -f "$CORE" ]; then
    echo "Brak $CORE -- uruchom najpierw: make device"
    exit 1
fi

# Jedyna lokalizacja, ktorej firmware szuka: CORES_DIRECTORY/<id>.sf2k
# (include/file.h + src/main.c). Katalog cores/<id>/ z core_87000000
# uzywany przez oryginalny loader, ale nie przez ten front-end.
mkdir -p "$SD_ROOT/system/Deimos/cores"
cp -vf "$CORE" "$SD_ROOT/system/Deimos/cores/$BIN.sf2k"

# 2. Posprzataj rdzen i katalog ROM ze starej nazwy.
#    UWAGA: vfat jest wrazliwy na wielkosc liter, wiec sprzataj oba warianty.
if [ -d "$SD_ROOT/cores/$OLD_APP" ]; then
    echo "Migracja $OLD_APP -> $APP ..."
    rm -rf "$SD_ROOT/cores/$OLD_APP"
fi
rm -fv "$SD_ROOT/system/Deimos/cores/$OLD_APP.sf2k" \
       "$SD_ROOT/system/Deimos/cores/$OLD_APP.gb3k" \
       "$SD_ROOT/system/Deimos/cores/$LOW_APP.sf2k" \
       "$SD_ROOT/system/Deimos/cores/$LOW_APP.gb3k" 2>/dev/null || true

# 3. Katalog ROM i stuby uruchomieniowe (FrogUI + Deimos + menu GBA)
ROMS_DIR="$SD_ROOT/ROMS/$APP"

# Przenies stara zawartosc, jesli juz istnieje, zamiast kopiowac od nowa.
# Na vfat zmiana samej wielkosci liter wymaga dwoch krokow.
if [ -d "$SD_ROOT/ROMS/$OLD_APP" ] && [ ! -d "$ROMS_DIR/Songs" ]; then
    echo "Przenoszenie $SD_ROOT/ROMS/$OLD_APP -> $ROMS_DIR"
    mv "$SD_ROOT/ROMS/$OLD_APP" "$ROMS_DIR"
fi
if [ -d "$SD_ROOT/ROMS/$OLD_APP2" ]; then
    echo "Przenoszenie $SD_ROOT/ROMS/$OLD_APP2 -> $ROMS_DIR"
    mv "$SD_ROOT/ROMS/$OLD_APP2" "$SD_ROOT/ROMS/.froggysm_tmp.$$"
    mv "$SD_ROOT/ROMS/.froggysm_tmp.$$" "$ROMS_DIR"
fi

mkdir -p "$ROMS_DIR/.res"
mkdir -p "$SD_ROOT/ROMS/gba"

# Wpis meniu.
#
# Format wymagany przez firmware (src/main.c:26-48 parse_filename):
#     <id_rdzenia>;<katalog_ROM>;<plik_ROM>.gba
# Trzecie pole MUSI miec rozszerzenie -- strrchr(d, '.') zwraca NULL i caly
# wpis jest odrzucany. Bez tego firmware traktuje plik jako stockowy GBA,
# pokazuje "STOCK" i rdzen sie nie zaladuje.
# Rozszerzenie jest obcinane, wiec rdzen laduje sie z cores/<id>.sf2k
# a ROM z ROMS/<katalog>/<plik>.
STUB="$BIN;$APP;$BIN.gba"

printf '%s' "$STUB" > "$SD_ROOT/ROMS/$BIN;$APP.gba"
printf '%s' "$STUB" > "$ROMS_DIR/$BIN"

# Posprzataj stare wpisy i stuby
rm -fv "$SD_ROOT/ROMS/$BIN.gba" \
       "$SD_ROOT/ROMS/$OLD_APP.gba" \
       "$SD_ROOT/ROMS/$LOW_APP.gba" \
       "$SD_ROOT/ROMS/pspmania;$OLD_APP.gba" \
       "$SD_ROOT/ROMS/gba/$BIN.gba" \
       "$SD_ROOT/ROMS/gba/$LOW_APP.gba" \
       "$ROMS_DIR/$BIN.gba" 2>/dev/null || true
rm -rf "$SD_ROOT/ROMS/$OLD_APP" "$SD_ROOT/ROMS/$LOW_APP" "$SD_ROOT/ROMS/$OLD_APP2" 2>/dev/null || true

# 4. Kopiuj okladke
if [ -f "/tmp/opencode/FroggySM.rgb565" ]; then
    cp -vf "/tmp/opencode/FroggySM.rgb565" "$ROMS_DIR/FroggySM.rgb565"
    cp -vf "/tmp/opencode/FroggySM.rgb565" "$ROMS_DIR/$BIN.rgb565"
    cp -vf "/tmp/opencode/FroggySM.rgb565" "$ROMS_DIR/.res/FroggySM.rgb565"
    cp -vf "/tmp/opencode/FroggySM.rgb565" "$ROMS_DIR/.res/$BIN.rgb565"
fi

# 5. Stare stuby i okladki przeniesione wraz z katalogiem ROM
rm -fv "$ROMS_DIR/$OLD_APP" \
       "$ROMS_DIR/$OLD_APP.gba" \
       "$ROMS_DIR/$OLD_APP.rgb565" \
       "$ROMS_DIR/pspmania" \
       "$ROMS_DIR/pspmania.gba" \
       "$ROMS_DIR/pspmania.rgb565" \
       "$ROMS_DIR/.res/$OLD_APP.rgb565" \
       "$ROMS_DIR/.res/pspmania.rgb565" 2>/dev/null || true

# 6. Piosenki
SONGS_DIR="$ROMS_DIR/Songs"
if [ -d "$SONGS_SRC" ]; then
    echo "Kopiowanie piosenek..."
    mkdir -p "$SONGS_DIR"
    cp -r "$SONGS_SRC"/* "$SONGS_DIR/"
fi

# 7. Motywy StepManii
THEMES_SRC="/home/Sajnaps/stepmania-sf2000/Themes"
THEMES_DIR="$ROMS_DIR/Themes"
if [ -d "$THEMES_SRC" ]; then
    echo "Kopiowanie motywow..."
    mkdir -p "$THEMES_DIR"
    cp -r "$THEMES_SRC"/* "$THEMES_DIR/"
fi

sync
echo ""
echo "=== Instalacja zakonczona pomyslnie ==="