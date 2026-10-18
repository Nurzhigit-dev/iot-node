#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "bmp180.h"

static const char *TAG = "bmp180";

#define I2C_FREQ_HZ         100000      /* 100 kHz, standard I2C speed */
#define I2C_TIMEOUT_MS      100

/* ---------- BMP180 registers (from the datasheet) ---------- */
#define BMP180_ADDR         0x77
#define BMP180_REG_CHIP_ID  0xD0
#define BMP180_CHIP_ID      0x55
#define BMP180_REG_CALIB    0xAA        /* 22 bytes: 0xAA..0xBF */
#define BMP180_REG_CTRL     0xF4
#define BMP180_REG_DATA     0xF6        /* 0xF6 MSB, 0xF7 LSB, 0xF8 XLSB */
#define BMP180_CMD_TEMP     0x2E
#define BMP180_CMD_PRES     0x34
#define BMP180_OSS          0           /* oversampling 0..3 */

/* A measurement takes up to 25.5 ms. pdMS_TO_TICKS rounds down to 10 ms ticks,
   so +1 tick guarantees we wait AT LEAST 30 ms. */
#define BMP180_WAIT_TICKS   (pdMS_TO_TICKS(30) + 1)

typedef struct {
    int16_t  ac1, ac2, ac3;
    uint16_t ac4, ac5, ac6;
    int16_t  b1, b2, mb, mc, md;
} bmp180_calib_t;

static i2c_master_dev_handle_t s_dev;
static bmp180_calib_t s_calib;

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

static esp_err_t read_regs(uint8_t reg, uint8_t *buf, size_t len)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, len, I2C_TIMEOUT_MS);
}

static esp_err_t write_reg(uint8_t reg, uint8_t value)
{
    uint8_t data[2] = { reg, value };
    return i2c_master_transmit(s_dev, data, sizeof(data), I2C_TIMEOUT_MS);
}

static esp_err_t read_calibration(void)
{
    uint8_t raw[22];
    esp_err_t err = read_regs(BMP180_REG_CALIB, raw, sizeof(raw));
    if (err != ESP_OK) {
        return err;
    }
    s_calib.ac1 = (int16_t)be16(&raw[0]);
    s_calib.ac2 = (int16_t)be16(&raw[2]);
    s_calib.ac3 = (int16_t)be16(&raw[4]);
    s_calib.ac4 = be16(&raw[6]);
    s_calib.ac5 = be16(&raw[8]);
    s_calib.ac6 = be16(&raw[10]);
    s_calib.b1  = (int16_t)be16(&raw[12]);
    s_calib.b2  = (int16_t)be16(&raw[14]);
    s_calib.mb  = (int16_t)be16(&raw[16]);
    s_calib.mc  = (int16_t)be16(&raw[18]);
    s_calib.md  = (int16_t)be16(&raw[20]);
    return ESP_OK;
}

static esp_err_t read_raw_temp(int32_t *ut)
{
    uint8_t buf[2];
    esp_err_t err = write_reg(BMP180_REG_CTRL, BMP180_CMD_TEMP);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(BMP180_WAIT_TICKS);
    err = read_regs(BMP180_REG_DATA, buf, sizeof(buf));
    if (err != ESP_OK) {
        return err;
    }
    *ut = (int32_t)be16(buf);
    return ESP_OK;
}

static esp_err_t read_raw_pressure(int32_t *up)
{
    uint8_t buf[3];
    esp_err_t err = write_reg(BMP180_REG_CTRL, BMP180_CMD_PRES + (BMP180_OSS << 6));
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(BMP180_WAIT_TICKS);
    err = read_regs(BMP180_REG_DATA, buf, sizeof(buf));
    if (err != ESP_OK) {
        return err;
    }
    *up = (int32_t)((((uint32_t)buf[0] << 16) | ((uint32_t)buf[1] << 8) | buf[2])
                    >> (8 - BMP180_OSS));
    return ESP_OK;
}

/* Raw numbers -> degrees C and hPa (formula from the BMP180 datasheet) */
static void compensate(int32_t ut, int32_t up, float *temp_c, float *pressure_hpa)
{
    int32_t x1, x2, x3, b3, b5, b6, p;
    uint32_t b4, b7;

    x1 = ((ut - (int32_t)s_calib.ac6) * (int32_t)s_calib.ac5) >> 15;
    x2 = ((int32_t)s_calib.mc << 11) / (x1 + s_calib.md);
    b5 = x1 + x2;
    *temp_c = ((b5 + 8) >> 4) / 10.0f;

    b6 = b5 - 4000;
    x1 = (s_calib.b2 * ((b6 * b6) >> 12)) >> 11;
    x2 = (s_calib.ac2 * b6) >> 11;
    x3 = x1 + x2;
    b3 = ((((int32_t)s_calib.ac1 * 4 + x3) << BMP180_OSS) + 2) >> 2;
    x1 = (s_calib.ac3 * b6) >> 13;
    x2 = (s_calib.b1 * ((b6 * b6) >> 12)) >> 16;
    x3 = ((x1 + x2) + 2) >> 2;
    b4 = ((uint32_t)s_calib.ac4 * (uint32_t)(x3 + 32768)) >> 15;
    b7 = ((uint32_t)up - (uint32_t)b3) * (uint32_t)(50000 >> BMP180_OSS);
    if (b7 < 0x80000000u) {
        p = (int32_t)((b7 << 1) / b4);
    } else {
        p = (int32_t)((b7 / b4) << 1);
    }
    x1 = (p >> 8) * (p >> 8);
    x1 = (x1 * 3038) >> 16;
    x2 = (-7357 * p) >> 16;
    p += (x1 + x2 + 3791) >> 4;
    *pressure_hpa = p / 100.0f;
}

esp_err_t bmp180_init(i2c_master_bus_handle_t bus)
{
    esp_err_t err = i2c_master_probe(bus, BMP180_ADDR, I2C_TIMEOUT_MS);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "No device at 0x%02X (%s). Check the wiring.",
                 BMP180_ADDR, esp_err_to_name(err));
        return err;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BMP180_ADDR,
        .scl_speed_hz = I2C_FREQ_HZ,
    };
    err = i2c_master_bus_add_device(bus, &dev_cfg, &s_dev);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t chip_id = 0;
    err = read_regs(BMP180_REG_CHIP_ID, &chip_id, 1);
    if (err != ESP_OK) {
        return err;
    }
    if (chip_id != BMP180_CHIP_ID) {
        ESP_LOGE(TAG, "Unexpected chip id 0x%02X (expected 0x%02X)", chip_id, BMP180_CHIP_ID);
        return ESP_ERR_NOT_FOUND;
    }

    err = read_calibration();
    if (err != ESP_OK) {
        return err;
    }
    ESP_LOGI(TAG, "BMP180 ready (chip id 0x%02X)", chip_id);
    return ESP_OK;
}

esp_err_t bmp180_read(float *temp_c, float *pressure_hpa)
{
    int32_t ut, up;
    esp_err_t err = read_raw_temp(&ut);
    if (err != ESP_OK) {
        return err;
    }
    err = read_raw_pressure(&up);
    if (err != ESP_OK) {
        return err;
    }
    compensate(ut, up, temp_c, pressure_hpa);
    return ESP_OK;
}