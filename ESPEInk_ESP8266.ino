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

#include <WiFiManager.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <PubSubClient.h>

#include "scripts.h"    // JavaScript code
#include "css.h"        // Cascading Style Sheets
#include "html.h"       // HTML page of the tool
#include "epd.h"        // e-Paper driver

ESP8266WebServer server(80);
IPAddress myIP;       // IP address in your local wifi net
WiFiClient espClient;
PubSubClient mqttClient(espClient);

// -----------------------------------------------------------------------------------------------------
const int FW_VERSION = 10; // for OTA
// -----------------------------------------------------------------------------------------------------
const char *AP_NAME = "ESPEInk-APSetup";
const char *MQTT_CLIENT_NAME = "ESPEInk";
const char *CONFIG_FILE = "/config.json";
const float TICKS_PER_SECOND = 80000000; // 80 MHz processor

bool shouldSaveConfig = false;
bool isUpdateAvailable = false;
bool isDisplayUpdateRunning = false;
bool isMqttEnabled = false;

class Ctx {
public:
	Ctx() :
			mqttPort(1883),
					sleepTime(60) {
		strcpy(mqttUpdateStatusTopic, "stat/display/needUpdate");
		strcpy(mqttCommandTopic, "cmd/display/upload");
	}
	~Ctx() {
		if (customMqttServer) {
			delete customMqttServer;
		}
		if (customMqttPort) {
			delete customMqttPort;
		}
		if (customMqttUpdateStatusTopic) {
			delete customMqttUpdateStatusTopic;
		}
		if (customMqttCommandTopic) {
			delete customMqttCommandTopic;
		}
		if (customSleepTime) {
			delete customSleepTime;
		}
		if (customFirmwareUrl) {
			delete customFirmwareUrl;
		}
	}
	void initWifiManagerParameters() {
		customMqttServer = new WiFiManagerParameter("server", "MQTT server", mqttServer, 40);
		itoa(mqttPort, mqttPortAsString, 10);
		customMqttPort = new WiFiManagerParameter("port", "MQTT port", mqttPortAsString, 6);
		customMqttUpdateStatusTopic = new WiFiManagerParameter("updateTopic", "MQTT update topic", mqttUpdateStatusTopic, 128);
		customMqttCommandTopic = new WiFiManagerParameter("commandTopic", "MQTT command topic", mqttCommandTopic, 128);
		itoa(sleepTime, sleepTimeAsString, 10);
		customSleepTime = new WiFiManagerParameter("sleepTime", "sleep time in seconds", sleepTimeAsString, 33);
		customFirmwareUrl = new WiFiManagerParameter("firmwareUrl", "base URL for firmware images", firmwareUrl, 128);
	}
	void updateParameters() {
		strcpy(mqttServer, customMqttServer->getValue());
		mqttPort = atoi(customMqttPort->getValue());
		strcpy(mqttUpdateStatusTopic, customMqttUpdateStatusTopic->getValue());
		strcpy(mqttCommandTopic, customMqttCommandTopic->getValue());
		sleepTime = atoi(customSleepTime->getValue());
		strcpy(firmwareUrl, customFirmwareUrl->getValue());

		if (strlen(mqttServer) > 0) {
			isMqttEnabled = true;
		}
	}

	char mqttServer[40];
	char mqttPortAsString[6];
	int mqttPort;
	char mqttUpdateStatusTopic[128];
	char mqttCommandTopic[128];
	char sleepTimeAsString[33];
	long sleepTime;
	char firmwareUrl[128];

	WiFiManagerParameter *customMqttServer;
	WiFiManagerParameter *customMqttPort;
	WiFiManagerParameter *customMqttUpdateStatusTopic;
	WiFiManagerParameter *customMqttCommandTopic;
	WiFiManagerParameter *customSleepTime;
	WiFiManagerParameter *customFirmwareUrl;

};

Ctx ctx;

