#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_system.h"
#include "esp_log.h"
#include "ota.h"

static const char *TAG = "ota";

static char s_url[256];
static volatile bool s_in_progress = false;

const char *ota_running_version(void)
{
    return esp_app_get_description()->version;
}

/* A freshly installed firmware starts in "pending verify" state.
   If it reboots before calling this, the bootloader rolls back to the old one. */
void ota_confirm_running_app(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(running, &state) == ESP_OK &&
        state == ESP_OTA_IMG_PENDING_VERIFY) {
        esp_ota_mark_app_valid_cancel_rollback();
        ESP_LOGI(TAG, "New firmware confirmed, rollback cancelled");
    }
}

/* Runs in its own task: downloading takes seconds and must not block MQTT */
static void ota_task(void *arg)
{
    ESP_LOGI(TAG, "Downloading %s", s_url);
    esp_http_client_config_t http_cfg = {
        .url = s_url,
        .timeout_ms = 10000,
    };
    esp_https_ota_config_t ota_cfg = {
        .http_config = &http_cfg,
    };
    esp_err_t err = esp_https_ota(&ota_cfg);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Update written to the other slot, restarting");
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    }
    ESP_LOGE(TAG, "Update failed: %s", esp_err_to_name(err));
    s_in_progress = false;
    vTaskDelete(NULL);
}

void ota_start(const char *url, int len)
{
    if (s_in_progress) {
        ESP_LOGW(TAG, "Update already running");
        return;
    }
    if (len <= 0 || len >= (int)sizeof(s_url)) {
        ESP_LOGE(TAG, "Bad URL length");
        return;
    }
    memcpy(s_url, url, len);      /* MQTT data is not 0-terminated */
    s_url[len] = '\0';
    s_in_progress = true;
    xTaskCreate(ota_task, "ota", 8192, NULL, 5, NULL);
}