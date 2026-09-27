#include <ArduinoJson.h>
#include <ESP8266WebServer.h>
#include <LittleFS.h>
#include <PubSubClient.h>
#include <SPI.h>
#include <WiFiManager.h>
#include <ESP8266mDNS.h>

#include <algorithm>

#include "../vendor/Loader/scripts.h"
#include "../vendor/Loader/css.h"
#include "../vendor/Loader/html.h"
#include "../vendor/Loader/epd.h"

// Firmware for the Waveshare ESP8266 loader board.
static const char *CONFIG_FILE = "/config.json";
static const uint32_t CONFIG_TIMEOUT_SECONDS = 900;
static const uint32_t FIRST_SETUP_RETRY_SECONDS = 15;
static const uint32_t MQTT_CONNECT_TIMEOUT_MS = 12000;
static const uint32_t MQTT_CHECK_WINDOW_MS = 3000;
static const uint32_t IMAGE_SERVER_TIMEOUT_MS = 120000;
static const uint32_t DEFAULT_SLEEP_SECONDS = 3600;
static const char *MQTT_CLIENT_ID = "ESPEInk_ESP8266";
static const char *SETUP_AP_SSID = "ESPEInk-Setup";
static const char *SETUP_AP_PASSWORD = "ESPEInkSetup";
// Waveshare ESP8266 board mapping from the original example/Loader.
#define PIN_BUSY BUSY_PIN
#define PIN_DC DC_PIN
#define PIN_RESET RST_PIN
#define PIN_CS CS_PIN
#define PIN_MOSI 13 // GPIO13 / D7
#define PIN_SCK 14  // GPIO14 / D5

ESP8266WebServer server(80);
IPAddress myIP;
WiFiClient mqttTransport;
PubSubClient mqttClient(mqttTransport);

struct Settings {
	char mqttHost[64] = "";
	char mqttPortText[6] = "1883";
	char mqttUser[64] = "";
	char mqttPassword[64] = "";
	char updateTopic[96] = "stat/display/needUpdate";
	char commandTopic[96] = "cmd/display/upload";
	char sleepSecondsText[12] = "3600";
	uint16_t mqttPort = 1883;
	uint32_t sleepSeconds = DEFAULT_SLEEP_SECONDS;
} settings;

bool saveSettings = false;
bool updateAvailable = false;
bool mqttMessageReceived = false;
bool imageUploadFinished = false;
bool updateFlagResetPending = false;
bool displayInitialized = false;
bool configPortalTimedOut = false;
uint32_t imageServerStartedAt = 0;

static uint32_t parseSleepSeconds(const char *value) {
	char *end = nullptr;
	const unsigned long seconds = strtoul(value, &end, 10);
	if (end == value || *end != '\0' || seconds == 0) return DEFAULT_SLEEP_SECONDS;
	return static_cast<uint32_t>(std::min(seconds, 4294UL));
}

static void initializeBoard() {
	pinMode(PIN_CS, OUTPUT);
	pinMode(PIN_RESET, OUTPUT);
	pinMode(PIN_DC, OUTPUT);
	pinMode(PIN_BUSY, INPUT);
	digitalWrite(PIN_CS, HIGH);
	SPI.begin(); // ESP8266 hardware SPI uses GPIO13 (MOSI) and GPIO14 (SCK).
}

static void clearRetainedUpdateRequest() {
	if (!updateFlagResetPending) return;
	for (uint8_t attempt = 0; attempt < 5; ++attempt) {
		if (!mqttClient.connected()) {
			const bool connected = settings.mqttUser[0]
				? mqttClient.connect(MQTT_CLIENT_ID, settings.mqttUser, settings.mqttPassword)
				: mqttClient.connect(MQTT_CLIENT_ID);
			if (!connected) {
				delay(250);
				continue;
			}
		}
		if (mqttClient.publish(settings.updateTopic, "false", true)) {
			mqttClient.loop();
			const uint32_t ackStarted = millis();
			while (mqttClient.connected() && millis() - ackStarted < 500) {
				mqttClient.loop();
				delay(10);
			}
			updateFlagResetPending = false;
			Serial.println(F("Retained needUpdate flag reset to false"));
			return;
		}
		delay(250);
	}
	Serial.println(F("WARNING: could not clear retained needUpdate flag"));
	if (mqttClient.connected()) mqttClient.disconnect();
	WiFi.disconnect(true);
	WiFi.mode(WIFI_OFF);
	ESP.deepSleep(1000000UL, WAKE_RF_DEFAULT);
	delay(100);
}

