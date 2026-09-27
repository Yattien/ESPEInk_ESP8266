# ESPEInk ESP8266

Neue, stromsparende Firmware für die Waveshare ESP8266 e-Paper-Treiberplatine. Der Waveshare-Loader aus `vendor/Loader` liefert die Weboberfläche, das Upload-Protokoll und die Displaytreiber.

## Ablauf

1. Beim ersten Start verbindet sich der ESP mit einem temporären, passwortgeschützten Setup-Access-Point (`ESPEInk-Setup-…`). Im Captive Portal werden WLAN, MQTT-Broker, Topics und Deep-Sleep-Zeit konfiguriert. Der Setup-Vorgang hat ein Zeitlimit von fünf Minuten.
2. WLAN- und Anwendungseinstellungen werden in LittleFS gespeichert. Ein leerer MQTT-Host deaktiviert MQTT; in diesem Betriebsmodus wird nicht hochgefahren, um einen Upload anzunehmen.
3. Nach WLAN-Verbindung wird `stat/display/needUpdate` abonniert. Nur ein empfangenes `true` oder `1` startet den Bildserver; bei `false`, fehlender MQTT-Konfiguration oder Brokerfehler geht das Gerät schlafen.
4. Mit gestartetem Bildserver wird `cmd/display/upload=true` (nicht retained) veröffentlicht. Nach erfolgreichem `/SHOW` veröffentlicht die Firmware das retained `stat/display/needUpdate=false` und geht danach direkt in Deep Sleep. Bleibt der Upload aus, greift nach zwei Minuten ein Timeout und das Update-Flag bleibt gesetzt.
5. Updates werden manuell über den USB-Web-Flasher installiert.

Die MQTT-Nachricht `stat/display/needUpdate` muss vom Broker als retained gespeichert werden, damit sie nach dem Aufwachen zugestellt wird. Nach erfolgreichem `/SHOW` veröffentlicht die Firmware automatisch `false` mit Retain-Flag auf demselben Topic, bevor sie schlafen geht. Bei nicht abgeschlossenem Upload bleibt das Update angefordert und kann nach dem nächsten Aufwachen erneut ausgeführt werden.

## Einrichtung

Beim fabrikneuen Gerät, bei einem normalen Neustart/Reset oder wenn keine WLAN-Zugangsdaten gespeichert sind, wird das Setup-Portal gestartet. Der Deep-Sleep-Wakeup überspringt das Portal. Verbinde dich mit dem passwortgeschützten 2,4-GHz-Netz `ESPEInk-Setup` auf Kanal 1 (Passwort `ESPEInkSetup`) und öffne `http://192.168.4.1`, falls das Captive Portal nicht automatisch erscheint. Der AP bleibt 15 Minuten verfügbar. Nach einem Timeout ohne gespeichertes WLAN startet das Gerät nach 15 Sekunden neu und öffnet den AP erneut. SSID und Portal-URL werden zusätzlich über die serielle Schnittstelle mit 115200 Baud ausgegeben.

## Build mit PlatformIO und Pipenv

Der Build erfolgt verbindlich mit **PlatformIO in Pipenv**. Boardprofil, Flashparameter und Arduino-Bibliotheken sind in `platformio.ini` festgelegt; die Python-Abhängigkeit ist über `Pipfile` und `Pipfile.lock` reproduzierbar.

Voraussetzungen: Python 3.12 und Pipenv. Unter Debian/Ubuntu beispielsweise:

```sh
sudo apt install pipenv python3-venv
```

Im Projektstamm ausführen:

```sh
PIPENV_VENV_IN_PROJECT=1 pipenv sync
PIPENV_VENV_IN_PROJECT=1 pipenv run pio run -e esp12e
```

Das erzeugte Image liegt anschließend unter `.pio/build/esp12e/firmware.bin`. Der erfolgreiche Referenzbuild benötigt etwa 52 KiB RAM und 448 KiB Flash.

### Per USB flashen

1. ESP8266 per USB verbinden; bei Bedarf Bootloader-Modus aktivieren.
2. Port ermitteln, etwa `/dev/ttyUSB0` oder `/dev/ttyACM0`.
3. Flashvorgang ausführen:

```sh
PIPENV_VENV_IN_PROJECT=1 pipenv run pio run -e esp12e -t upload --upload-port /dev/ttyUSB0
```

Serielle Ausgabe öffnen:

```sh
PIPENV_VENV_IN_PROJECT=1 pipenv run pio device monitor --baud 115200 --port /dev/ttyUSB0
```

Die lokale Umgebung `.venv/` und PlatformIO-Buildartefakte `.pio/` sind absichtlich nicht versioniert.

Konfiguration: Der Build verwendet das Boardprofil `esp12e`, Flash-Modus `dio`, 40 MHz Flash-Takt und LittleFS. GPIO-Belegung: CS=15, RST=2, DC=4, BUSY=5, MOSI=13, SCK=14. Für Deep Sleep muss GPIO16 (D0) mit RESET verbunden sein.

## USB-Flash im Chrome-Browser von GitHub

Der Ordner `web-flasher/` enthält einen [ESP Web Tools](https://github.com/esphome/esp-web-tools)-Installer. Das ist **kein OTA-Update über WLAN**: Für Installation oder Aktualisierung muss der ESP8266 per USB angeschlossen und im Browser ausgewählt werden. Bei Änderungen an Firmware, Loader, PlatformIO- oder Pipenv-Konfiguration baut der Pages-Workflow das Image neu und veröffentlicht es zusammen mit dem Installer. Nach Einrichtung von GitHub Pages lautet die Installer-Adresse `https://yattien.github.io/ESPEInk_ESP8266/`. Chrome/Edge und eine HTTPS-Seite sind erforderlich; für den USB-Adapter muss ggf. der Bootloader-Modus manuell aktiviert werden. Bei Updates im ESP-Web-Tools-Dialog **Erase device / Gerät löschen nicht auswählen**, damit WLAN-Zugangsdaten und LittleFS-Konfiguration erhalten bleiben. Nur beim Erstflash/Recovery löschen.

`.github/workflows/deploy-web-flasher.yml` baut die Firmware und veröffentlicht sie zusammen mit dem Installer auf GitHub Pages. Pages muss vor dem ersten Workflowlauf einmal manuell aktiviert werden: **Repository Settings → Pages → Build and deployment → Source: GitHub Actions**. Der Workflow-Token kann die Pages-Site nicht selbst anlegen; bei Organisations-Repositories muss die Organisation Pages ebenfalls erlauben. Der Installer stellt immer die Firmware des Branches bereit, der den letzten erfolgreichen Pages-Workflow ausgelöst hat.

Der Installer lädt ESP Web Tools von `unpkg.com` (festgelegte Version im HTML). Für maximal reproduzierbare/supply-chain-kontrollierte Deployments sollte die Bibliothek lokal eingebunden werden.

## Abhängigkeiten

PlatformIO installiert ArduinoJson 6, WiFiManager und PubSubClient aus den in `platformio.ini` festgelegten Abhängigkeiten. Die ESP8266 Arduino Core-Bibliotheken stellen Webserver, SPIFFS und WLAN bereit.

## Hinweise

- Die MQTT-Anmeldedaten werden lokal unverschlüsselt in SPIFFS abgelegt.
- Der Upload-Webserver lauscht ohne Authentifizierung auf Port 80 und sollte nur in einem vertrauenswürdigen LAN erreichbar sein.
- Vor Einsatz bitte mit der konkreten ESP8266-Platine, Displayvariante und dem eigenen FHEM-/MQTT-Setup testen.