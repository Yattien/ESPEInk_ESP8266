# ESPEInk-ESP8266
Erweiterung des ESP8266-Waveshare-Treibers um Wifi-Einrichtungsassistent, Deepsleep und MQTT-Funktionalität als Ergänzung zum FHEM-Modul `ESPEInk`.

![GitHub](https://img.shields.io/github/license/Yattien/ESPEInk_ESP8266)
[![GitHub release](https://img.shields.io/github/v/release/Yattien/ESPEInk_ESP8266?include_prereleases)](https://github.com/Yattien/ESPEInk_ESP8266/releases)
![GitHub All Releases](https://img.shields.io/github/downloads/Yattien/ESPEInk_ESP8266/total)
![GitHub issues](https://img.shields.io/github/issues-raw/Yattien/ESPEInk_ESP8266)

# Waveshare-Treiberversion
[29.10.2020](https://www.waveshare.com/wiki/File:E-Paper_ESP8266_Driver_Board_Code.7z)

# Installation
Das fertige Image kann per OTA (siehe auch OTA-Beispiel-Sketche) oder auch per [`esptool`](https://github.com/espressif/esptool) auf den ESP8266 geladen werden.

# Features
* **Einrichtungsmanager** für WiFi, MQTT und Deepsleep-Zeit
  - Verbinden mit dem AP `ESPEInk-APSetup` und im Browser die URL `http://192.168.4.1` aufrufen
  - MQTT-Server ist _optional_, falls keiner angegeben wird, wird kein MQTT verwendet
  - Sleeptime in Sekunden ist _optional_, wird keine angegeben, läuft der ESP ständig (=ähnlich der Original-Firmware)
  - Firmware-Basis-URL ist _optional_, wird keine angegeben, wird keine OTA-Update-Anfrage durchgeführt
  - die Einrichtungsdaten werden im ESP gespeichert und nicht mehr im Quellcode; sie sind damit auch nach einem Update noch verfügbar
* **OTA-Firmware-Update**
  - beim Start wird auf OTA-Updates geprüft - jedoch nicht, wenn der ESP aus dem deep-sleep kommt.
  - unterhalb der Firmware-Basis-URL werden zwei Dateien erwartet, Hauptnamensbestandteil ist die klein geschriebene (WiFi-)MAC-Adresse des ESP
  - `<MAC>.version` eine Datei, die nur eine Zahl, die Versionsnummer des zugehörigen Firmwareimages, enthält
  - `<MAC>.bin` das Firmware-Image
  - ein Update erfolgt nur, wenn die aktuelle Versionsnummer kleiner als die auf dem Webserver ist (ja, ist derzeit viel Handarbeit :wink:)
* **Deepsleep**
  - der ESP-Webserver wird 10s für Aktionen gestartet, danach geht er schlafen - falls eine Schlafdauer konfiguriert ist
  - wird ein manueller Upload über die Webseite oder ein ESPEInk-Upload gestartet, wird der Schlaf solange verzögert, bis das `SHOW`-Kommando zurückkehrt oder über die Abort-Seite abgebrochen wird
* **MQTT-Szenario**, wie unten stehend beschrieben
  - Funktioniert nicht :warning: standalone, benötigt also eine eingerichtete FHEM-Gegenseite
* **Reset-Seite** `http://<esp>/reset`, um den Einrichtungsassistenten (ohne Rückfrage) zu starten; die Wifi-Verbindungsdaten müssen erneut eingegeben werden
* **Abort-Seite** `http://<esp>/abort`, um einen verzögerten Schlaf (bspw. abgebrochener manueller Upload) sofort auszulösen 

# Abhängigkeiten
Da ich bereits viele Libs in meiner Arduino-Bauumgebung habe und diese somit nicht mehr "clean" ist, können es auch mehr Abhängigkeiten sein, die zusätzlich installiert werden müssen. Für Tipps bin ich dankbar :)

* ArduinoJSON >=6
* WiFiManager
* ?

# Pin-Belegung Lolin/Wemos D1 mini
e-Paper HAT|GPIO|Wemos D1
--------------|---|--------
CS|15|D8
RST|2|D4
DC|4|D2
BUSY|5|D1
DIN|13|D7
CLK|14|D5
GND|GND|G
VCC|3.3V|3V3

# Bekannte Fehler
* Das 2.13-Display funktioniert nicht mit einem "Wemos D1 mini" oder einem Clon davon im deepsleep-Mode. Grund dafür ist, dass die Reset-Leitung des Displays und die interne LED sich einen Port teilen und die LED irgendwo angesteuert wird - und sich das Display dann löscht. Falls jmd. eine Lösung hat, arbeite ich sie gern mit ein.

# MQTT-Szenario
Im Zusammenspiel mit dem FHEM-Modul `ESPEInk` kann man das EInk-Display dazu bringen, weniger Strom zu verbrauchen. Dazu kann man so vorgehen:
* das Attribut `interval` in ESPEInk steht auf einem hohen Wert, da kein automatischer Upload erfolgen soll (noch notwendiger Workaround)
* ein DOIF triggert die Konvertierung
* ein weiteres DOIF reagiert auf Änderungen im ESPEInk-Reading `result_picture` und setzt das MQTT-Topic `stat/display/needUpdate` mit dem QOS 1 (=wird im MQTT-Server zwischengespeichert, solange der ESP schläft)
* ein MQTT_DEVICE wartet auf das ESP-Signal im Topic `cmd/display/upload`
* der ESP erwacht und befragt das Topic `stat/display/needUpdate`, ob es was zu tun gibt. Falls nicht, geht er wieder schlafen (Wachzeit ~ 5s).
* falls es etwas zu tun gibt, startet der ESP seinen Webserver und setzt das MQTT-Topic `cmd/display/upload`
* das MQTT_DEVICE reagiert darauf und startet den ESPEink-upload

:exclamation: Untenstehende FHEM-Schnipsel sind RAW-Definitionen: wenn man die Definition kopiert, dürfen die Backslash nicht mit eingefügt werden.

## Benötigt wird
* ein eingerichtetes und funktionierendes FHEM-Modul `ESPEInk`
	```
	defmod display ESPEInk /opt/fhem/www/images/displayBackground.png
	attr display boardtype ESP8266
	attr display colormode color
	attr display convertmode level
	attr display definitionFile /opt/fhem/FHEM/eink.cfg
	attr display devicetype 7.5inch_e-Paper_HAT_(B)
	attr display picturefile /opt/fhem/www/images/displayBackground.png
	attr display interval 48600
	attr display timeout 50
	attr display url eink-panel
	```
* mosquitto als MQTT-Server (MQTT2_* kann leider kein QOS oder ich hab nicht herausgefunden wie)
* MQTT als Verbindung zu mosquitto
	```
	defmod mqtt_device MQTT <mqtt-server>:1883
	```
* ein DOIF als Trigger für die Konvertierung (derzeit ist die Konvertierung und der Upload noch gekoppelt), im Beispiel reagiere ich auf die Änderungen in einem dummy-Device `display_status`, welches bereits alle relevante Informationen enthält	
	```
	defmod display_status_doif DOIF ([display_status]) (\
		set display convert\
	)
	attr display_status_doif do always
	```
* ein DOIF, welches bei einem Update des konvertierten ESPEInk-Bildes reagiert und das Topic mit qos=1 setzt
	```
	defmod display_doif DOIF ([display:result_picture]) (\
		set mqtt_device publish qos:1 stat/display/needUpdate true\
	)
	attr display_doif do always
	```
* ein MQTT_DEVICE um auf die Nachrage des ESP zu reagieren (wichtig ist, dass das Attribut `subscribeReading_cmd` heißt, ansonsten wird das FHEM-Kommando nicht ausgeführt, zumindest nicht in meinen Tests)
	```
	defmod display_mqtt MQTT_DEVICE
	attr display_mqtt IODev mqtt_device
	attr display_mqtt subscribeReading_cmd {fhem("set display upload")} cmd/display/upload
	```
