#include "MotionSensors.h"

#include <AsyncMqttClient.h>
#include <AsyncWiFiSettings.h>

#include "GUI.h"
#include "defaults.h"
#include "globals.h"
#include "mqtt.h"
#include "string_utils.h"

namespace Motion {
int lastMotionValue = -1;

int pirPin = -1;
float pirTimeout = 0;
int lastPirValue = -1;
unsigned long lastPirMilli = 0;

int radarPin = -1;
float radarTimeout = 0;
int lastRadarValue = -1;
unsigned long lastRadarMilli = 0;
bool online;

void Setup() {
    if (pirPin >= 0)
        pinMode(pirPin, INPUT_PULLUP);
    if (radarPin >= 0)
        pinMode(radarPin, INPUT_PULLUP);
}

void ConnectToWifi() {
    pirPin = AsyncWiFiSettings.integer("pir_pin", -1, "PIR motion pin (-1 for disable)");
    pirTimeout = AsyncWiFiSettings.floating("pir_timeout", 0, 300, DEFAULT_DEBOUNCE_TIMEOUT, "PIR motion timeout (in seconds)");
    radarPin = AsyncWiFiSettings.integer("radar_pin", -1, "Radar motion pin (-1 for disable)");
    radarTimeout = AsyncWiFiSettings.floating("radar_timeout", 0, 300, DEFAULT_DEBOUNCE_TIMEOUT, "Radar motion timeout (in seconds)");
}

void SerialReport() {
    Serial.print("PIR Sensor:   ");
    Serial.println(pirPin >= 0 ? "enabled" : "disabled");
    Serial.print("Radar Sensor: ");
    Serial.println(radarPin >= 0 ? "enabled" : "disabled");
}

static void pirLoop() {
    if (pirPin < 0) return;
    bool detected = digitalRead(pirPin) == HIGH;
    if (detected) lastPirMilli = millis();
    unsigned long since = millis() - lastPirMilli;
    int pirValue = (detected || since < (pirTimeout * 1000)) ? HIGH : LOW;

    if (lastPirValue == pirValue) return;
    mqttClient.publish((roomsTopic + "/pir").c_str(), 0, true, pirValue == HIGH ? "ON" : "OFF");
    lastPirValue = pirValue;
}

static void radarLoop() {
    if (radarPin < 0) return;
    bool detected = digitalRead(radarPin) == HIGH;
    if (detected) lastRadarMilli = millis();
    unsigned long since = millis() - lastRadarMilli;
    int radarValue = (detected || since < (radarTimeout * 1000)) ? HIGH : LOW;

    if (lastRadarValue == radarValue) return;
    mqttClient.publish((roomsTopic + "/radar").c_str(), 0, true, radarValue == HIGH ? "ON" : "OFF");
    lastRadarValue = radarValue;
}

void Loop() {
    pirLoop();
    radarLoop();
    int motionValue = (lastRadarValue == HIGH || lastPirValue == HIGH) ? HIGH : LOW;
    if (lastMotionValue == motionValue) return;
    GUI::Motion(lastPirValue == HIGH, lastRadarValue == HIGH);
    mqttClient.publish((roomsTopic + "/motion").c_str(), 0, true, motionValue == HIGH ? "ON" : "OFF");
    lastMotionValue = motionValue;
}

bool SendDiscovery() {
    if (pirPin < 0 && radarPin < 0) return true;

    if (pirPin >= 0)
        if (!sendNumberDiscovery("Pir Timeout", EC_CONFIG)) return false;
    if (radarPin >= 0)
        if (!sendNumberDiscovery("Radar Timeout", EC_CONFIG)) return false;
    return sendBinarySensorDiscovery("Motion", EC_NONE, "motion");
}

bool Command(String& command, String& pay) {
    if (command == "pir_timeout") {
        pirTimeout = pay.toInt();
        spurt("/pir_timeout", pay);
    } else if (command == "radar_timeout") {
        radarTimeout = pay.toInt();
        spurt("/radar_timeout", pay);
    } else
        return false;
    return true;
}

bool SendOnline() {
    if (online) return true;
    if (!pub((roomsTopic + "/pir_timeout").c_str(), 0, true, String(pirTimeout).c_str()) && pub((roomsTopic + "/radar_timeout").c_str(), 0, true, String(radarTimeout).c_str()))
        return false;
    online = true;
    return true;
}
}  // namespace Motion
