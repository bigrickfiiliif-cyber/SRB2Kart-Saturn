# Blockwerk

3D-Spiele bauen wie in einer Engine (Szene, Objekte, Kamera) und programmieren mit Blöcken wie in Scratch. Fertige Spiele laufen im Browser oder als **PlayStation-2-Programm** (`game.elf`).

## Starten

`Blockwerk.html` im Browser öffnen. Es wird nichts installiert; die Bibliotheken (three.js, Blockly, JSZip) kommen von einem CDN.

## So funktioniert es

| Bereich | Was du dort machst |
|---|---|
| **Szene** (links) | Würfel, Kugel, Zylinder oder Fläche hinzufügen, Objekt auswählen |
| **3D-Ansicht** | Objekte verschieben, drehen und skalieren (Tasten `W` `E` `R`), Kamera mit der Maus |
| **Eigenschaften** (rechts) | Name, Position, Drehung, Größe und Farbe; unter „Welt & Kamera“ Himmel und Startkamera |
| **Skript** (unten) | Blöcke für das ausgewählte Objekt zusammenstecken |
| **▶ Spielen** | Spiel im Editor testen (60 Hz, 4:3 wie auf der PS2) |
| **Export Web** | Eine einzelne HTML-Datei, die überall läuft |
| **Export PS2** | Ein .zip mit C-Projekt für PS2SDK + gsKit |

### Blöcke

- **Ereignisse:** Wenn Spiel startet · Jeden Frame · Wenn Taste gedrückt · Wenn ich … berühre
- **Bewegung:** fahre vorwärts · bewege um · drehe um · setze Position/Drehung · gehe zurück (für Wände)
- **Aussehen:** Farbe · zeigen/verstecken · Größe · Anzeige (Text + Zahl auf dem Bildschirm)
- **Kamera & Spiel:** Kamera folgt mir · Kamera schaut auf mich · setze Kamera · Spiel neu starten
- **Fühlen:** Taste gehalten? · berühre ich …? · Position/Drehung/Größe · Entfernung · Zeit
- **Steuerung, Logik, Mathe, Variablen** (Variablen gelten für alle Objekte)

Tasten sind PS2-Controller-Tasten. Am PC: Pfeile/WASD = Steuerkreuz, Leertaste = ✕, K = ○, U = □, I = △, Enter = START, Q/E = L1/R1. Gamepads werden erkannt.

## PS2-Export

Das .zip enthält `src/game.c` (deine Szene und Blöcke als C), die Laufzeit (`rt.c`, `render.c`, `main_ps2.c`), `build.sh`, `Makefile`, einen GitHub-Workflow und eine Anleitung. Bauen:

```sh
docker run --rm -v "$PWD":/src -w /src ps2dev/ps2dev sh build.sh
```

Dann `game.elf` in PCSX2 starten (*System → Run ELF*, eigenes BIOS nötig) oder auf einer echten PS2 per USB-Stick mit wLaunchELF.

Die PS2-Version rechnet die 3D-Grafik selbst (Transformation, Clipping, flache Beleuchtung) und zeichnet mit gsKit. Die Spiel-Logik ist dieselbe wie im Browser. `src/main_host.c` lässt das Spiel ohne PS2 am PC laufen und speichert ein Bild aus dem PS2-Renderer.

`examples/muenzjaeger-ps2/` ist ein fertiger Export des Beispielspiels.

## Entwicklung

- `editor.html` ist der Quelltext des Editors, `ps2-runtime/` die C-Laufzeit.
- `node build.mjs` fügt beides zu `Blockwerk.html` zusammen.

## Grenzen

- Höchstens 128 Objekte. Kollisionen nutzen achsenparallele Boxen (die Drehung zählt dabei nicht).
- Formen: Würfel, Kugel, Zylinder, Fläche. Keine Texturen, Modelle oder Sound.
- Auf PAL-Konsolen (50 Hz) läuft die Logik etwas langsamer als auf NTSC.
