# VectorPie Vectrex Emulator – Status

Stand: 2026-11-08 · Version 0.1.0 (nichts committet, nichts auf dem Automaten installiert)

## Ziel
Ein zyklengenauer Vectrex-Emulator als VectorPie-Spielpaket (`.vpk`) für den Vektor-Automaten: Pi 4/5, USB-DVG oder BeamForge, RGB-Vektormonitor. Er spielt auch UVMC2-Spiele (`.um2`) und PiTrex-Programme.

## Aktueller Stand
- **Kern:** [hugonl31/Vectrex_EMU](https://github.com/hugonl31/Vectrex_EMU) v1.6.1, vendored und **unverändert** in `third_party/vectrex_emu/` (siehe `VENDOR.md`). Als Bibliothek eingebunden über `src/core/vx_core.c/.h`, das upstreams Harness `vtest.c` einbindet. Ein Prozess, kein Pipe.
- **Programm** `vectrex` auf dem VectorPie SDK, Aufbau nach Major Havoc PE Enhanced:
  - `src/platform/app.c`: Hauptschleife aus Major Havoc übernommen (`hold_cancel` für das Menü).
  - `audio.c`: SDK-Stream mit 44,1 kHz.
  - `config.c`: ini-Kette, siehe unten.
  - `controls.c`: 2 Controller mit je Stick und 4 Buttons über `vp_controls`.
  - `src/render/draw.c`: Bildabbildung.
- **Takt:** Die Emulation läuft nach Vectrex-Zyklen (1,5 MHz) über ganze Frames. Das Board bekommt jeweils das letzte fertige Bild mit `board_hz` (Default 50).
- **Einstellungen** (`package/vectrex.ini`, dann `save/vectrex.ini`, dann `save/games/<id>.ini`):
  - Monitor horizontal/vertikal, Größe, Breite/Höhe, Position, Spiegeln, Helligkeit/Gamma, Linienfarbe, `board_hz`
  - BIOS-Bypass
  - UVMC2 Fast/Exact
  - Digital→Analog-Rampe (`dig_attack`, `dig_release`, `dig_curve`, `dig_max`), Deadzone
  - Lautstärke
- **Steuerung pro Spiel:** Erst `save/controls.ini` (alle Spiele), dann `save/controls/<id>.ini`. Was die Spieldatei angibt, überschreibt die allgemeine Belegung.
- **Optionsmenü** (`src/platform/menu.c`, Aufbau wie `platform/menu.c` bei Major Havoc): Tab/F2 (bzw. `ui_configure`/`service`) öffnet es, das Spiel steht still, Esc gehört solange dem Menü.
  - Seiten:
    - STEUERUNG SPIELER 1/2: Richtungen und 4 Buttons, belegt mit `vp_capture`
    - STICK: Anlauf, Rücklauf, Kurve, Maximum, Deadzone, Live-Test mit Kreuz in einem Kasten
    - BILD: Monitor quer/hochkant, Größe, Breite, Höhe, Position, Spiegeln, Helligkeit, Gamma, Farbe (8 Vorgaben), Board-Rate. Das Spielbild bleibt dahinter sichtbar.
    - MASCHINE: BIOS-Intro, UVMC2 Fast/Exact (nur bei UVMC2-Spielen)
    - TON: Lautstärke
  - **SPEICHERN FÜR:**
    - DIESES SPIEL: schreibt `save/games/<id>.ini` (nur Abweichungen) und `save/controls/<id>.ini`.
    - ALLE SPIELE: Was in dieser Menüsitzung geändert wurde, geht nach `save/vectrex.ini`. Eine eigene Einstellung des Spiels für andere Werte bleibt erhalten. Die Steuerung geht nach `save/controls.ini`, die Steuerungsdatei des Spiels wird gelöscht.
  - **DIESES SPIEL WIE ALLE SPIELE** (zweimal bestätigen) löscht die eigenen Einstellungen und Steuerung des Spiels.
  - **SPIEL NEU STARTEN** schaltet die Konsole neu ein, damit wird die BIOS-Intro-Einstellung wirksam. Der UVMC2-Modus gilt erst beim nächsten Start des Spiels, das Menü weist darauf hin.
  - **SPEICHERN UND ZURÜCK, SPEICHERN UND SPIEL VERLASSEN**: Verlassen führt zurück zur Spieleliste.
- **Steuerfunktionen** heißen `pad1_up` … `pad2_b4`, Standard sind die advmame-Aktionen `p1_up` … `p2_button4`. Die Namen sind bewusst verschieden von den Aktionsnamen, sonst würde eine Funktion ihre eigene Aktion verdecken.
- **Spielauswahl** (`src/platform/browser.c`, `roms.c`): Ohne Argument (so startet das VectorPie-Menü ein Paket) erscheint die Spieleliste. Mine Storm steht zuerst, dann alle Module aus `rom_dir`, `/persistent/games/rom/vectrex`, `/usr/local/share/advance/image/vectrex` und `roms/`.
  - Dateitypen: `.bin .vec .gam .rom .um2 .pit`.
  - Namen: zuerst `names.ini` (Paket, dann `save/names.ini`), sonst der Header-Titel, wenn er kurz und keine Botschaft ist, sonst der Dateiname. Doppelte Namen bekommen den Dateinamen in Klammern.
  - Das gewählte Spiel läuft als **eigener Prozess** (`vectrex -cart PFAD`). Esc im Spiel führt zurück zur Liste, Esc in der Liste zurück zum VectorPie-Menü. Das zuletzt gespielte Spiel wird gemerkt (`save/browser.ini`).
- **Tür zum VectorPie-Menü:** Das Menü startet Vectrex heute mit der Vorlage `vectrex /usr/local/bin/advmess vectrex -cart "$(clone_rom)"` in `/usr/local/share/advance/vector_pie_menu.cfg`, Abschnitt `[launch]`. In `gamelist.ini` stehen noch keine Vectrex-Zeilen. Unser Programm versteht genau diesen Aufruf (`vectrex -cart NAME`; der Name wird ohne Rücksicht auf Endung und Groß-/Kleinschreibung in den Modul-Ordnern gesucht). Später reichen zwei Schritte:
  1. Die Vorlage auf `/persistent/games/vectrex/vectrex -cart "$(clone_rom)"` umstellen.
  2. Zeilen der Form `Vectrex|Armor Attack|ARMOR|ARMOR` in `gamelist.ini` eintragen.

  Am Automaten habe ich dafür noch nichts geändert.
- **Paket:** `package/game.ini` und `vectrex.ini`, BIOS in `package/system/bios.bin` (kommt mit, so entschieden).

## Verifiziert (2026-11-08)
- **Build:** PC (MSYS2 gcc) und Pi (clang aarch64) bauen ohne Warnungen in unserem Code.
- **CPU-Timing-Testmodul** über die Bibliothek: Alle 32 Werte stimmen mit dem echten Vectrex überein, auf **Windows x64 und auf dem Pi 5 (ARM64)**. Die Zykelzahl (21.038.423 für 700 Frames) ist identisch mit dem Original-Harness.
- **BIOS-Bypass:** Mine Storm, das Timing-Testmodul und UVMC2 starten sofort. Das Timing-Testmodul liefert dabei dieselben 32 Werte.
- **Fenster (PC):** Mine Storm, UVMC2 Demo und Last Human (UVMC2) laufen mit 60 fps in Echtzeit. Monitor horizontal (Ränder) und vertikal (per Spieldatei `save/games/uvmc2_demo.ini`) wurden per SVG-Ausgabe geprüft.
- **Tempo Pi 5** (headless, ein Thread):
  - 6809-Spiele: Mine Storm etwa 5,4-fache Echtzeit.
  - UVMC2 Exact: Demo 0,48-fach, Last Human 0,79-fach, beides **zu langsam**. `VT_THREADS=1` hilft nicht.
  - UVMC2 Fast: Last Human 2,3-fach, das ist ok. Die Demo nutzt die SDK-Listen-Ausgabe nicht, Fast greift dort nicht, sie bleibt bei 0,48-fach.

- **Last während der Pi-Messungen** (2026-11-08): Das VectorPie-Menü (`vector_pie_menu`) lief mit etwa 28 % eines Kerns mit, dazu Screensaver und PipeWire. Load um 1,1 bis 1,5, Takt 2,4 GHz, keine Drosselung (`throttled=0x0`). Der Emulator nutzt einen von 4 Kernen. Die Hintergrundlast verschiebt die Werte deshalb nur wenig und erklärt den Abstand bei UVMC2 Exact (Faktor 1,3 bis 2) nicht. Gegenmaßnahmen, falls nötig: den Emu-Thread auf einen eigenen Kern pinnen (der Kern hat `host_pin`) und die Priorität erhöhen.

- **Spielauswahl:**
  - PC: Liste im Fenster (Screenshot geprüft). Ablauf Liste → Spiel als Kindprozess → zurück zur Liste getestet (`--pick 2 --child-frames 120 --frames 90`). Auflösung `-cart "last human"`, `vectrex -cart CPUTIME_TEST_64K` und unbekannter Name getestet.
  - Pi 5: `--list` findet alle 71 Module in `/usr/local/share/advance/image/vectrex`.

- **Optionsmenü:**
  - PC: Hauptseite, Steuerung P1, Stick und Bild per Screenshot geprüft (`--menu SEITE`). Die Standardbelegung zeigt die Belegung des Automaten.
  - `vxunit` (17 Prüfungen, alle ok): Spieldatei nur mit Abweichungen; ALLE SPIELE übernimmt nur die Änderungen; „wie alle Spiele“ löscht die Datei; `rom_dir` mit Leerzeichen; Rampe (Anlauf, Rücklauf, Umkehr, Kurve, Maximum, sofort).
  - **Nicht geprüft:** die Bedienung mit Tasten (keine simulierten Tasten auf dem Desktop des Besitzers), das Belegen mit `vp_capture` und das Speichern der Steuerung.

## Nicht verifiziert
- Das Programm **auf dem Automaten** (Board-Ausgabe, Controller, Ton, Spielauswahl am Board). Auf dem Pi liefen nur `vxtest` und `--list` in `/tmp`.
  - Ausnahme, aus Versehen: Am 2026-11-08 lief einmal `vectrex -cart armor --frames 1`. Das ging wahrscheinlich für ein Bild aufs Board, während der Screensaver es hielt. Danach war alles normal.
- Steuerung und Digital→Analog-Rampe mit echtem Input. Bisher nur kompiliert und gestartet, nicht gespielt.
- Ton angehört.

## Anforderungen und Entscheidungen
- Zyklengenau (CPU 6809, VIA 6522, Analogteil): erfüllt, siehe Timing-Testmodul.
- UVMC2: läuft. Auf dem Pi 5 nur im Fast-Modus in Echtzeit, dort ist die Vectrex-Seite exakt, das Timing des Moduls angenähert. Default `uvmc2_fast = 1`, pro Spiel umschaltbar.
- BIOS kommt mit. GPL-3 ist ok. Der Kern ist direkt eingebaut.
- Bildeinstellungen: Monitor horizontal/vertikal mit Rändern bei horizontal, dazu Größe, Breite/Höhe und Position.
- Steuerung pro Spiel. Default: 2 Controller mit je 4 Buttons. Digital→Analog mit einstellbarer Kurve.
- Farbprofile: dynamisch, also Teile der Spiele einfärben statt eines statischen Overlays (Konzept unten).
- BIOS-Bypass als Option. Optionsmenü über Tab und/oder F2.

### Konzept Farbprofile
- Der Monitor ist RGB ohne eigene Intensität: Die Vectrex-Helligkeit (0..127) wird mit der Farbe multipliziert (`draw.c`).
- Der Kern muss jede Linie markieren: mit dem 6809-PC bzw. der Rücksprungadresse der BIOS-Zeichenroutine und mit der Adresse der Vektorliste (X/U bei `Draw_VL*`). Dafür braucht es eine kleine Änderung an `vt_line` und `vecx.c` im Kern. Die wird in `VENDOR.md` festgehalten.
- Profilregeln pro Spiel (Schlüssel: ROM-Hash) prüfen Vektorliste, Aufrufer-PC, Region, Helligkeit und RAM-Bedingungen. Das Ergebnis ist eine Farbe, ein Verlauf oder eine Animation.
- UVMC2/PiTrex: kein 6809-PC, dort nur Region, Helligkeit und Reihenfolge.

## Bekannte Probleme und Einschränkungen
- **UVMC2 Demo** ist auf dem Pi 5 zu langsam (0,48-fach), weil sie nicht über die SDK-Listen-Ausgabe läuft. Eine Lösung wäre z. B. der Mehr-Thread-Modus des Kerns, falls der für solche Spiele greift. Das ist noch nicht untersucht.
- **Last Human (UVMC2)** zeichnet rund 5.700 Vektoren pro Bild. Auf dem USB-DVG (etwa 3 Mio. Punkte/s) wird das wahrscheinlich dunkel oder flimmert, auf BeamForge geht es eher. Muss am Automaten geprüft werden, eventuell braucht es `board_hz` pro Spiel.
- Spielnamen: `package/names.ini` deckt nur Namen ab, bei denen ich sicher bin. Die übrigen kommen aus Header oder Dateiname und sind teils unschön („JOHN DONDZILA (AGT)“, „THIS IS JUST“, „PATRIOTREMIX“). Am besten ergänzt du sie in `names.ini`.
- PiTrex-Programme mit der Endung `.img` erscheinen nicht in der Liste, weil die Endung mehrdeutig ist (z. B. `system.img`, das BIOS von AdvanceMESS). Direkt gestartet funktionieren sie.
- Der Browser zeichnet Text ohne Korrektur für den stehenden Monitor. Auf 3:4 wird er schmaler.
- Beim Start legt der Browser `/persistent/games/rom/vectrex` an, falls `/persistent/games` existiert. Auf dem Automaten ist der Ordner seit 2026-11-08 da und leer.
- Für einen stehenden Monitor gehe ich davon aus, dass die Board-Koordinaten bereits aus Sicht des Betrachters gedreht sind (das SDK dreht nichts). Am Automaten prüfen.
- `size = 1.0` bezieht sich auf die Canvas des Kerns (35000 × 41000). Manche Spiele, z. B. die Einstellungsseite von Last Human, zeichnen darüber hinaus.

## Nächste Schritte
1. Das Menü mit Tasten bedienen und belegen. Das musst du am PC oder am Automaten selbst machen.
2. Später: das VectorPie-Menü umstellen (Startvorlage und `gamelist.ini`, siehe „Tür zum VectorPie-Menü“). Erst wenn du das willst.
3. Paket/Deploy: `tools/pack.sh` und `tools/deploy.sh` nach Major Havoc, dann am Automaten testen.
4. Farbprofile: Linien im Kern markieren, Profilformat und Regel-Engine bauen.
5. UVMC2 Demo bzw. Spiele ohne Listen-Ausgabe auf dem Pi beschleunigen.
