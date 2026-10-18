#pragma once
#include "esp_err.h"
#include "driver/i2c_master.h"

/* Find the sensor on an existing I2C bus, check its chip id, read its calibration */
esp_err_t bmp180_init(i2c_master_bus_handle_t bus);

/* Measure temperature (degrees C) and pressure (hPa) */
esp_err_t bmp180_read(float *temp_c, float *pressure_hpa);