// -----------------------------------------------------------------------------------------------------
void setup() {
	Serial.begin(115200);
	pinMode(LED_BUILTIN, OUTPUT);
	digitalWrite(LED_BUILTIN, HIGH);

	getConfig(&ctx);

	ctx.initWifiManagerParameters();
	setupWifi(ctx);
	ctx.updateParameters();

	Serial.printf(" MQTT Server: %s:%d\n", ctx.mqttServer, ctx.mqttPort);
	Serial.printf(" MQTT UpdateStatusTopic: %s\n", ctx.mqttUpdateStatusTopic);
	Serial.printf(" MQTT CommandTopic: %s\n", ctx.mqttCommandTopic);
	Serial.printf(" sleep time: %ld\n", ctx.sleepTime);
	Serial.printf(" firmware base URL: %s\n", ctx.firmwareUrl);
	saveConfig(ctx);

	getUpdate();
	initializeSpi();
	myIP = WiFi.localIP();

	mqttClient.setServer(ctx.mqttServer, ctx.mqttPort);
	mqttClient.setCallback(callback);

	Serial.println("Setup complete.");
}

// -----------------------------------------------------------------------------------------------------
void getConfig(Ctx *ctx) {
	if (SPIFFS.begin()) {
		if (SPIFFS.exists(CONFIG_FILE)) {
			Serial.println("reading config file");
			File configFile = SPIFFS.open(CONFIG_FILE, "r");
			if (configFile) {
				DynamicJsonDocument jsonDocument(512);
				DeserializationError error = deserializeJson(jsonDocument, configFile);
				if (!error) {
					strlcpy(ctx->mqttServer, jsonDocument["mqttServer"] | "", sizeof ctx->mqttServer);
					ctx->mqttPort = jsonDocument["mqttPort"] | 1883;
					strlcpy(ctx->mqttUpdateStatusTopic, jsonDocument["mqttUpdateStatusTopic"] | "", sizeof ctx->mqttUpdateStatusTopic);
					strlcpy(ctx->mqttCommandTopic, jsonDocument["mqttCommandTopic"] | "", sizeof ctx->mqttCommandTopic);
					ctx->sleepTime = jsonDocument["sleepTime"] | 0;
					strlcpy(ctx->firmwareUrl, jsonDocument["firmwareUrl"] | "", sizeof ctx->firmwareUrl);

				} else {
					Serial.println(" failed to load json config");
				}
				configFile.close();
			}
		}
	} else {
		Serial.println("failed to mount FS");
	}
}

// -----------------------------------------------------------------------------------------------------
void setupWifi(const Ctx &ctx) {
	WiFiManager wifiManager;
	requestMqttParameters(ctx, &wifiManager);
	wifiManager.autoConnect(AP_NAME);
	Serial.println("Connected.");
}

// -----------------------------------------------------------------------------------------------------
void requestMqttParameters(const Ctx &ctx, WiFiManager *wifiManager) {
	wifiManager->addParameter(ctx.customMqttServer);
	wifiManager->addParameter(ctx.customMqttPort);
	wifiManager->addParameter(ctx.customMqttUpdateStatusTopic);
	wifiManager->addParameter(ctx.customMqttCommandTopic);
	wifiManager->addParameter(ctx.customSleepTime);
	wifiManager->addParameter(ctx.customFirmwareUrl);
	wifiManager->setSaveConfigCallback(saveConfigCallback);
}

