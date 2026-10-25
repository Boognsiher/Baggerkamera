# Baggerkamera

WLAN-Videostream für eine ESP32-CAM, die in einen ferngesteuerten Bagger
eingebaut wird. Das Handy zeigt das Bild direkt im Browser an (kein App
nötig) – entweder über einen eigenen Hotspot der Kamera oder im selben
Heim-WLAN.

## Hardware

- ESP32-CAM Modul (AI-Thinker, das verbreitete ~5€-Board mit OV2640-Sensor)
- FTDI/USB-TTL-Adapter zum Flashen (das ESP32-CAM-Board hat keinen
  eigenen USB-Anschluss)
- Eigene 5V-Stromversorgung mit mindestens 2A (aus dem RC-Akku über einen
  5V-BEC/Spannungsregler, **nicht** über die 3.3V/5V-Leitung eines
  USB-Adapters – die liefert zu wenig Strom für Kamera + WLAN)
- 2x Mini-Servo (z. B. SG90) für Pan/Tilt der Kamera, plus eine
  Pan/Tilt-Halterung dafür

### Verkabelung zum Flashen

| ESP32-CAM | USB-TTL-Adapter |
|-----------|------------------|
| 5V        | 5V               |
| GND       | GND              |
| U0R (RX)  | TX               |
| U0T (TX)  | RX               |
| IO0       | GND (nur während des Flashens!) |

1. IO0 mit GND verbinden, Board (neu) starten → Bootloader-Modus
2. `pio run -t upload` (oder Upload-Button in VS Code/PlatformIO)
3. IO0-Brücke entfernen, Board neu starten → normaler Betrieb

### Servos anschließen

| Servo | ESP32-CAM Pin |
|---|---|
| Pan (links/rechts) | GPIO13 |
| Tilt (hoch/runter) | GPIO14 |
| Servo-VCC (beide) | 5V (gleiche externe Versorgung wie das Board – **nicht** aus dem 3.3V-Pin) |
| Servo-GND (beide) | GND |

GPIO13/14 sind auf dem AI-Thinker-Board frei, solange ihr keine SD-Karte
nutzt (die läuft über dieselben Pins), und sind keine "Strapping-Pins",
die den Bootvorgang stören könnten. Zwei Mini-Servos (SG90 o. ä.) ziehen
kurzzeitig mehrere hundert mA – aus demselben Grund wie bei der Kamera
unbedingt eine ordentliche externe 5V-Versorgung verwenden, sonst gibt es
Reboots oder zuckende Servos.

## Software einrichten

1. `include/secrets.h.example` nach `include/secrets.h` kopieren
2. WLAN-Zugangsdaten bzw. gewünschten Hotspot-Namen eintragen
3. `WIFI_MODE` wählen:
   - **`WIFI_MODE_STA_WITH_AP_FALLBACK`** (Standard, empfohlen): versucht
     zuerst das Heim-WLAN, und macht bei Bagger-Einsätzen außer Reichweite
     automatisch einen eigenen Hotspot auf.
   - `WIFI_MODE_AP_ONLY`: die Kamera macht immer ihren eigenen Hotspot auf
     (z. B. wenn ihr im Garten fahrt, wo kein Heimnetz hinreicht).
   - `WIFI_MODE_STA_ONLY`: nur Heim-WLAN, kein eigener Hotspot.
4. Flashen (siehe oben)

`secrets.h` ist in `.gitignore`, das Passwort landet also nicht im Git-Repo.

## Benutzung

- **Hotspot-Modus:** Am Handy mit dem WLAN `Baggerkamera` verbinden, dann
  `http://192.168.4.1/` öffnen (bei aktivierter Captive-Portal-Erkennung
  öffnet sich die Seite oft automatisch nach dem Verbinden).
- **Heimnetz-Modus:** Handy im selben WLAN, dann die IP-Adresse öffnen,
  die die ESP32 beim Start über Serial ausgibt, oder
  `http://baggerkamera.local/` (funktioniert zuverlässig auf iPhones;
  auf Android lieber die IP-Adresse nutzen).

Auf der Seite:
- Live-Bild als MJPEG-Stream
- Button zum Ein-/Ausschalten der Onboard-LED (Hilfslicht für dunkle
  Ecken/Motorraum)
- Auflösungs-Auswahl (kleinere Auflösung = weniger Verzögerung, wichtig
  wenn ihr nach dem Bild lenkt/steuert)
- Zwei Schieberegler für Pan/Tilt der Kamera, plus ein "Mitte"-Button
  zum Zurücksetzen auf 90°/90°

Direkte Endpunkte, falls ihr das Bild in eine eigene App/Website einbinden
wollt:
- `GET /stream` – MJPEG-Stream (multipart/x-mixed-replace)
- `GET /capture` – einzelnes JPEG-Standbild
- `GET /control?var=<name>&val=<n>` – Kamera-Einstellungen (`framesize`,
  `quality`, `vflip`, `hmirror`, `led`) und Servos (`pan` 0-180,
  `tilt` 30-150, Grenzen in `src/main.cpp` anpassbar)

## Einbautipps für den Bagger

- **Antenne/Reichweite:** Das WLAN-Modul auf dem ESP32-CAM hat eine sehr
  kleine PCB-Antenne. Metallgehäuse/Blech am Bagger schirmt stark ab –
  Kamera möglichst so einbauen, dass die Antennenseite frei nach oben
  oder zur Seite zeigt, nicht hinter Metallteilen versteckt.
- **Stromversorgung:** Ein sauberer 5V-BEC (aus dem Fahrakku) verhindert
  Reboots durch Spannungseinbrüche beim Kamerastart (die Firmware
  deaktiviert zusätzlich den Brownout-Detektor als Absicherung).
- **Erschütterungen:** Das Modul mit Schaumstoff/Doppelklebeband dämpfen,
  die Kamera-Flexkabel-Verbindung ist mechanisch empfindlich.
- **Sichtfeld:** Montage am Baggerausleger nahe der Schaufel gibt die
  beste Sicht zum Graben; ein zusätzlicher Blickwinkel auf die Ketten/den
  Unterwagen hilft beim Rangieren, falls Platz für ein zweites Modul ist.

## Andere ESP32-CAM-Boards

Nutzt ihr kein AI-Thinker-Board (z. B. M5Camera, TTGO T-Camera, ESP-EYE),
`include/camera_pins.h` mit dem passenden Pinout aus dem offiziellen
Arduino-ESP32 `CameraWebServer`-Beispiel ersetzen und die `board`-Zeile in
`platformio.ini` anpassen.
