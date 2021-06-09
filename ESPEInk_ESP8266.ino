/**
 * Replacement for waveshares ino-File "Loader.ino"
 * Author: Jendaw, 09.03.2020
 *
 *                     +--\/\/-+
 *    +--------- RST  1| |    | |22  TX  GPIO1
 *    |     ADC0 A0   2| |    | |21  RX  GPIO3
 *    +-- GPIO16 D0   4| +----+ |20  D1  GPIO5  BUSY
 *        GPIO14 D5   5|        |19  D2  GPIO4  DC
 *        GPIO12 D6   6|        |18  D3  GPIO0
 *        GPIO13 D7   7|        |17  D4  GPIO2  RST
 *    CS  GPIO15 D8  16|        |15  GND
 *               3V3  8|        |USB 5V
 *                     +--------+
 */

/* if the ESP does not wakeup from deepsleep a 220 resistor between SD0(MISO) and 3.3v might help */

#include <WiFiManager.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <PubSubClient.h>
#include "ctx.h"

#include "scripts.h"    // JavaScript code
#include "css.h"        // Cascading Style Sheets
#include "html.h"       // HTML page of the tool
#include "epd.h"        // e-Paper driver

ESP8266WebServer server(80);
IPAddress myIP;       // IP address in your local wifi net
WiFiClientSecure espClient;
PubSubClient mqttClient(espClient);

// -----------------------------------------------------------------------------------------------------
const int FW_VERSION = 18; // for OTA
// -----------------------------------------------------------------------------------------------------
const char *CONFIG_FILE = "/config.json";
const float TICKS_PER_SECOND = 80000000; // 80 MHz processor
const int UPTIME_SEC = 10;

char accessPointName[24];
bool shouldSaveConfig = false;
bool isUpdateAvailable = false;
bool isDisplayUpdateRunning = false;
bool isMqttEnabled = false;

Ctx ctx;

// -----------------------------------------------------------------------------------------------------
void setup() {
	String resetReason = ESP.getResetReason();
	Serial.begin(115200);
	Serial.println("\r\nESPEInk_ESP8266 v" + String(FW_VERSION) + ", reset reason='" + resetReason + "'...");
	Serial.println("Entering setup...");

//	pinMode(LED_BUILTIN, OUTPUT); // won't work, waveshare uses D2 as DC
//	digitalWrite(LED_BUILTIN, HIGH);

	getConfig();
	initMqttClientName();
	initAccessPointName();

	ctx.initWifiManagerParameters();
	setupWifi();
	ctx.updateParameters();
	isMqttEnabled = ctx.isMqttEnabled();

	Serial.println(" Using configuration:");
	Serial.printf("  MQTT Server: %s:%d, Client: %s\r\n", ctx.mqttServer, ctx.mqttPort, ctx.mqttClientName);
	Serial.printf("  MQTT UpdateStatusTopic: %s\r\n", ctx.mqttUpdateStatusTopic);
	Serial.printf("  MQTT CommandTopic: %s\r\n", ctx.mqttCommandTopic);
	Serial.printf("  sleep time: %ld\r\n", ctx.sleepTime);
	Serial.printf("  firmware base URL: %s\r\n", ctx.firmwareUrl);
	saveConfig();

	if (resetReason != "Deep-Sleep Wake") {
		getUpdate();
	}
	myIP = WiFi.localIP();
	setupMqtt();

	Serial.println("Setup complete.");
}

