# boot_display

Kleine C-Anwendung für den Raspberry Pi, die auf einem **SSD1306-OLED (128x64)** am **I2C-Bus 1** das Wort **„BOOT“** zentriert anzeigt.

Sie kommt ohne externe Bibliotheken aus und nutzt nur die Linux-I2C-Schnittstelle (`/dev/i2c-1`).

## Hardware

| Display | Raspberry Pi                 |
|---------|------------------------------|
| VCC     | 3,3 V (Pin 1)                |
| GND     | GND (Pin 6)                  |
| SDA     | GPIO 2 / SDA1 (Pin 3)        |
| SCL     | GPIO 3 / SCL1 (Pin 5)        |

Standardadresse des Displays: `0x3C` (manche Module: `0x3D`).

## Voraussetzungen

I2C aktivieren (danach neu starten):

```bash
sudo raspi-config nonint do_i2c 0
```

Werkzeuge installieren (Compiler, `make`, `i2cdetect`):

```bash
sudo apt install build-essential i2c-tools
```

Prüfen, ob das Display erkannt wird (es sollte `3c` erscheinen):

```bash
i2cdetect -y 1
```

## Bauen

```bash
make
```

Alternativ ohne Makefile:

```bash
gcc -O2 -Wall -o boot_display boot_display.c
```

## Verwendung

```bash
./boot_display              # zeigt "BOOT"
./boot_display HALLO        # zeigt beliebigen Text (A-Z, 0-9, Leerzeichen)
./boot_display BOOT 0x3D    # andere I2C-Adresse
```

Ist der Benutzer nicht in der Gruppe `i2c`, braucht der Start `sudo`. Alternativ fügst du den Benutzer der Gruppe hinzu (danach neu anmelden):

```bash
sudo usermod -aG i2c $USER
```

Kleinbuchstaben werden als Großbuchstaben dargestellt, andere Zeichen als Leerzeichen.

## Funktionsweise

- Initialisiert den SSD1306 (Ladungspumpe, 128x64-Multiplex, horizontale Adressierung).
- Zeichnet den Text mit einem 5x7-Pixel-Font in einen 1024-Byte-Bildspeicher.
- Wählt die größte Skalierung (max. 4-fach), bei der der Text noch passt, und zentriert ihn horizontal und vertikal. „BOOT“ wird 92×28 Pixel groß.
- Überträgt den Bildspeicher in Blöcken zu 32 Byte über I2C.

Der Inhalt bleibt nach Programmende auf dem Display stehen.

## Installieren (Anzeige direkt beim Booten)

```bash
sudo make install
```

Das erledigt:

- `boot_display` nach `/usr/local/bin` kopieren
- `boot-display.service` nach `/etc/systemd/system` installieren und aktivieren
- `i2c-dev` in `/etc/modules-load.d/` eintragen, falls das Modul noch nicht automatisch geladen wird

Der Dienst läuft in der frühen Bootphase (`DefaultDependencies=no`, `WantedBy=sysinit.target`). Er wartet nur, bis `/dev/i2c-1` existiert, und zeigt dann „BOOT“ an, typischerweise 1–3 Sekunden nach dem Kernelstart.

Ohne Neustart testen:

```bash
sudo systemctl start boot-display.service
```

Prüfen, wann der Dienst beim Booten gelaufen ist:

```bash
systemd-analyze critical-chain boot-display.service
```

Wieder entfernen:

```bash
sudo make uninstall
```

Ein anderes Installationsziel ist über `PREFIX` möglich (z. B. `sudo make install PREFIX=/usr`). Mit `DESTDIR` wird nur in ein Staging-Verzeichnis installiert, ohne `systemctl`-Aufrufe.

## Fehlersuche

| Problem | Ursache / Lösung |
|---|---|
| `Kann /dev/i2c-1 nicht oeffnen` | I2C nicht aktiviert oder fehlende Rechte (siehe oben) |
| `I2C write: Remote I/O error` | falsche Adresse oder Verdrahtung; mit `i2cdetect -y 1` prüfen |
| Bild verschoben oder Streifen am Rand | Display hat vermutlich einen **SH1106**-Chip (oft 1,3″) statt SSD1306 |
| Text steht auf dem Kopf | `0xA1`/`0xC8` in `oled_init()` durch `0xA0`/`0xC0` ersetzen |

## Dateien

- `boot_display.c`: Quellcode
- `Makefile`: Build-Datei mit `install`/`uninstall`
- `boot-display.service`: systemd-Dienst für die Anzeige beim Booten
