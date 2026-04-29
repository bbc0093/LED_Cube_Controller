/**
 * @file tmp102.c
 * @brief Driver for the Texas Instruments TMP102 I2C temperature sensor.
 *
 * @author William Crow
 * @date 4/28/2026
 * @last_modified 2026-04-28
 */

/*
 * @copyright
 * MIT License
 * Copyright (c) 2026 William Crow
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 */

#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "driver/i2c_master.h"

#include "tmp102.h"

// Register addresses.
#define TMP102_REG_TEMPERATURE  0x00
#define TMP102_REG_CONFIG       0x01
#define TMP102_REG_TLOW         0x02
#define TMP102_REG_THIGH        0x03

// Config register MSByte bits.
#define TMP102_CFG_CR_SHIFT   5  // CR[1:0] at bits [6:5]
#define TMP102_CFG_EM_BIT     3  // Extended mode at bit 3
#define TMP102_CFG_SD_BIT     0  // Shutdown mode at bit 0

// Config register LSByte default (TM=1, POL=0, fault queue=4).
#define TMP102_CFG_LSB_DEFAULT  0xA0

// Temperature resolution: 0.0625 °C per LSB in both 12- and 13-bit modes.
#define TMP102_RESOLUTION_C  0.0625f

#define I2C_TIMEOUT_MS  100

static const char *TAG = "tmp102";

struct tmp102_driver {
    volatile bool           initialized;
    i2c_master_bus_handle_t bus;
    i2c_master_dev_handle_t dev;
    bool                    bus_owned;
    bool                    extended_mode;
};

static struct tmp102_driver drv = {0};

static esp_err_t write_register(uint8_t reg, uint8_t msb, uint8_t lsb)
{
    uint8_t buf[3] = {reg, msb, lsb};
    return i2c_master_transmit(drv.dev, buf, sizeof(buf), I2C_TIMEOUT_MS);
}

static esp_err_t read_register(uint8_t reg, uint8_t *out, size_t len)
{
    return i2c_master_transmit_receive(drv.dev, &reg, 1, out, len, I2C_TIMEOUT_MS);
}

esp_err_t tmp102_init(const tmp102_config_t *config)
{
    ESP_RETURN_ON_FALSE(!drv.initialized, ESP_ERR_INVALID_STATE, TAG, "already initialized");
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG, "config must not be NULL");

    esp_err_t ret = ESP_OK;

    if (config->bus_handle != NULL) {
        drv.bus = config->bus_handle;
    } else {
        i2c_master_bus_config_t bus_cfg = {
            .i2c_port            = config->i2c_port,
            .sda_io_num          = config->sda_pin,
            .scl_io_num          = config->scl_pin,
            .clk_source          = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt   = 7,
            .flags.enable_internal_pullup = true,
        };
        ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &drv.bus),
                            TAG, "failed to create I2C bus");
        drv.bus_owned = true;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = (uint16_t)config->address,
        .scl_speed_hz    = config->scl_speed_hz,
    };
    ESP_GOTO_ON_ERROR(i2c_master_bus_add_device(drv.bus, &dev_cfg, &drv.dev),
                      err_bus, TAG, "failed to add TMP102 device");

    // Build and write the configuration register.
    // MSByte: CR[1:0] at bits [6:5], EM at bit [3], SD=0 (continuous).
    uint8_t cfg_msb = (uint8_t)(((config->conv_rate & 0x03) << TMP102_CFG_CR_SHIFT) |
                                 (config->extended_mode ? (1 << TMP102_CFG_EM_BIT) : 0));
    ESP_GOTO_ON_ERROR(write_register(TMP102_REG_CONFIG, cfg_msb, TMP102_CFG_LSB_DEFAULT),
                      err_dev, TAG, "failed to write configuration register");

    drv.extended_mode = config->extended_mode;
    drv.initialized   = true;
    ESP_LOGI(TAG, "initialized at address 0x%02X (%d-bit mode)",
             config->address, config->extended_mode ? 13 : 12);
    return ESP_OK;

err_dev:
    i2c_master_bus_rm_device(drv.dev);
err_bus:
    if (drv.bus_owned) {
        i2c_del_master_bus(drv.bus);
    }
    memset(&drv, 0, sizeof(drv));
    return ret;
}

void tmp102_uninit(void)
{
    assert(drv.initialized && "tmp102_uninit called before tmp102_init");

    // Put the sensor into shutdown mode before removing the device.
    uint8_t cfg_msb = (uint8_t)(1 << TMP102_CFG_SD_BIT);
    write_register(TMP102_REG_CONFIG, cfg_msb, TMP102_CFG_LSB_DEFAULT);

    i2c_master_bus_rm_device(drv.dev);
    if (drv.bus_owned) {
        i2c_del_master_bus(drv.bus);
    }
    memset(&drv, 0, sizeof(drv));
    ESP_LOGI(TAG, "uninitialized");
}

esp_err_t tmp102_read(tmp102_data_t *data)
{
    ESP_RETURN_ON_FALSE(drv.initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");
    ESP_RETURN_ON_FALSE(data != NULL, ESP_ERR_INVALID_ARG, TAG, "data must not be NULL");

    uint8_t buf[2];
    ESP_RETURN_ON_ERROR(read_register(TMP102_REG_TEMPERATURE, buf, sizeof(buf)),
                        TAG, "temperature register read failed");

    // Combine bytes and arithmetic-shift right to extract the signed temperature count.
    // 12-bit: data is in bits [15:4], shift right 4.
    // 13-bit: data is in bits [15:3], shift right 3.
    int shift = drv.extended_mode ? 3 : 4;
    data->raw = (int16_t)((uint16_t)(buf[0] << 8) | buf[1]) >> shift;
    data->temperature_c = (float)data->raw * TMP102_RESOLUTION_C;
    return ESP_OK;
}

esp_err_t tmp102_read_temperature(float *temperature_c)
{
    ESP_RETURN_ON_FALSE(temperature_c != NULL, ESP_ERR_INVALID_ARG,
                        TAG, "temperature_c must not be NULL");
    tmp102_data_t data;
    ESP_RETURN_ON_ERROR(tmp102_read(&data), TAG, "read failed");
    *temperature_c = data.temperature_c;
    return ESP_OK;
}