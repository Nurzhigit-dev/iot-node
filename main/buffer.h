#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

/* One measurement, exactly as stored in flash (16 bytes) */
typedef struct {
    int64_t ts_ms;        /* Unix time in milliseconds */
    float   temperature;  /* degrees C */
    float   pressure;     /* hPa */
} reading_t;

esp_err_t buffer_init(void);
size_t    buffer_count(void);
esp_err_t buffer_push(const reading_t *r);
size_t    buffer_read_front(reading_t *out, size_t max);   /* oldest first */
esp_err_t buffer_drop_front(size_t n);                    /* delete the n oldest */