void sendCSS();
void sendJS_A();
void sendJS_B();
void sendJS_C();
void sendJS_D();
void EPD_Init();
void EPD_Load();
void EPD_Next();
void EPD_Show();
void handleNotFound();

static void saveConfigCallback() {
	saveSettings = true;
}

static void configureSetupPortal(WiFiManager &manager) {
	manager.setDebugOutput(true);
	manager.setConfigPortalTimeout(CONFIG_TIMEOUT_SECONDS);
	manager.setConfigPortalBlocking(true);
	manager.setBreakAfterConfig(true);
	manager.setShowInfoErase(false);
	manager.setShowInfoUpdate(false);
	manager.setMinimumSignalQuality(0);
	manager.setWiFiAPChannel(1);
	manager.setWiFiAPHidden(false);
	manager.setAPStaticIPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
	manager.setAPCallback([](WiFiManager *) {
		Serial.printf("Setup AP started: SSID='%s', password='%s', URL=http://192.168.4.1\n", SETUP_AP_SSID, SETUP_AP_PASSWORD);
	});
}

static void loadSettings() {
	if (!LittleFS.begin()) {
		Serial.println(F("LittleFS mount failed; formatting filesystem"));
		if (!LittleFS.format()) {
			Serial.println(F("LittleFS format/mount failed; settings unavailable"));
			return;
		}
		if (!LittleFS.begin()) {
			Serial.println(F("LittleFS remount failed; settings unavailable"));
			return;
		}
	}
	if (!LittleFS.exists(CONFIG_FILE)) return;

	File file = LittleFS.open(CONFIG_FILE, "r");
	if (!file) return;
	DynamicJsonDocument doc(1024);
	DeserializationError error = deserializeJson(doc, file);
	file.close();
	if (error) {
		Serial.printf("Invalid config JSON: %s\n", error.c_str());
		LittleFS.remove(CONFIG_FILE);
		return;
	}
	strlcpy(settings.mqttHost, doc["mqttHost"] | "", sizeof(settings.mqttHost));
	strlcpy(settings.mqttPortText, doc["mqttPort"] | "1883", sizeof(settings.mqttPortText));
	strlcpy(settings.mqttUser, doc["mqttUser"] | "", sizeof(settings.mqttUser));
	strlcpy(settings.mqttPassword, doc["mqttPassword"] | "", sizeof(settings.mqttPassword));
	strlcpy(settings.updateTopic, doc["updateTopic"] | "stat/display/needUpdate", sizeof(settings.updateTopic));
	strlcpy(settings.commandTopic, doc["commandTopic"] | "cmd/display/upload", sizeof(settings.commandTopic));
	strlcpy(settings.sleepSecondsText, doc["sleepSeconds"] | "3600", sizeof(settings.sleepSecondsText));
	settings.mqttPort = constrain(atoi(settings.mqttPortText), 1, 65535);
	settings.sleepSeconds = parseSleepSeconds(settings.sleepSecondsText);
}

static void persistSettings() {
	if (!saveSettings) return;
	DynamicJsonDocument doc(1024);
	doc["mqttHost"] = settings.mqttHost;
	doc["mqttPort"] = settings.mqttPortText;
	doc["mqttUser"] = settings.mqttUser;
	doc["mqttPassword"] = settings.mqttPassword;
	doc["updateTopic"] = settings.updateTopic;
	doc["commandTopic"] = settings.commandTopic;
	doc["sleepSeconds"] = settings.sleepSecondsText;
	File file = LittleFS.open(CONFIG_FILE, "w");
	if (file) {
		serializeJson(doc, file);
		file.close();
	} else {
		Serial.println(F("Could not save config"));
	}
	saveSettings = false;
}

