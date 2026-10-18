#pragma once

/* ---------- Wi-Fi ---------- */
#define WIFI_SSID             "Wokwi-GUEST"
#define WIFI_PASSWORD         ""
#define WIFI_CHANNEL          6
/* Reconnect delay: 1 s, 2 s, 4 s ... up to 60 s */
#define WIFI_BACKOFF_MIN_MS   1000
#define WIFI_BACKOFF_MAX_MS   60000

/* ---------- MQTT ---------- */
#define MQTT_BROKER_URI       "mqtt://host.wokwi.internal"
#define MQTT_BASE_TOPIC       "iot-node-6g7l/node1"
#define MQTT_DATA_TOPIC       MQTT_BASE_TOPIC "/env"
#define MQTT_STATUS_TOPIC     MQTT_BASE_TOPIC "/status"
#define MQTT_OTA_TOPIC        MQTT_BASE_TOPIC "/ota"
#define MQTT_ACK_TIMEOUT_MS   5000
#define PUBLISH_PERIOD_MS     5000

/* ---------- Time ---------- */
#define NTP_SERVER            "pool.ntp.org"
#define TIME_HTTP_URL         "http://host.wokwi.internal:8000/"

/* ---------- Offline buffer ---------- */
#define BUFFER_MAX_RECORDS    2000   /* 16 bytes each = 32 KB, about 2.7 h at 5 s */
#define FLUSH_BATCH           50     /* records read from flash at once when re-sending */

/* ---------- I2C ---------- */
#define I2C_SDA_PIN           8
#define I2C_SCL_PIN           9