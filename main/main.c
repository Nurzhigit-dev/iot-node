#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif_sntp.h"
#include "driver/i2c_master.h"
#include "mqtt_client.h"
#include "app_config.h"
#include "bmp180.h"
#include "wifi.h"
#include "buffer.h"
#include "ota.h"
#include <strings.h>
#include "esp_http_client.h"

static const char *TAG = "main";

static esp_mqtt_client_handle_t s_mqtt;
static volatile bool s_mqtt_connected = false;

/* Set by the MQTT task when the broker acknowledges a QoS 1 message (PUBACK) */
#define ACK_BIT BIT0
static EventGroupHandle_t s_ack_events;
static volatile int s_acked_msg_id = -1;

/* ---------- MQTT ---------- */

static void mqtt_event_handler(void *arg, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED: {
        ESP_LOGI(TAG, "MQTT connected");
        s_mqtt_connected = true;
        esp_mqtt_client_subscribe(s_mqtt, MQTT_OTA_TOPIC, 1);
        char status[64];
        snprintf(status, sizeof(status), "online, fw %s", ota_running_version());
        esp_mqtt_client_publish(s_mqtt, MQTT_STATUS_TOPIC, status, 0, 1, 1);
        ota_confirm_running_app();   /* we got this far, so the firmware works */
        break;
    }
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT disconnected");
        s_mqtt_connected = false;
        break;
    case MQTT_EVENT_PUBLISHED:
        s_acked_msg_id = event->msg_id;
        xEventGroupSetBits(s_ack_events, ACK_BIT);
        break;
    case MQTT_EVENT_DATA:
        if ((size_t)event->topic_len == strlen(MQTT_OTA_TOPIC) &&
            strncmp(event->topic, MQTT_OTA_TOPIC, event->topic_len) == 0) {
            ota_start(event->data, event->data_len);
        }
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT error");
        break;
    default:
        break;
    }
}

/* Publish one reading with QoS 1 and wait for the broker's acknowledgment.
   Returns true only if the broker confirmed it. */
static bool publish_reading(const reading_t *r)
{
    char payload[128];
    int len = snprintf(payload, sizeof(payload),
                       "{\"ts\":%lld,\"temperature\":%.2f,\"pressure\":%.2f}",
                       (long long)r->ts_ms, r->temperature, r->pressure);

    xEventGroupClearBits(s_ack_events, ACK_BIT);
    int msg_id = esp_mqtt_client_publish(s_mqtt, MQTT_DATA_TOPIC, payload, len, 1, 0);
    if (msg_id < 0) {
        return false;
    }
    EventBits_t bits = xEventGroupWaitBits(s_ack_events, ACK_BIT, pdTRUE, pdFALSE,
                                           pdMS_TO_TICKS(MQTT_ACK_TIMEOUT_MS));
    return (bits & ACK_BIT) && s_acked_msg_id == msg_id;
}

/* Send buffered readings, oldest first. Returns true if the buffer is now empty. */
static bool flush_buffer(void)
{
    static reading_t batch[FLUSH_BATCH];

    while (s_mqtt_connected) {
        size_t n = buffer_read_front(batch, FLUSH_BATCH);
        if (n == 0) {
            return true;
        }
        size_t sent = 0;
        while (sent < n && publish_reading(&batch[sent])) {
            sent++;
        }
        if (sent > 0) {
            buffer_drop_front(sent);   /* delete only what the broker confirmed */
            ESP_LOGI(TAG, "Re-sent %u buffered readings, %u left",
                     (unsigned)sent, (unsigned)buffer_count());
        }
        if (sent < n) {
            return false;              /* connection trouble, try again next round */
        }
    }
    return false;
}

/* ---------- Time ---------- */

static bool time_is_valid(void)
{
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    return t.tm_year + 1900 >= 2024;   /* before sync the clock starts at 1970 */
}

static int64_t now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

/* ---------- Fallback: take the time from an HTTP server's "Date" header ---------- */

static char s_http_date[40];

static esp_err_t http_date_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_HEADER && strcasecmp(evt->header_key, "Date") == 0) {
        snprintf(s_http_date, sizeof(s_http_date), "%s", evt->header_value);
    }
    return ESP_OK;
}