// -----------------------------------------------------------------------------------------------------
void saveConfig(const Ctx &ctx) {
	if (shouldSaveConfig) {
		Serial.println("saving config...");
		File configFile = SPIFFS.open(CONFIG_FILE, "w");
		if (!configFile) {
			Serial.println("failed to open config file for writing");
		}
		DynamicJsonDocument jsonDocument(512);
		jsonDocument["mqttServer"] = ctx.mqttServer;
		jsonDocument["mqttPort"] = ctx.mqttPort;
		jsonDocument["mqttUpdateStatusTopic"] = ctx.mqttUpdateStatusTopic;
		jsonDocument["mqttCommandTopic"] = ctx.mqttCommandTopic;
		jsonDocument["sleepTime"] = ctx.sleepTime;
		jsonDocument["firmwareUrl"] = ctx.firmwareUrl;
		if (serializeJson(jsonDocument, configFile) == 0) {
			Serial.println("Failed to write to file");
		}
		configFile.close();
		Serial.println("config saved");
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

	Serial.printf("Checking for firmware update, version file '%s'.\n", firmwareVersionUrl.c_str());
	HTTPClient httpClient;
	httpClient.begin(firmwareVersionUrl);
	int httpCode = httpClient.GET();
	if (httpCode == 200) {
		String newFWVersion = httpClient.getString();
		Serial.printf(" current firmware version: %d, available version: %s", FW_VERSION, newFWVersion.c_str());
		int newVersion = newFWVersion.toInt();
		if (newVersion > FW_VERSION) {
			Serial.println(" updating...");
			String firmwareImageUrl = firmwareUrl;
			firmwareImageUrl.concat(".bin");
			t_httpUpdate_return ret = ESPhttpUpdate.update(firmwareImageUrl);

			switch (ret) {
				case HTTP_UPDATE_FAILED:
					Serial.printf(" update failed: (%d): %s\n", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
					break;

				case HTTP_UPDATE_NO_UPDATES:
					Serial.printf(" update file '%s' found\n", firmwareImageUrl.c_str());
					break;
			}
		} else {
			Serial.println(" we are up to date");
		}

	} else if (httpCode == 404) {
		Serial.println(" no firmware version file found");

	} else {
		Serial.printf(" firmware version check failed, got HTTP response code %d\n", httpCode);
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
	if (isMqttEnabled && !mqttClient.connected()) {
		reconnect();
		Serial.println(" reconnected, waiting for incoming MQTT message");
		// get max 100 messages
		for (int i = 0; i < 100; ++i) {
			mqttClient.loop();
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

	static bool serverStarted = false;
	if (!serverStarted) {
		initializeWebServer();
		server.begin();
		serverStarted = true;

		if (isUpdateAvailable) {
			mqttClient.publish(ctx.mqttCommandTopic, "true");
			isUpdateAvailable = false;
		}
		Serial.println("Webserver started, waiting 10s for updates");

	} else {
		server.handleClient();

		int decile = fmod(difference / (TICKS_PER_SECOND / 100.0), 100.0);
		static bool ledStatus = false;
		if (!ledStatus && decile == 95) {
			digitalWrite(LED_BUILTIN, LOW);
			ledStatus = true;
		} else if (ledStatus && decile == 0) {
			digitalWrite(LED_BUILTIN, HIGH);
			ledStatus = false;
		}
	}

	bool isTimeToSleep = false;
	if (!isTimeToSleep && ctx.sleepTime > 0) { // check if 10 seconds have passed
		if (difference > 10 * TICKS_PER_SECOND) {
			isTimeToSleep = true;
		}

	} else if (!isTimeToSleep && isMqttEnabled) {
		if (isMqttEnabled) {
			mqttClient.disconnect();
			delay(100);
		}
		isTimeToSleep = true;
	}

	if (!isDisplayUpdateRunning && isTimeToSleep) {
		Serial.printf("Going to sleep for %ld seconds.\n", ctx.sleepTime);
		ESP.deepSleep(ctx.sleepTime * 1000000);
		delay(100);
	}
}

// -----------------------------------------------------------------------------------------------------
void factoryReset() {
	server.send(200, "text/plain", "Resetting WLAN settings...\r\n");
	delay(200);

	WiFiManager wifiManager;
	wifiManager.resetSettings();
	ESP.restart();
}

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
		// clientID, username, password, willTopic, willQoS, willRetain, willMessage, cleanSession
		if (!mqttClient.connect(MQTT_CLIENT_NAME, NULL, NULL, NULL, 0, 0, NULL, 0)) {
			delay(1000);
		} else {
			boolean rc = mqttClient.subscribe(ctx.mqttUpdateStatusTopic, 1);
			if (rc) {
				Serial.printf(" subscribed to %s\n", ctx.mqttUpdateStatusTopic);
			} else {
				Serial.printf(" subscription to %s failed: %d\n", ctx.mqttUpdateStatusTopic, rc);
			}
		}
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
	Serial.println("SHOW");
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