static void createConfigParameters(
	WiFiManager &manager,
	WiFiManagerParameter &mqttHostParameter,
	WiFiManagerParameter &mqttPortParameter,
	WiFiManagerParameter &mqttUserParameter,
	WiFiManagerParameter &mqttPasswordParameter,
	WiFiManagerParameter &updateTopicParameter,
	WiFiManagerParameter &commandTopicParameter,
	WiFiManagerParameter &sleepSecondsParameter) {
	manager.addParameter(&mqttHostParameter);
	manager.addParameter(&mqttPortParameter);
	manager.addParameter(&mqttUserParameter);
	manager.addParameter(&mqttPasswordParameter);
	manager.addParameter(&updateTopicParameter);
	manager.addParameter(&commandTopicParameter);
	manager.addParameter(&sleepSecondsParameter);
	manager.setSaveConfigCallback(saveConfigCallback);
}

static bool configureAndConnect(bool showSetupPortal) {
	WiFi.mode(WIFI_STA);
	WiFiManager manager;
	manager.setDebugOutput(false);
	manager.setConnectTimeout(20);
	manager.setConfigPortalTimeout(CONFIG_TIMEOUT_SECONDS);
	const bool isInitialSetup = WiFi.SSID().length() == 0;
	bool connected = false;
	bool configurationPortalStarted = false;
	WiFiManagerParameter mqttHostParameter("mqttHost", "MQTT host (leer = MQTT aus)", settings.mqttHost, sizeof(settings.mqttHost));
	WiFiManagerParameter mqttPortParameter("mqttPort", "MQTT port", settings.mqttPortText, sizeof(settings.mqttPortText));
	WiFiManagerParameter mqttUserParameter("mqttUser", "MQTT user (optional)", settings.mqttUser, sizeof(settings.mqttUser));
	WiFiManagerParameter mqttPasswordParameter("mqttPassword", "MQTT password (optional)", settings.mqttPassword, sizeof(settings.mqttPassword), "type='password' autocomplete='new-password'");
	WiFiManagerParameter updateTopicParameter("updateTopic", "MQTT update topic", settings.updateTopic, sizeof(settings.updateTopic));
	WiFiManagerParameter commandTopicParameter("commandTopic", "MQTT upload command topic", settings.commandTopic, sizeof(settings.commandTopic));
	WiFiManagerParameter sleepSecondsParameter("sleepSeconds", "Deepsleep (Sekunden)", settings.sleepSecondsText, sizeof(settings.sleepSecondsText));
	if (isInitialSetup || showSetupPortal) {
		createConfigParameters(manager, mqttHostParameter, mqttPortParameter, mqttUserParameter, mqttPasswordParameter, updateTopicParameter, commandTopicParameter, sleepSecondsParameter);
		configurationPortalStarted = true;
		configureSetupPortal(manager);
		WiFi.mode(WIFI_AP);
		connected = manager.startConfigPortal(SETUP_AP_SSID, SETUP_AP_PASSWORD);
		configPortalTimedOut = !connected;
		if (!connected && WiFi.SSID().length() > 0) {
			Serial.println(F("Setup portal ended without saved WiFi; trying stored credentials"));
			WiFi.mode(WIFI_STA);
			WiFi.begin();
			const uint32_t connectStarted = millis();
			while (WiFi.status() != WL_CONNECTED && millis() - connectStarted < 20000) delay(100);
			connected = WiFi.status() == WL_CONNECTED;
		}
		if (!connected && WiFi.SSID().length() == 0) configPortalTimedOut = true;
	} else {
		WiFi.begin();
		const uint32_t connectStarted = millis();
		while (WiFi.status() != WL_CONNECTED && millis() - connectStarted < 20000) delay(100);
		connected = WiFi.status() == WL_CONNECTED;
		if (!connected) {
			Serial.println(F("WiFi unavailable; starting temporary setup portal"));
			createConfigParameters(manager, mqttHostParameter, mqttPortParameter, mqttUserParameter, mqttPasswordParameter, updateTopicParameter, commandTopicParameter, sleepSecondsParameter);
			configurationPortalStarted = true;
			configureSetupPortal(manager);
			WiFi.mode(WIFI_AP);
			connected = manager.startConfigPortal(SETUP_AP_SSID, SETUP_AP_PASSWORD);
			configPortalTimedOut = !connected;
		}
	}
	if (configurationPortalStarted) {
		if (connected) {
			strlcpy(settings.mqttHost, mqttHostParameter.getValue(), sizeof(settings.mqttHost));
			strlcpy(settings.mqttPortText, mqttPortParameter.getValue(), sizeof(settings.mqttPortText));
			strlcpy(settings.mqttUser, mqttUserParameter.getValue(), sizeof(settings.mqttUser));
			strlcpy(settings.mqttPassword, mqttPasswordParameter.getValue(), sizeof(settings.mqttPassword));
			strlcpy(settings.updateTopic, updateTopicParameter.getValue(), sizeof(settings.updateTopic));
			strlcpy(settings.commandTopic, commandTopicParameter.getValue(), sizeof(settings.commandTopic));
			strlcpy(settings.sleepSecondsText, sleepSecondsParameter.getValue(), sizeof(settings.sleepSecondsText));
			settings.mqttPort = constrain(atoi(settings.mqttPortText), 1, 65535);
			settings.sleepSeconds = parseSleepSeconds(settings.sleepSecondsText);
			saveSettings = true;
			persistSettings();
		}
		return connected && WiFi.status() == WL_CONNECTED;
	}
	if (connected) {
		settings.mqttPort = constrain(atoi(settings.mqttPortText), 1, 65535);
		settings.sleepSeconds = parseSleepSeconds(settings.sleepSecondsText);
		Serial.printf("WiFi connected: %s\n", WiFi.localIP().toString().c_str());
	}
	return connected && WiFi.status() == WL_CONNECTED;
}