/* Parses e.g. "Sun, 18 Oct 2026 09:07:00 GMT" */
static bool parse_http_date(const char *s, struct tm *out)
{
    static const char *months = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char mon[4] = {0};
    int day, year, h, m, sec;
    if (sscanf(s, "%*3s, %d %3s %d %d:%d:%d", &day, mon, &year, &h, &m, &sec) != 6) {
        return false;
    }
    const char *pos = strstr(months, mon);
    if (pos == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->tm_mday = day;
    out->tm_mon  = (int)(pos - months) / 3;
    out->tm_year = year - 1900;
    out->tm_hour = h;
    out->tm_min  = m;
    out->tm_sec  = sec;
    return true;
}

static bool time_from_http(void)
{
    s_http_date[0] = '\0';
    esp_http_client_config_t cfg = {
        .url = TIME_HTTP_URL,
        .method = HTTP_METHOD_HEAD,   /* we only need the headers, not the page */
        .event_handler = http_date_handler,
        .timeout_ms = 5000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_err_t err = esp_http_client_perform(client);
    esp_http_client_cleanup(client);

    struct tm tm;
    if (err != ESP_OK || s_http_date[0] == '\0' || !parse_http_date(s_http_date, &tm)) {
        ESP_LOGW(TAG, "HTTP time fallback failed");
        return false;
    }
    /* The header is in GMT and the ESP32's timezone is UTC by default, so mktime fits */
    struct timeval tv = { .tv_sec = mktime(&tm), .tv_usec = 0 };
    settimeofday(&tv, NULL);
    ESP_LOGI(TAG, "Time set from HTTP Date header: %s", s_http_date);
    return true;
}

static void time_sync_start(void)
{
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(NTP_SERVER);
    esp_netif_sntp_init(&cfg);         /* keeps re-syncing in the background */
    if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(15000)) == ESP_OK) {
        ESP_LOGI(TAG, "Time synchronized");
    } else {
        ESP_LOGW(TAG, "NTP did not answer, trying HTTP time");
        time_from_http();
    }
}

/* ---------- Startup ---------- */

static void nvs_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Firmware version %s", ota_running_version());
    nvs_init();
    ESP_ERROR_CHECK(buffer_init());
    s_ack_events = xEventGroupCreate();

    /* 1. Sensor */
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus));
    if (bmp180_init(bus) != ESP_OK) {
        ESP_LOGE(TAG, "Sensor not available, stopping");
        return;
    }

    /* 2. Network and time */
    wifi_connect();
    time_sync_start();

    /* 3. MQTT, with a Last Will: if we vanish, the broker publishes "offline" for us */
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER_URI,
        .session.last_will = {
            .topic = MQTT_STATUS_TOPIC,
            .msg = "offline",
            .qos = 1,
            .retain = 1,
        },
        .network.reconnect_timeout_ms = 5000,
    };
    s_mqtt = esp_mqtt_client_init(&mqtt_cfg);
    ESP_ERROR_CHECK(esp_mqtt_client_register_event(s_mqtt, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL));
    ESP_ERROR_CHECK(esp_mqtt_client_start(s_mqtt));

    /* 4. Main loop: measure every PUBLISH_PERIOD_MS, send or buffer */
    TickType_t last_wake = xTaskGetTickCount();
    while (1) {
        bool buffer_empty = (buffer_count() == 0) || flush_buffer();

        float t, p;
            if (!time_is_valid() && !time_from_http()) {
            ESP_LOGW(TAG, "No valid time yet, measurement skipped");
        } else if (bmp180_read(&t, &p) == ESP_OK) {
            reading_t r = { .ts_ms = now_ms(), .temperature = t, .pressure = p };
            if (s_mqtt_connected && buffer_empty && publish_reading(&r)) {
                ESP_LOGI(TAG, "Published: %.2f C, %.2f hPa", t, p);
            } else if (buffer_push(&r) == ESP_OK) {
                ESP_LOGW(TAG, "Not delivered, buffered (%u stored)", (unsigned)buffer_count());
            }
        } else {
            ESP_LOGW(TAG, "Sensor read failed");
        }

        xTaskDelayUntil(&last_wake, pdMS_TO_TICKS(PUBLISH_PERIOD_MS));
    }
}