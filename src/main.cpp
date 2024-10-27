#include <time.h>

#include <SPI.h>
#include <Wire.h>
#include <Arduino.h>
#define DEVICE "ESP8266 (NodeMCU-12E)"
#define SERIAL_BAUD 112500
#define TZ_INFO "ICT-7"

#include <Adafruit_GFX.h>
#include <Adafruit_I2CDevice.h>

#include <WiFiClient.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#define WIFI_SSID "<your_ssid>"
#define WIFI_PASSWORD "<your_password>"
WiFiClient wifi_client;
WiFiClient wifi_client_mqtt;

#include <PubSubClient.h>
#define MQTT_SERVER "<your_ip_address>"
#define MQTT_PORT 1883
#define MQTT_USER "cat"
#define MQTT_PASSWORD "j9I5cNen1Aj7"
#define MQTT_TOPIC "cat/feeder"
PubSubClient mqtt_client(wifi_client_mqtt);

#include <InfluxDbClient.h>
#define INFLUXDB_URL "http://<your_ip_address>:8086"
#define INFLUXDB_TOKEN "539MPPO9k9JtsL/f6h23cGMWaeZ7WtAeidPbUyR3mOU="
#define INFLUXDB_ORG "cat"
#define INFLUXDB_BUCKET "box"
InfluxDBClient influxdb_client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN);

#include <Redis.h>
#define REDIS_ADDR "<your_ip_address>"
#define REDIS_PORT 6379
#define REDIS_PASSWORD "j9I5cNen1Aj7"
Redis redis_client(wifi_client);

/**
 * Line Notify (Temporary use for Cat Feeder)
 * * You need to read this because it affects the code.
 * ! Line Notify will deprecated on April 1, 2025
 * TODO: You need to find another service to replace Line Notify such LINE Messaging API (https://developers.line.biz/en/services/messaging-api/).
 */
#include <ESP_Line_Notify.h>
LineNotifyClient line_notify_client;
#define LINE_NOTIFY_API_TOKEN "awZLNagxdWriAKmc6byBTO5mQVIJ4UER11IvPHUdd0J"

#include <SPI.h>
#include <Servo.h>

#define TRIGGER_PIN D1
#define ECHO_PIN D2
#define SERVO_PIN D5
#define BUTTON_PIN D7

// global variables
Servo servo;
Point sensor("cat");

unsigned long previous_millis = 0;
const long interval = 1024;
bool lid_opened = false;

// function declaration
void set_latest_timestamp(time_t *current_time_t);

void establish_mqtt_connection();
void mqtt_callback(char *topic, byte *payload, unsigned int length);

void send_line_notify(String message);

void set_button_listener(time_t *current_time_t, uint8_t pin);
void set_schedule_listener(time_t *current_time_t, int *seconds_timer, String latest_button_pressed_timestamp);

int get_seconds_timer(String hours);

float get_tank_height(int trigger_pin, int echo_pin);

// Function to set the latest timestamp in Redis
void set_latest_timestamp(time_t *current_time_t) {
    char timestamp[20];
    snprintf(timestamp, sizeof(timestamp), "%lld", static_cast<long long>(*current_time_t));

    bool set_status = redis_client.set("latest_release_timestamp", timestamp);
    if (!set_status) {
        Serial.print("[Cat] Failed to set time in Redis");
    }

    Serial.println("[Cat] Stored timestamp in Redis");
}

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

// mqtt callback function
void mqtt_callback(char *topic, byte *payload, unsigned int length) {
    Serial.print("[Cat] Message arrived [");
    Serial.print(topic);
    Serial.print("] ");
    String message;
    for (unsigned int idx = 0; idx < length; idx++) {
        message += (char)payload[idx];
    }

    if (message == "open_lid") {
        Serial.println("[Cat] Received 'open_lid' command, opening the lid...");
        servo.write(0);
        delay(768);

        servo.write(180);
        delay(64);

        send_line_notify("Received 'open_lid' command, opening the lid....");
    }
}

// Function to send a message to Line Notify
void send_line_notify(String message) {
    ESP.wdtDisable();

    line_notify_client.message = message;
    LineNotify.send(line_notify_client);
    yield();

    ESP.wdtEnable(WDTO_8S);
}


// Button listener function to open and close the lid
void set_button_listener(time_t *current_time_t, uint8_t pin) {
    int read_button_pin = digitalRead(pin);
    if (read_button_pin == LOW) {
        sensor.addField("button_pressed", 1);
        if (!influxdb_client.writePoint(sensor)) {
            Serial.print("[Cat] Failed to write data point on InfluxDB: ");
            Serial.println(influxdb_client.getLastErrorMessage());
            return;
        }

        Serial.println("[Cat] Button pressed, opening the lid...");
        set_latest_timestamp(current_time_t);

        servo.write(0);
        delay(768);

        servo.write(180);
        delay(64);

        send_line_notify("Button pressed, opening the lid....");
    }

    delay(64);
    return;
}

// Function to check the schedule and open the lid
void set_schedule_listener(time_t *current_time_t, int *seconds_timer, String latest_button_pressed_timestamp) {
    if (latest_button_pressed_timestamp.length() > 0) {
        time_t latest_button_pressed_time_t = atol(latest_button_pressed_timestamp.c_str());
        if (difftime(*current_time_t, latest_button_pressed_time_t) > *seconds_timer) {
            Serial.println("[Cat] Reached the feed time, opening the lid...");
            set_latest_timestamp(current_time_t);

            servo.write(0);
            delay(768);

            servo.write(180);
            delay(64);

            send_line_notify("Reached the feed time, opening the lid....");
        }
    }

    delay(64);
    return;
}