static bool mqttConnectAndReadRequest() {
	if (settings.mqttHost[0] == '\0' || settings.updateTopic[0] == '\0' || settings.commandTopic[0] == '\0') return false;
	updateAvailable = false;
	mqttMessageReceived = false;
	mqttClient.setServer(settings.mqttHost, settings.mqttPort);
	mqttClient.setBufferSize(256);
	mqttClient.setCallback([](char *topic, byte *payload, unsigned int length) {
		if (strcmp(topic, settings.updateTopic) != 0) return;
		mqttMessageReceived = true;
		String value;
		value.reserve(length);
		for (unsigned int i = 0; i < length; ++i) value += static_cast<char>(payload[i]);
		value.trim();
		updateAvailable = value.equalsIgnoreCase("true") || value == "1";
	});

	const uint32_t started = millis();
	while (!mqttClient.connected() && millis() - started < MQTT_CONNECT_TIMEOUT_MS) {
		bool connected = settings.mqttUser[0]
			? mqttClient.connect(MQTT_CLIENT_ID, settings.mqttUser, settings.mqttPassword)
			: mqttClient.connect(MQTT_CLIENT_ID);
		if (!connected) delay(500);
	}
	if (!mqttClient.connected()) return false;
	if (!mqttClient.subscribe(settings.updateTopic, 1)) return false;
	const uint32_t checkStarted = millis();
	bool receivedMessage = false;
	while (millis() - checkStarted < MQTT_CHECK_WINDOW_MS && !receivedMessage) {
		mqttClient.loop();
		receivedMessage = mqttMessageReceived;
		delay(10);
	}
	return receivedMessage && updateAvailable;
}

static void enterDeepSleep(const char *reason) {
	Serial.printf("%s; sleeping %lu seconds\n", reason, static_cast<unsigned long>(settings.sleepSeconds));
	clearRetainedUpdateRequest();
	if (mqttClient.connected()) mqttClient.disconnect();
	WiFi.disconnect(true);
	WiFi.mode(WIFI_OFF);
	// RF must be enabled on wake: every wake cycle needs WiFi (STA or setup AP).
	ESP.deepSleep(settings.sleepSeconds * 1000000UL, WAKE_RF_DEFAULT);
	delay(100);
}

static void startImageServer() {
	myIP = WiFi.localIP();
	MDNS.begin("espeink");
	server.on("/", handleRoot);
	server.on("/styles.css", sendCSS);
	server.on("/processingA.js", sendJS_A);
	server.on("/processingB.js", sendJS_B);
	server.on("/processingC.js", sendJS_C);
	server.on("/processingD.js", sendJS_D);
	server.on("/LOAD", EPD_Load);
	server.on("/EPD", EPD_Init);
	server.on("/NEXT", EPD_Next);
	server.on("/SHOW", EPD_Show);
	server.onNotFound(handleNotFound);
	server.begin();
	imageServerStartedAt = millis();
	if (mqttClient.connected() && !mqttClient.publish(settings.commandTopic, "true")) {
		Serial.println(F("MQTT upload command publish failed"));
		enterDeepSleep("Could not announce upload command");
		return;
	}
	Serial.printf("Image server ready: http://%s/\n", myIP.toString().c_str());
}

