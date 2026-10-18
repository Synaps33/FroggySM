# HACKERLOG - PSPMania GB300 & SF2000 Port

```
  ____  ____  ____  __  __    _    _   _  _____ 
 |  _ \/ ___||  _ \|  \/  |  / \  | \ | |/ ___|
 | |_) \___ \| |_) | |\/| | / _ \ |  \| | |  _ 
 |  __/ ___) |  __/| |  | |/ ___ \| |\  | |_| |
 |_|   |____/|_|   |_|  |_/_/   \_\_| \_|\____|
        GB300 / SF2000 NATIVE MIPS32 PORT
```

## 1. Cel projektu

Port **PSPMania** (StepMania 3.9 dla PSP, autor: **mafu9**) na konsole retro **Data Frog SF2000** oraz **GB300** (układ SoC Actions Semi ATS2825 / ATS2831, MIPS32r2 LE, 200MHz, 32MB RAM).

Oryginalny PSPMania to port StepManii 3.9 — silnika rytmicznego z setkami tysięcy linii kodu C++ (Rage framework), zależny od PSP SDK, DirectX/OpenGL, Lua, XML, wątków i sprzętowego dekodera MP3 (MediaEngine). Dosłowny port jest niemożliwy — zamiast tego napisano **nowy silnik w C**, wierny oryginałowi w logice, ale zoptymalizowany pod 320x240 RGB565 i 200MHz MIPS32 soft-float.

## 2. Kluczowe cechy i optymalizacje

### Grafika (320x240 RGB565)
- **Sprite'y z oryginalnego note skinu**: `NoteSkins/dance/default` z wersji demo PSPMania (36px strzałki). Lewo/Góra/Prawo są rotacjami tekstury Down — dokładnie tak, jak deklarują pliki `.sprite`/`.actor` w oryginale.
- **Chromakey 0x0000**: sprite'y mają przezroczystość przez klucz koloru, więc blitting to jeden porównanie i zapis, bez alpha blendingu.
- **Jedna siatka nut rezydentna**: PSPMania ładowała wszystkie trudności wszystkich piosenek naraz (kilka MB siatek nut). Tutaj siatka jest dekodowana leniwie tylko dla wybranej trudności — `sm_song_load_steps()`.

### Dźwięk (MP3 softwareowy + zegar próbek)
- **Dekoder MP3**: minimp3 (CC0, public domain) — jedyny w pełni otwarty dekoder MP3, nie wymaga libm, tylko stdint/stdlib/string. PSPMania używała sprzętowego MediaEngine, więc nie było dekodera softwareowego do odziedziczenia.
- **Zegar piosenki = liczba zdekodowanych próbek**: dokładnie jak w oryginale (`ScreenGameplay.cpp:1173-1182` — pozycja piosenki z licznika próbek silnika audio). Żadne dryfowanie rytmu niezależnie od czasu klatki.
- **Resampling**: piosenki 44.1kHz/48kHz/22.05kHz → 44.1kHz przez liniowy resampler.

### Parser chartów (.sm)
- **Wierny port NotesLoaderSM.cpp**: tokenizer MSD (`#TAG:param;`), dwuprzegiegowy (timing przed metadata), `#NOTES` → siatka nut.
- **Wierny port NoteDataUtil.cpp**: miary oddzielone przecinkami, wiersze oddzielone nowyminiami, K wierszy = K równomiernie rozłożonych beatów (K=3 → triplety, K=8 → ósemki).
- **Wierny port NoteData.cpp**: konwersja `2...3` → hold notes (pierwsza nie-pusta nota w kolumnie kończy hold).
- **TimingData**: `#BPMS`, `#STOPS`, `#OFFSET` → beat↔seconds z dokładną semantyką StepManii (stop dodawany wholesale, BPM segmenty clampowane).

### Oceny i wynik (ScoreKeeperMAX2)
- **Okna ocen** (PrefsManager.cpp:80-87): Marvelous ±22.5ms, Perfect ±45ms, Great ±90ms, Good ±135ms, Boo ±180ms, OK ±250ms, Mine ±90ms.
- **Wagi ocen** (PrefsManager.cpp:125-133): Marvelous=2, Perfect=2, Great=1, Good=0, Boo=-4, Miss=-8, HitMine=-8, OK=6, NG=0.
- **Wynik MAX2**: `meter * 10,000,000` max, wagi 10/9/5 dla Marvelous/Perfect/Great, `GetScore(p,B,S,n) = p*n*B/S`.
- **Combo**: rośnie przy Great+, reset przy Boo/Miss. Hold OK dodaje pełny Marvelous.
- **Oceny**: AAAA/AAA/AA/A/B/C/D z progami 1.00/1.00/0.93/0.80/0.65/0.45/-99999.

### HackerLog Telemetry
- **CRT boot log**: diagnostyka podsystemów przy starcie.
- **Nakładka telemetryczna** (SELECT): transport/audio clock, decode load, chart timing, judgment tallies, holds, pro-timing, score/combo/accuracy, frame count, SFX, keys, song name.

## 3. Mapowanie sterowania

| Akcja | SF2000 / GB300 | Klawiatura (Runner) |
|---|---|---|
| Nuta LEWA (Left) | D-Pad Lewo / Y (Switch) | Strzałka w lewo |
| Nuta DÓŁ (Down) | D-Pad Dół / B (Switch) | Strzałka w dół |
| Nuta GÓRA (Up) | D-Pad Góra / X (Switch) | Strzałka w górę |
| Nuta PRAWA (Right) | D-Pad Prawo / A (Switch) | Strzałka w prawo |
| HackerLog Overlay | SELECT | Tab |
| Start / Pauza / Potwierdzenie | START / Przycisk A | Enter |
| Wstecz / Anuluj | Przycisk B | Escape |
| Wybór piosenki | D-Pad Góra/Dół | Strzałki |
| Wybór trudności | D-Pad Lewo/Prawo / L/R | Strzałki |

