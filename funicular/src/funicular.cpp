#include <time.h>

#include <Arduino.h>
#define DEVICE "ESP8266 (NodeMCU-12E) - Funicular"
#define SERIAL_BAUD 112500
#define TZ_INFO "ICT-7"

#include <WiFiClient.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#define WIFI_SSID "Bearbear"
#define WIFI_PASSWORD "earn180347"
WiFiClient wifi_client_mqtt;

#include <PubSubClient.h>
#define MQTT_SERVER "185.84.160.95"
#define MQTT_PORT 1883
#define MQTT_USER "cat"
#define MQTT_PASSWORD "j9I5cNen1Aj7"
#define MQTT_TOPIC "cat/feeder"
PubSubClient mqtt_client(wifi_client_mqtt);

#include <InfluxDbClient.h>
#define INFLUXDB_URL "http://185.84.160.95:8086"
#define INFLUXDB_TOKEN "539MPPO9k9JtsL/f6h23cGMWaeZ7WtAeidPbUyR3mOU="
#define INFLUXDB_ORG "cat"
#define INFLUXDB_BUCKET "box"
InfluxDBClient influxdb_client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN);

#define BUTTON_PIN D7

// function declaration
void establish_mqtt_connection();

// Function to establish the MQTT connection
void establish_mqtt_connection() {
    while (!mqtt_client.connected()) {
        Serial.println("[Cat] Attempting MQTT connection...");
        if (mqtt_client.connect(DEVICE, MQTT_USER, MQTT_PASSWORD)) {
            Serial.println("[Cat] Connected to MQTT server.");
            mqtt_client.subscribe(MQTT_TOPIC);

        } else {
            Serial.print("[Cat] Failed to connect to MQTT server, rc=");
            Serial.println(mqtt_client.state());
            delay(2000);
        }
    }
}

// global variables
unsigned long previous_millis = 0;
const long interval = 1024;

void setup() {
    Serial.begin(SERIAL_BAUD);

    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    Serial.println("\n[Cat] Connecting to WiFi... ");
    while (WiFi.status() != WL_CONNECTED) { delay(interval); }
    Serial.println("[Cat] Connected to WiFi.\n");

    timeSync(TZ_INFO, "1.th.pool.ntp.org", "time.navy.mi.th");

    mqtt_client.setServer(MQTT_SERVER, MQTT_PORT);
    establish_mqtt_connection();

    if (influxdb_client.validateConnection()) {
        Serial.printf("[Cat] Connected to the InfluxDB server! (%s)\n", influxdb_client.getServerUrl().c_str());

    } else {
        Serial.print("[Cat] Failed to connect to the InfluxDB server:");
        Serial.println(influxdb_client.getLastErrorMessage());
    }

    pinMode(BUTTON_PIN, INPUT_PULLUP);
}

void loop() {
    unsigned long currentMillis = millis();
    if (currentMillis - previous_millis >= interval) {
        previous_millis = currentMillis;

        if (!wifi_client_mqtt.connected()) {
            establish_mqtt_connection();
        }

        int read_button_pin = digitalRead(BUTTON_PIN);
        if (read_button_pin == LOW) {
            Serial.println("[Cat] Button pressed, opening the lid...");
            mqtt_client.publish(MQTT_TOPIC, "open_lid");
        }
    }
}
