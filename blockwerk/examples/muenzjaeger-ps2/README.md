# Münzjäger – PlayStation 2

Mit **Blockwerk** erstellt. Dieses Verzeichnis ist ein vollständiges PS2SDK-Projekt (C, gsKit, libpad).

## Bauen

Am einfachsten mit dem offiziellen ps2dev-Docker-Image (Linux, macOS, Windows mit Docker Desktop):

```sh
docker run --rm -v "$PWD":/src -w /src ps2dev/ps2dev sh build.sh
```

Mit lokal installiertem PS2SDK (`$PS2SDK` und `$GSKIT` gesetzt) geht auch `make` oder `sh build.sh`.
Ergebnis: `game.elf`.

Auf GitHub baut `.github/workflows/ps2.yml` die ELF automatisch bei jedem Push (Download unter „Actions“ → Artifacts).

## Starten

- **Emulator:** PCSX2 → *System → Run ELF* → `game.elf` (benötigt ein eigenes PS2-BIOS).
- **Echte PS2:** `game.elf` auf einen USB-Stick kopieren und mit wLaunchELF/uLaunchELF starten (z. B. über FreeMcBoot oder OpenTuna).

## Steuerung

Controller 1. Die Tasten entsprechen den Blöcken: ✕ ○ □ △, Steuerkreuz, START, SELECT, L1, R1.

## Auf dem PC testen (ohne PS2)

```sh
cc -O2 -Isrc -o game_test src/main_host.c src/rt.c src/render.c src/game.c -lm
./game_test 600 bild.ppm
```
Das simuliert 600 Frames mit vorgegebenen Tasten, gibt die Positionen aller Objekte aus und speichert mit dem PS2-Renderer ein Bild (`bild.ppm`).

## Dateien

| Datei | Inhalt |
|---|---|
| `src/game.c` | Szene und deine Blöcke als C-Code (generiert) |
| `src/rt.c` | Spiel-Logik: Ereignisse, Bewegung, Kollision, Kamera |
| `src/render.c` | 3D: Transformation, Clipping, Licht (Software, flach schattiert) |
| `src/main_ps2.c` | PS2: gsKit-Grafik, Controller, Schrift |
| `project.blockwerk.json` | Dein Projekt – in Blockwerk wieder öffnen |

## Grenzen

- Höchstens 128 Objekte. Kollision nutzt achsenparallele Boxen (Drehung wird dabei ignoriert).
- Läuft mit 60 Hz (NTSC). Auf PAL-Konsolen läuft die Logik bei 50 Hz etwas langsamer.
- Anzeige-Texte nutzen die PS2-ROM-Schrift; Umlaute können je nach Konsole fehlen.
