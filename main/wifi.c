#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_log.h"
#include "app_config.h"
#include "wifi.h"

static const char *TAG = "wifi";

#define WIFI_CONNECTED_BIT BIT0
static EventGroupHandle_t s_wifi_events;
static esp_timer_handle_t s_retry_timer;
static uint32_t s_backoff_ms = WIFI_BACKOFF_MIN_MS;

static void retry_timer_cb(void *arg)
{
    ESP_LOGI(TAG, "Reconnecting...");
    esp_wifi_connect();
}

/* Exponential backoff with jitter: wait, then double the wait for next time */
static void schedule_reconnect(void)
{
    uint32_t jitter = esp_random() % (s_backoff_ms / 5 + 1);   /* up to +20% */
    uint32_t delay_ms = s_backoff_ms + jitter;
    ESP_LOGW(TAG, "Disconnected, next attempt in %lu ms", (unsigned long)delay_ms);

    (void)esp_timer_stop(s_retry_timer);
    esp_timer_start_once(s_retry_timer, (uint64_t)delay_ms * 1000);

    s_backoff_ms *= 2;
    if (s_backoff_ms > WIFI_BACKOFF_MAX_MS) {
        s_backoff_ms = WIFI_BACKOFF_MAX_MS;
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT);
        schedule_reconnect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "Connected, IP " IPSTR, IP2STR(&event->ip_info.ip));
        s_backoff_ms = WIFI_BACKOFF_MIN_MS;   /* success: reset the backoff */
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
    }
}

esp_err_t wifi_connect(void)
{
    s_wifi_events = xEventGroupCreate();

    esp_timer_create_args_t timer_args = {
        .callback = retry_timer_cb,
        .name = "wifi_retry",
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_retry_timer));

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, NULL));

    wifi_config_t cfg = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .channel = WIFI_CHANNEL,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connecting to \"%s\"...", WIFI_SSID);
    xEventGroupWaitBits(s_wifi_events, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
    return ESP_OK;
}