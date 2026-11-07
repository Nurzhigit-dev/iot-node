#pragma once

/* ---------- Where the firmware runs ----------
   1 = Wokwi simulator
   0 = a real ESP32-S3 board: copy secrets.example.h to secrets.h (git-ignored)
       and put your Wi-Fi name, password and the server PC's IP address in it */
#define USE_SIMULATOR         1

/* ---------- Wi-Fi and server address ---------- */
#if USE_SIMULATOR
#define WIFI_SSID             "Wokwi-GUEST"
#define WIFI_PASSWORD         ""
#define WIFI_CHANNEL          6                       /* Wokwi's access point, skips the scan */
#define SERVER_HOST           "host.wokwi.internal"   /* the PC that runs the simulator */
#else
#if !__has_include("secrets.h")
#error "Real board build: copy main/secrets.example.h to main/secrets.h and fill it in"
#else
#include "secrets.h"                                  /* WIFI_SSID, WIFI_PASSWORD, SERVER_HOST */
#endif
#ifndef WIFI_CHANNEL
#define WIFI_CHANNEL          0                       /* channel unknown: scan all of them */
#endif
#endif
/* Reconnect delay: 1 s, 2 s, 4 s ... up to 60 s */
#define WIFI_BACKOFF_MIN_MS   1000
#define WIFI_BACKOFF_MAX_MS   60000

/* ---------- MQTT ---------- */
#define MQTT_BROKER_URI       "mqtt://" SERVER_HOST
#define MQTT_BASE_TOPIC       "iot-node-6g7l/node1"
#define MQTT_DATA_TOPIC       MQTT_BASE_TOPIC "/env"
#define MQTT_STATUS_TOPIC     MQTT_BASE_TOPIC "/status"
#define MQTT_OTA_TOPIC        MQTT_BASE_TOPIC "/ota"
#define MQTT_ACK_TIMEOUT_MS   5000
#define PUBLISH_PERIOD_MS     5000

/* ---------- Time ---------- */
#define NTP_SERVER            "pool.ntp.org"
#define TIME_HTTP_URL         "http://" SERVER_HOST ":8000/"

/* ---------- Offline buffer ---------- */
#define BUFFER_MAX_RECORDS    2000   /* 16 bytes each = 32 KB, about 2.7 h at 5 s */
#define FLUSH_BATCH           50     /* records read from flash at once when re-sending */
#define FLUSH_BUDGET_MS       (PUBLISH_PERIOD_MS / 2)   /* re-send at most this long per cycle, then measure */

/* ---------- OTA ---------- */
/* New firmware must reach the MQTT broker within this time, otherwise it is rolled back */
#define OTA_CONFIRM_TIMEOUT_MS 120000

/* ---------- I2C ---------- */
#define I2C_SDA_PIN           8
#define I2C_SCL_PIN           9