// -----------------------------------------------------------------------------------------------------
void getConfig() {
	if (SPIFFS.begin()) {
		if (SPIFFS.exists(CONFIG_FILE)) {
			Serial.println(" Reading config file...");
			File configFile = SPIFFS.open(CONFIG_FILE, "r");
			if (configFile) {
				DynamicJsonDocument jsonDocument(512);
				DeserializationError error = deserializeJson(jsonDocument, configFile);
				if (!error) {
					strlcpy(ctx.mqttServer, jsonDocument["mqttServer"] | "", sizeof ctx.mqttServer);
					ctx.mqttPort = jsonDocument["mqttPort"] | 1883;
					strlcpy(ctx.mqttUser, jsonDocument["mqttUser"] | "", sizeof ctx.mqttUser);
					strlcpy(ctx.mqttPassword, jsonDocument["mqttPassword"] | "", sizeof ctx.mqttPassword);
					strlcpy(ctx.mqttFingerprint, jsonDocument["mqttFingerprint"] | "", sizeof ctx.mqttFingerprint);
					strlcpy(ctx.mqttClientName, jsonDocument["mqttClientName"] | "", sizeof ctx.mqttClientName);
					strlcpy(ctx.mqttUpdateStatusTopic, jsonDocument["mqttUpdateStatusTopic"] | "", sizeof ctx.mqttUpdateStatusTopic);
					strlcpy(ctx.mqttCommandTopic, jsonDocument["mqttCommandTopic"] | "", sizeof ctx.mqttCommandTopic);
					ctx.sleepTime = jsonDocument["sleepTime"] | 0;
					strlcpy(ctx.firmwareUrl, jsonDocument["firmwareUrl"] | "", sizeof ctx.firmwareUrl);

					Serial.println(" Config file read.");
				} else {
					Serial.println("  Failed to load json config.");
				}
				configFile.close();
			}
		}
	} else {
		Serial.println("  Failed to mount FS (probably initial start), continuing w/o config...");
	}
}

// -----------------------------------------------------------------------------------------------------
void initAccessPointName() {
	sprintf(accessPointName, "ESPEInk-AP-%s", getMAC().c_str());
}

// -----------------------------------------------------------------------------------------------------
void initMqttClientName() {
	if (!strlen(ctx.mqttClientName)) {
		sprintf(ctx.mqttClientName, "ESPEInk_%s", getMAC().c_str());
	}
}

// -----------------------------------------------------------------------------------------------------
void setupWifi() {
	Serial.println(" Connecting to WiFi...");

	WiFiManager wifiManager;
	wifiManager.setDebugOutput(false);
	wifiManager.setTimeout(300);
	requestMqttParameters(&wifiManager);
	initAccessPointName();
	if (!wifiManager.autoConnect(accessPointName)) {
		Serial.println("  Failed to connect, resetting.");
		WiFi.disconnect();
		delay(3000);
		ESP.restart();
		delay(3000);
	}
	Serial.println(" Connected to WiFi.");
}

// -----------------------------------------------------------------------------------------------------
void requestMqttParameters(WiFiManager *wifiManager) {
	wifiManager->addParameter(ctx.customMqttServer);
	wifiManager->addParameter(ctx.customMqttPort);
	wifiManager->addParameter(ctx.customMqttUser);
	wifiManager->addParameter(ctx.customMqttPassword);
	wifiManager->addParameter(ctx.customMqttFingerprint);
	wifiManager->addParameter(ctx.customMqttUpdateStatusTopic);
	wifiManager->addParameter(ctx.customMqttCommandTopic);
	wifiManager->addParameter(ctx.customSleepTime);
	wifiManager->addParameter(ctx.customFirmwareUrl);
	wifiManager->setSaveConfigCallback(saveConfigCallback);
}

// -----------------------------------------------------------------------------------------------------
void saveConfig() {
	if (shouldSaveConfig) {
		Serial.println(" Saving config...");
		File configFile = SPIFFS.open(CONFIG_FILE, "w");
		if (!configFile) {
			Serial.println("  Failed to open config file for writing.");
		}
		DynamicJsonDocument jsonDocument(512);
		jsonDocument["mqttServer"] = ctx.mqttServer;
		jsonDocument["mqttPort"] = ctx.mqttPort;
		jsonDocument["mqttUser"] = ctx.mqttUser;
		jsonDocument["mqttPassword"] = ctx.mqttPassword;
		jsonDocument["mqttFingerprint"] = ctx.mqttFingerprint;
		jsonDocument["mqttClientName"] = ctx.mqttClientName;
		jsonDocument["mqttUpdateStatusTopic"] = ctx.mqttUpdateStatusTopic;
		jsonDocument["mqttCommandTopic"] = ctx.mqttCommandTopic;
		jsonDocument["sleepTime"] = ctx.sleepTime;
		jsonDocument["firmwareUrl"] = ctx.firmwareUrl;
		if (serializeJson(jsonDocument, configFile) == 0) {
			Serial.println("  Failed to write to file.");
		}
		configFile.close();
		Serial.println(" Config saved.");
	}
}

