#include <stdio.h>
#include <sys/stat.h>
#include "esp_spiffs.h"
#include "esp_log.h"
#include "app_config.h"
#include "buffer.h"

static const char *TAG = "buffer";

#define BASE_PATH  "/spiffs"
#define BUF_FILE   BASE_PATH "/buffer.bin"
#define TMP_FILE   BASE_PATH "/buffer.tmp"

esp_err_t buffer_init(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = BASE_PATH,
        .partition_label = "storage",
        .max_files = 3,
        .format_if_mount_failed = true,   /* first boot: format the empty partition */
    };
    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Mounting storage failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "Ready, %u readings waiting from before", (unsigned)buffer_count());
    return ESP_OK;
}

size_t buffer_count(void)
{
    struct stat st;
    if (stat(BUF_FILE, &st) != 0) {
        return 0;                          /* no file = empty buffer */
    }
    return (size_t)st.st_size / sizeof(reading_t);
}

esp_err_t buffer_push(const reading_t *r)
{
    if (buffer_count() >= BUFFER_MAX_RECORDS) {
        ESP_LOGW(TAG, "Buffer full, reading dropped");
        return ESP_ERR_NO_MEM;
    }
    FILE *f = fopen(BUF_FILE, "ab");      /* append, binary */
    if (f == NULL) {
        return ESP_FAIL;
    }
    size_t written = fwrite(r, sizeof(*r), 1, f);
    fclose(f);
    return written == 1 ? ESP_OK : ESP_FAIL;
}

size_t buffer_read_front(reading_t *out, size_t max)
{
    FILE *f = fopen(BUF_FILE, "rb");
    if (f == NULL) {
        return 0;
    }
    size_t n = fread(out, sizeof(reading_t), max, f);
    fclose(f);
    return n;
}

esp_err_t buffer_drop_front(size_t n)
{
    if (n >= buffer_count()) {
        remove(BUF_FILE);
        return ESP_OK;
    }
    /* Copy everything after the first n records into a new file, then swap */
    FILE *src = fopen(BUF_FILE, "rb");
    FILE *dst = fopen(TMP_FILE, "wb");
    if (src == NULL || dst == NULL) {
        if (src) fclose(src);
        if (dst) fclose(dst);
        return ESP_FAIL;
    }
    fseek(src, (long)(n * sizeof(reading_t)), SEEK_SET);
    reading_t r;
    while (fread(&r, sizeof(r), 1, src) == 1) {
        fwrite(&r, sizeof(r), 1, dst);
    }
    fclose(src);
    fclose(dst);
    remove(BUF_FILE);
    return rename(TMP_FILE, BUF_FILE) == 0 ? ESP_OK : ESP_FAIL;
}