// Function to get the seconds timer from Redis
int get_seconds_timer(String hours) {
    int seconds_timer = 0;
    switch (hours.toInt()) {
        case 1:
            seconds_timer = 3600;
            break;
        case 2:
            seconds_timer = 7200;
            break;
        case 3:
            seconds_timer = 10800;
            break;
        case 4:
            seconds_timer = 14400;
            break;
        case 5:
            seconds_timer = 18000;
            break;
        case 6:
            seconds_timer = 21600;
            break;
        default:
            break;
    }

    return seconds_timer;
}

// Function to get the height of the cat from the distance
float get_height_cm(float distance_cm) {
    if (distance_cm >= 20 && distance_cm <= 50) {
        return 200 - (distance_cm - 20) * (200 - 170) / (50 - 20);

    } else {
        return -1;
    }
}

// Function to get the ultrasonic distance
float get_tank_height(int trigger_pin, int echo_pin) {
    digitalWrite(TRIGGER_PIN, LOW);
    delayMicroseconds(2);

    digitalWrite(TRIGGER_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIGGER_PIN, LOW);

    float height = 0.00;
    float duration = pulseIn(ECHO_PIN, HIGH);
    if (duration > 0) {
        height = (duration / 2) / 29.1;
        if (height > 0) {
            sensor.addField("height", height);
            if (!influxdb_client.writePoint(sensor)) {
                Serial.print("[Cat] Failed to write data point on InfluxDB: ");
                Serial.println(influxdb_client.getLastErrorMessage());
            }
        }
    } else {
        Serial.println("[Cat] Ultrasonic sensor timeout.");
    }

    return height;
}

// setup function
void setup() {
    ESP.wdtDisable();

    Serial.begin(SERIAL_BAUD);

    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    Serial.println("\n[Cat] Connecting to WiFi... ");
    while (WiFi.status() != WL_CONNECTED) { delay(interval); }
    Serial.println("[Cat] Connected to WiFi.\n");

    timeSync(TZ_INFO, "1.th.pool.ntp.org", "time.navy.mi.th");

    mqtt_client.setServer(MQTT_SERVER, MQTT_PORT);
    mqtt_client.setCallback(mqtt_callback);
    establish_mqtt_connection();

    if (!wifi_client.connect(REDIS_ADDR, REDIS_PORT)) {
        Serial.println("[Cat] Failed to connect to the Redis server!");
        return;
    }

    if (influxdb_client.validateConnection()) {
        Serial.printf("[Cat] Connected to the InfluxDB server! (%s)\n", influxdb_client.getServerUrl().c_str());

    } else {
        Serial.print("[Cat] Failed to connect to the InfluxDB server:");
        Serial.println(influxdb_client.getLastErrorMessage());
    }

    auto client_ret = redis_client.authenticate(REDIS_PASSWORD);
    if (client_ret == RedisSuccess) {
        Serial.printf("[Cat] Connected to the Redis server! (tcp://%s:%d)\n", REDIS_ADDR, REDIS_PORT);

    } else {
        Serial.printf("[Cat] Failed to authenticate to the Redis server! Errno: %d\n", (int)client_ret);
        return;
    }

    line_notify_client.setNetworkStatus(WiFi.status() == WL_CONNECTED);
    line_notify_client.setExternalClient(&wifi_client);
    line_notify_client.token = LINE_NOTIFY_API_TOKEN;

    pinMode(TRIGGER_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);

    servo.attach(SERVO_PIN);

    pinMode(BUTTON_PIN, INPUT_PULLUP);

    return;
}

// Main loop function
void loop() {
    unsigned long currentMillis = millis();
    if (currentMillis - previous_millis >= interval) {
        previous_millis = currentMillis;

        if (!wifi_client.connected() || redis_client.authenticate(REDIS_PASSWORD) != RedisSuccess) {
            Serial.println("[Cat] Redis connection lost, reconnecting...");
            if (!wifi_client.connect(REDIS_ADDR, REDIS_PORT)) {
                Serial.println("[Cat] Redis reconnection failed.");
                return;
            }

            if (redis_client.authenticate(REDIS_PASSWORD) != RedisSuccess) {
                Serial.println("[Cat] Redis re-authentication failed.");
                return;
            }

            delay(interval);
        }

        if (!mqtt_client.connected()) {
            establish_mqtt_connection();
        }
    }

    mqtt_client.loop();

    sensor.clearFields();

    String latest_release_timestamp = redis_client.get("latest_release_timestamp");
    String feed_schedule_hours = redis_client.get("feed_schedule_hours");
    int seconds_timer = get_seconds_timer(feed_schedule_hours);
    if (seconds_timer == 0) {
        Serial.println("[Cat] Invalid feed schedule hours.");
        delay(interval);
        return;
    }

    time_t current_time_t = time(nullptr);

    set_button_listener(&current_time_t, BUTTON_PIN);
    set_schedule_listener(&current_time_t, &seconds_timer, latest_release_timestamp);

    float height_cm = get_tank_height(TRIGGER_PIN, ECHO_PIN);
    if (height_cm > 0) {
        Serial.println("[Cat] Height: " + String(height_cm) + " cm");

        if (height_cm >= 20) {
            bool is_refill_line_notify_cooldown_exists = redis_client.exists("refill_line_notify_cooldown");
            if (!is_refill_line_notify_cooldown_exists) {
                Serial.println("[Cat] Food tank is almost empty, please refill the food.");

                redis_client.set("refill_line_notify_cooldown", "1");
                redis_client.expire("refill_line_notify_cooldown", 600);

                send_line_notify("Food tank is almost empty, please refill the food.");
            }
        }
    }

    delay(64);
    return;
}