## 4. Struktura plików

```
stepmania-sf2000/
├── Makefile                    # build host (runner) + device (MIPS32)
├── HACKERLOG.md                # ten plik
├── install_to_sd.sh            # skrypt instalacyjny na SD
├── include/
│   ├── sm_types.h              # stałe StepManii (ROWS_PER_BEAT=24, TNS_*, wagi)
│   ├── sm_song.h               # API parsera .sm
│   ├── sm_library.h            # skaner katalogów SD
│   ├── sm_game.h               # silnik gry
│   ├── sm_render.h             # renderer RGB565
│   ├── sm_audio.h              # dekoder MP3 + miksowanie
│   ├── sm_hackerlog.h          # telemetria
│   ├── sm_assets.h             # sprite'y (generowane)
│   ├── sm_dirent.h             # shim dirent (host vs device)
│   ├── libretro.h              # API libretro
│   └── font8x8.h               # font 8x8
├── src/
│   ├── libretro.c              # front-end libretro
│   ├── sm_timing.c             # TimingData (beat↔seconds)
│   ├── sm_song.c               # parser .sm (NotesLoaderSM + NoteDataUtil)
│   ├── sm_library.c            # skaner Songs/<group>/<song>/
│   ├── sm_game.c               # silnik gry (Player.cpp + ScoreKeeperMAX2)
│   ├── sm_render.c             # renderer 320x240
│   ├── sm_audio.c              # minimp3 + WAV + miksowanie
│   ├── sm_hackerlog.c          # telemetria
│   ├── sm_assets.c             # sprite'y (generowane z note skinu)
│   └── mp3_mini.h              # minimp3 (CC0)
├── runner/
│   └── test_runner.c           # headless test runner
├── tools/
│   └── make_assets.py          # konwerter PNG → RGB565
├── assets/                     # (opcjonalnie) surowe assety
└── dist/
    ├── pspmania.sf2k           # binarka SF2000
    └── pspmania.gb3k           # binarka GB300
```

## 5. Testy i weryfikacja

Testy uruchamiane na runnerze (`make test`):

1. **Inicjalizacja core** — RGB565 pixel format.
2. **Skanowanie biblioteki** — 7 piosenek, 31 trudności, 20976 nut.
3. **Dekodowanie chartów** — każda trudność ma nuty.
4. **Boot screen** — CRT HackerLog.
5. **Wybór piosenki** — przejście z boot do select.
6. **Start piosenki** — START wchodzi w gameplay, siatka nut rezydentna.
7. **Strumień audio** — dekodowane MP3 ma amplitudę, zegar piosenki >1s, beat >0.
8. **Klatki gry** — licznik klatek rośnie.
9. **HackerLog overlay** — SELECT przełącza.
10. **Auto-play** — symulacja wciśnięć z chartu: MARV/PERF/GREAT/GOOD/BOO/MISS, combo, score, accuracy.
11. **Ekran wyników** — renderowanie.
12. **Integralność audio/video** — klatki i próbki dostarczone, bez clippingu.
13. **Teardown** — zwolnienie zasobów.

Wyniki auto-play (Shaka Beach, Medium):
- 95 ocenionych rzędów, 95 nut
- MARV=80, MISS=16
- score=13,015,179 / 70,000,000
- combo=86, max=86
- accuracy=70.80%

## 6. Wymagania sprzętowe

- **SD card**: katalog `/ROMS/PSPMania/Songs/` z piosenkami z wersji demo PSPMania (7 piosenek, 31 trudności).
- **Kontroler**: D-Pad + A/B/X/Y + SELECT + START.
- **Wyświetlacz**: 320x240 RGB565.

## 7. Instalacja

```bash
# 1. Skopiuj piosenki na SD card
mkdir -p /mnt/sda1/ROMS/PSPMania/Songs
cp -r /path/to/PSPMania/Songs/* /mnt/sda1/ROMS/PSPMania/Songs/

# 2. Skopiuj binarkę na SD card
cp dist/pspmania.sf2k /mnt/sda1/system/Deimos/cores/.sf2k   # SF2000
cp dist/pspmania.gb3k /mnt/sda1/system/Deimos/cores/.sf2k   # GB300

# 3. Uruchom z menu
```

Albo użyj `install_to_sd.sh` (wymaga zamontowanego SD).

## 8. Znane ograniczenia

- **Brak libm na urządzeniu**: wszystkie funkcje transcendentalne (sin/cos) zaimplementowane własnoręcznie w `sm_timing.c`.
- **Dekoder MP3 softwareowy**: minimp3 na 200MHz MIPS32 soft-float może mieć ograniczoną wydajność przy długich piosenkach (Fury of the Storm, 6:47). Telemetria HackerLog pokazuje `DECODE LOAD %`.
- **Brak profili/wyników online**: jak w PSPMania, profile nie są szyfrowane.
- **Brak edycji chartów**: jak w PSPMania (za mało przycisków).
- **Brak trybu Battle/3D**: jak w PSPMania.

## 9. Licencje

- **PSPMania**: mafu9 (StepMania 3.9 port) — licencja mieszana (StepMania GPL + PSPMania closed source).
- **minimp3**: CC0 (public domain) — https://github.com/lieff/minimp3
- **Ten port**: kod źródłowy w C napisany od zera, wierny logice oryginału.

## 10. Podziękowania

- **mafu9** za PSPMania i StepMania 3.9.
- **StepMania team** za silnik rytmiczny.
- **lieff** za minimp3.
- **FunkinCrew** za Friday Night Funkin' (inspiracja dla portów GB300/SF2000).
