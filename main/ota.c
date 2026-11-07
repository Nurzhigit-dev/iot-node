#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "app_config.h"
#include "ota.h"

static const char *TAG = "ota";

static char s_url[256];
static volatile bool s_in_progress = false;
static esp_timer_handle_t s_confirm_timer;

const char *ota_running_version(void)
{
    return esp_app_get_description()->version;
}

static bool running_app_pending_verify(void)
{
    esp_ota_img_states_t state;
    return esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK &&
           state == ESP_OTA_IMG_PENDING_VERIFY;
}

/* The bootloader only rolls back if the new firmware reboots unconfirmed.
   A firmware that hangs without network would never reboot, so we force it. */
static void confirm_timeout_cb(void *arg)
{
    ESP_LOGE(TAG, "New firmware did not reach MQTT within %d s, rolling back",
             OTA_CONFIRM_TIMEOUT_MS / 1000);
    esp_ota_mark_app_invalid_rollback_and_reboot();
}

void ota_init(void)
{
    if (!running_app_pending_verify()) {
        return;
    }
    ESP_LOGW(TAG, "Firmware not confirmed yet: rollback in %d s unless MQTT connects",
             OTA_CONFIRM_TIMEOUT_MS / 1000);
    esp_timer_create_args_t args = {
        .callback = confirm_timeout_cb,
        .name = "ota_confirm",
    };
    ESP_ERROR_CHECK(esp_timer_create(&args, &s_confirm_timer));
    ESP_ERROR_CHECK(esp_timer_start_once(s_confirm_timer, (uint64_t)OTA_CONFIRM_TIMEOUT_MS * 1000));
}

/* A freshly installed firmware starts in "pending verify" state.
   If it reboots before calling this, the bootloader rolls back to the old one. */
void ota_confirm_running_app(void)
{
    if (running_app_pending_verify()) {
        if (s_confirm_timer != NULL) {
            esp_timer_stop(s_confirm_timer);
        }
        esp_ota_mark_app_valid_cancel_rollback();
        ESP_LOGI(TAG, "New firmware confirmed, rollback cancelled");
    }
}

/* Refuse the version that is already running (an OTA message delivered twice, or
   retained on the broker, would otherwise reboot the device in a loop) and a version
   that was installed before and rolled back. */
static bool version_acceptable(const char *new_version)
{
    if (strcmp(new_version, ota_running_version()) == 0) {
        ESP_LOGW(TAG, "Version %s is already running, update skipped", new_version);
        return false;
    }
    const esp_partition_t *invalid = esp_ota_get_last_invalid_partition();
    esp_app_desc_t invalid_app;
    if (invalid != NULL && esp_ota_get_partition_description(invalid, &invalid_app) == ESP_OK &&
        strcmp(new_version, invalid_app.version) == 0) {
        ESP_LOGW(TAG, "Version %s was rolled back before, update skipped", new_version);
        return false;
    }
    return true;
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
    esp_https_ota_handle_t ota = NULL;
    esp_app_desc_t new_app;

    esp_err_t err = esp_https_ota_begin(&ota_cfg, &ota);
    if (err == ESP_OK) {
        err = esp_https_ota_get_img_desc(ota, &new_app);   /* reads just the image header */
    }
    if (err == ESP_OK && !version_acceptable(new_app.version)) {
        err = ESP_ERR_INVALID_VERSION;
    }
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Updating %s -> %s", ota_running_version(), new_app.version);
        int size = esp_https_ota_get_image_size(ota);
        int last_tenth = 0;
        while ((err = esp_https_ota_perform(ota)) == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            int tenth = size > 0 ? esp_https_ota_get_image_len_read(ota) * 10 / size : 0;
            if (tenth != last_tenth) {
                ESP_LOGI(TAG, "Downloaded %d%%", tenth * 10);
                last_tenth = tenth;
            }
        }
    }
    if (err == ESP_OK && !esp_https_ota_is_complete_data_received(ota)) {
        err = ESP_ERR_INVALID_SIZE;
    }
    if (err == ESP_OK) {
        err = esp_https_ota_finish(ota);   /* checks the image, selects it for the next boot */
        ota = NULL;                        /* finish frees the handle, also on failure */
    }
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Update written to the other slot, restarting");
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    }
    if (ota != NULL) {
        esp_https_ota_abort(ota);
    }
    if (err != ESP_ERR_INVALID_VERSION) {   /* a skipped version was already explained */
        ESP_LOGE(TAG, "Update failed: %s", esp_err_to_name(err));
    }
    s_in_progress = false;
    vTaskDelete(NULL);
}

void ota_start(const char *url, int len)
{
    if (len == 0) {
        return;                   /* empty message: someone cleared a retained OTA request */
    }
    if (s_in_progress) {
        ESP_LOGW(TAG, "Update already running");
        return;
    }
    if (len < 0 || len >= (int)sizeof(s_url)) {
        ESP_LOGE(TAG, "Bad URL length");
        return;
    }
    memcpy(s_url, url, len);      /* MQTT data is not 0-terminated */
    s_url[len] = '\0';
    s_in_progress = true;
    xTaskCreate(ota_task, "ota", 8192, NULL, 5, NULL);
}