// -----------------------------------------------------------------------------------------------------
void saveConfigCallback() {
	shouldSaveConfig = true;
}

// -----------------------------------------------------------------------------------------------------
String getMAC() {
	uint8_t mac[6];
	WiFi.macAddress(mac);

	char result[14];
	snprintf(result, sizeof(result), "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

	return String(result);
}

// -----------------------------------------------------------------------------------------------------
void getUpdate() {
	if (!ctx.firmwareUrl || strlen(ctx.firmwareUrl) == 0) {
		return;
	}

	String mac = getMAC();
	String firmwareUrl = String(ctx.firmwareUrl);
	firmwareUrl.concat(mac);
	String firmwareVersionUrl = firmwareUrl;
	firmwareVersionUrl.concat(".version");

	Serial.printf(" Checking for firmware update, version file '%s'...\r\n", firmwareVersionUrl.c_str());
	HTTPClient httpClient;
	httpClient.begin(espClient, firmwareVersionUrl);
	int httpCode = httpClient.GET();
	if (httpCode == 200) {
		String newFWVersion = httpClient.getString();
		Serial.printf("  Current firmware version: %d, available version: %s", FW_VERSION, newFWVersion.c_str());
		int newVersion = newFWVersion.toInt();
		if (newVersion > FW_VERSION) {
			Serial.println("  Updating...");
			String firmwareImageUrl = firmwareUrl;
			firmwareImageUrl.concat(".bin");
			t_httpUpdate_return ret = ESPhttpUpdate.update(espClient, firmwareImageUrl);

			switch (ret) {
				case HTTP_UPDATE_FAILED:
					Serial.printf("  Update failed: (%d): %s\r\n", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
					break;

				case HTTP_UPDATE_NO_UPDATES:
					Serial.printf("  Update file '%s' found\r\n", firmwareImageUrl.c_str());
					break;

				case HTTP_UPDATE_OK:
					Serial.printf("  Updated\r\n");
					break;
			}

		} else if (newVersion == -1) {
			factoryReset();

		} else {
			Serial.println("  We are up to date.");
		}

	} else if (httpCode == 404) {
		Serial.println("  No firmware version file found.");

	} else {
		Serial.printf("  Firmware version check failed, got HTTP response code %d.\r\n", httpCode);
	}
	httpClient.end();
}

// -----------------------------------------------------------------------------------------------------
void initializeSpi() {
	pinMode(CS_PIN, OUTPUT);
	pinMode(RST_PIN, OUTPUT);
	pinMode(DC_PIN, OUTPUT);
	pinMode(BUSY_PIN, INPUT);
	SPI.begin();
}

// -----------------------------------------------------------------------------------------------------
void setupMqtt() {
	mqttClient.setServer(ctx.mqttServer, ctx.mqttPort);
	mqttClient.setCallback(callback);
}

// -----------------------------------------------------------------------------------------------------
void initializeWebServer() {
	server.on("/", handleBrowserCall);
	server.on("/styles.css", sendCSS);
	server.on("/processingA.js", sendJS_A);
	server.on("/processingB.js", sendJS_B);
	server.on("/processingC.js", sendJS_C);
	server.on("/processingD.js", sendJS_D);
	server.on("/LOAD", EPD_Load);
	server.on("/EPD", EPD_Init);
	server.on("/NEXT", EPD_Next);
	server.on("/SHOW", EPD_Show);
	server.on("/reset", factoryReset);
	server.on("/abort", abortDisplayUpdate);
	server.onNotFound(handleNotFound);
}

// -----------------------------------------------------------------------------------------------------
void loop() {
	if (!isDisplayUpdateRunning && isMqttEnabled && !mqttClient.connected()) {
		reconnect();
		Serial.println(" Reconnected, waiting for incoming MQTT messages...");
		for (int i = 0; i < 100; ++i) {
			mqttClient.loop();
			delay(10);
		}
		if (!isUpdateAvailable) {
			Serial.println(" No update available.");
		}
	}

	static unsigned long startCycle = ESP.getCycleCount();
	unsigned long currentCycle = ESP.getCycleCount();
	unsigned long difference;
	if (currentCycle < startCycle) {
		difference = (4294967295 - startCycle + currentCycle);
	} else {
		difference = (currentCycle - startCycle);
	}

	if ((isMqttEnabled && isUpdateAvailable)
			|| (isMqttEnabled && isDisplayUpdateRunning)
			|| !isMqttEnabled) {
		static bool serverStarted = false;
		if (!serverStarted) {
			initializeWebServer();
			server.begin();
			serverStarted = true;

			if (isUpdateAvailable) {
				mqttClient.publish(ctx.mqttCommandTopic, "true");
				delay(100);
			}
			Serial.printf("Webserver started, waiting %sfor data\r\n", isMqttEnabled ? "" : "10s ");

		} else {
			server.handleClient();

			int decile = fmod(difference / (TICKS_PER_SECOND / 100.0), 100.0);
			static bool ledStatus = false;
			if (!ledStatus && decile == 95) {
//				digitalWrite(LED_BUILTIN, LOW);
				ledStatus = true;
			} else if (ledStatus && decile == 0) {
//				digitalWrite(LED_BUILTIN, HIGH);
				ledStatus = false;
			}
		}
	}

	bool isTimeToSleep = false;
	if (isMqttEnabled && !isUpdateAvailable) {
		isTimeToSleep = true;

	} else if (difference > UPTIME_SEC * TICKS_PER_SECOND) {
		isTimeToSleep = true;
	}

	if (!isDisplayUpdateRunning) {
		if (isTimeToSleep) {
			if (ctx.sleepTime > 0) {
				disconnect();
				Serial.printf("\r\nGoing to sleep for %ld seconds.\r\n\r\n", ctx.sleepTime);
				ESP.deepSleep(ctx.sleepTime * 1000000);
				delay(100);

			} else { // avoid overheating
				startCycle = ESP.getCycleCount();
				delay(1000);
			}
		}
	}
}

// -----------------------------------------------------------------------------------------------------
void factoryReset() {
	server.send(200, "text/plain", "Resetting WLAN settings...\r\r\n");
	delay(200);

	WiFiManager wifiManager;
	wifiManager.resetSettings();
	ESP.restart();
}

// -----------------------------------------------------------------------------------------------------
void abortDisplayUpdate() {
	isDisplayUpdateRunning = false;
}

// -----------------------------------------------------------------------------------------------------
void callback(char* topic, byte* message, unsigned int length) {
	String messageTemp;

	for (int i = 0; i < length; i++) {
		messageTemp += (char) message[i];
	}

	if (String(topic) == ctx.mqttUpdateStatusTopic
			&& messageTemp == "true") {
		isUpdateAvailable = true;
	}

	Serial.print("Callback called, isUpdateAvailable=");
	Serial.println(messageTemp);
}

// -----------------------------------------------------------------------------------------------------
void reconnect() {
	Serial.println("Connecting to MQTT...");
	while (!mqttClient.connected()) {
		verifyFingerprint();
		// clientID, username, password, willTopic, willQoS, willRetain, willMessage, cleanSession
		if (!mqttClient.connect(ctx.mqttClientName, ctx.mqttUser, ctx.mqttPassword, NULL, 0, 0, NULL, 0)) {
			delay(1000);
		} else {
			boolean rc = mqttClient.subscribe(ctx.mqttUpdateStatusTopic, 1);
			if (rc) {
				Serial.printf(" Subscribed to %s\r\n", ctx.mqttUpdateStatusTopic);
			} else {
				Serial.printf(" Subscription to %s failed: %d\r\n", ctx.mqttUpdateStatusTopic, rc);
			}
		}
	}
}

// -----------------------------------------------------------------------------------------------------
void verifyFingerprint() {
	if (ctx.mqttFingerprint && strlen(ctx.mqttFingerprint) > 0) {
		if(!espClient.verify(ctx.mqttFingerprint, ctx.mqttServer)) {
			Serial.printf("MQTT fingerprint '%s' does not match, rebooting...\r\n", ctx.mqttFingerprint);
			Serial.flush();
			delay(200);
			ESP.restart();
		} else {
			Serial.println("MQTT fingerprint does match");
		}
	}
}

// -----------------------------------------------------------------------------------------------------
void disconnect() {
	if (mqttClient.connected()) {
		Serial.println("Disconnecting from MQTT...");
		mqttClient.disconnect();
		delay(5);
	}
}

// -----------------------------------------------------------------------------------------------------
void handleBrowserCall() {
	isDisplayUpdateRunning = true;
	handleRoot();
}

// -----------------------------------------------------------------------------------------------------
// waveshare display part
// -----------------------------------------------------------------------------------------------------
void EPD_Init() {
	isDisplayUpdateRunning = true;
	isUpdateAvailable = false;
	initializeSpi();
	EPD_dispIndex = ((int) server.arg(0)[0] - 'a')
			+ (((int) server.arg(0)[1] - 'a') << 4);
	// Print log message: initialization of e-Paper (e-Paper's type)
	Serial.printf("EPD %s\r\n", EPD_dispMass[EPD_dispIndex].title);

	// Initialization
	EPD_dispInit();
	server.send(200, "text/plain", "Init ok\r\n");
}

void EPD_Load() {
	//server.arg(0) = data+data.length+'LOAD'
	String p = server.arg(0);
	if (p.endsWith("LOAD")) {
		int index = p.length() - 8;
		int L = ((int) p[index] - 'a') + (((int) p[index + 1] - 'a') << 4)
				+ (((int) p[index + 2] - 'a') << 8)
				+ (((int) p[index + 3] - 'a') << 12);
		if (L == (p.length() - 8)) {
			Serial.println("LOAD");
			// if there is loading function for current channel (black or red)
			// Load data into the e-Paper
			if (EPD_dispLoad != 0)
				EPD_dispLoad();
		}
	}
	server.send(200, "text/plain", "Load ok\r\n");
}

void EPD_Next() {
	Serial.println("NEXT");

	// Instruction code for for writting data into
	// e-Paper's memory
	int code = EPD_dispMass[EPD_dispIndex].next;

	// If the instruction code isn't '-1', then...
	if (code != -1) {
		// Do the selection of the next data channel
		EPD_SendCommand(code);
		delay(2);
	}
	// Setup the function for loading choosen channel's data
	EPD_dispLoad = EPD_dispMass[EPD_dispIndex].chRd;

	server.send(200, "text/plain", "Next ok\r\n");
}

void EPD_Show() {
	Serial.println("\r\nSHOW\r\n");
	// Show results and Sleep
	EPD_dispMass[EPD_dispIndex].show();
	server.send(200, "text/plain", "Show ok\r\n");
	delay(200);
	isDisplayUpdateRunning = false;
}

void handleNotFound() {
	String message = "File Not Found\n\n";
	message += "URI: ";
	message += server.uri();
	message += "\nMethod: ";
	message += (server.method() == HTTP_GET) ? "GET" : "POST";
	message += "\nArguments: ";
	message += server.args();
	message += "\n";
	for (uint8_t i = 0; i < server.args(); i++) {
		message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
	}
	server.send(200, "text/plain", message);
	Serial.print("Unknown URI: ");
	Serial.println(server.uri());
}