void setup() {
	Serial.begin(115200);
	const String resetReason = ESP.getResetReason();
	const bool deepSleepWake = resetReason == "Deep-Sleep Wake";
	Serial.printf("\nESPEInk ESP8266 (%s)\n", resetReason.c_str());
	// Re-enable RF in case a previous firmware slept with WAKE_RF_DISABLED.
	WiFi.forceSleepWake();
	delay(1);
	WiFi.persistent(true);
	loadSettings();
	if (!configureAndConnect(!deepSleepWake)) {
		if (configPortalTimedOut && WiFi.SSID().length() == 0) {
			Serial.printf("Setup portal ended without WiFi. Restarting in %u seconds to reopen it.\n", FIRST_SETUP_RETRY_SECONDS);
			delay(FIRST_SETUP_RETRY_SECONDS * 1000);
			ESP.restart();
			return;
		}
		enterDeepSleep(configPortalTimedOut ? "Setup portal timed out / WiFi unavailable" : "WiFi unavailable");
		return;
	}
	if (!mqttConnectAndReadRequest()) {
		enterDeepSleep(settings.mqttHost[0] == '\0' ? "MQTT not configured" : "MQTT unavailable or no update request");
		return;
	}
	initializeBoard();
	startImageServer();
}

void loop() {
	if (imageUploadFinished) {
		delay(250); // allow the HTTP response to leave before powering down
		updateFlagResetPending = true;
		enterDeepSleep("Image upload complete");
		return;
	}
	if (imageServerStartedAt && millis() - imageServerStartedAt > IMAGE_SERVER_TIMEOUT_MS) {
		enterDeepSleep("Image server timeout");
		return;
	}
	if (imageServerStartedAt) server.handleClient();
	if (mqttClient.connected()) mqttClient.loop();
}

void EPD_Init() {
	if (server.arg(0).length() < 2) { server.send(400, "text/plain", "Invalid display type"); return; }
	const char low = server.arg(0)[0];
	const char high = server.arg(0)[1];
	if (low < 'a' || low > 'p' || high < 'a' || high > 'p') { server.send(400, "text/plain", "Invalid display type"); return; }
	const int index = (low - 'a') + ((high - 'a') << 4);
	const size_t displayCount = sizeof(EPD_dispMass) / sizeof(EPD_dispMass[0]);
	if (static_cast<size_t>(index) >= displayCount) { server.send(400, "text/plain", "Invalid display type"); return; }
	EPD_dispIndex = index;
	Serial.printf("EPD %s\n", EPD_dispMass[EPD_dispIndex].title);
	EPD_dispInit();
	displayInitialized = true;
	server.send(200, "text/plain", "Init ok\r\n");
}

void EPD_Load() {
	if (!displayInitialized) { server.send(409, "text/plain", "Display not initialized"); return; }
	String payload = server.arg(0);
	if (payload.length() >= 8 && payload.endsWith("LOAD")) {
		const int index = payload.length() - 8;
		const int encodedLength = (payload[index] - 'a') + ((payload[index + 1] - 'a') << 4)
			+ ((payload[index + 2] - 'a') << 8) + ((payload[index + 3] - 'a') << 12);
		if (encodedLength == index && EPD_dispLoad != nullptr) EPD_dispLoad();
	}
	server.send(200, "text/plain", "Load ok\r\n");
}

void EPD_Next() {
	if (!displayInitialized) { server.send(409, "text/plain", "Display not initialized"); return; }
	const int code = EPD_dispMass[EPD_dispIndex].next;
	if (code != -1) { EPD_SendCommand(code); delay(2); }
	EPD_dispLoad = EPD_dispMass[EPD_dispIndex].chRd;
	server.send(200, "text/plain", "Next ok\r\n");
}

void EPD_Show() {
	if (!displayInitialized) { server.send(409, "text/plain", "Display not initialized"); return; }
	EPD_dispMass[EPD_dispIndex].show();
	server.send(200, "text/plain", "Show ok\r\n");
	imageUploadFinished = true;
}

void handleNotFound() {
	server.send(404, "text/plain", "Not found: " + server.uri());
}
