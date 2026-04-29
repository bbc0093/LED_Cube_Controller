/**
 * @file icm42688p.c
 * @brief Driver for the TDK ICM-42688-P 6-axis IMU over I2C.
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

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "icm42688p.h"
#include "icm42688p_registers.h"

// Gyro low-noise startup time per datasheet (45 ms); 50 ms adds margin.
#define ICM42688P_STARTUP_DELAY_MS  50

#define I2C_TIMEOUT_MS  100

static const char *TAG = "icm42688p";

struct icm42688p_driver {
    volatile bool           initialized;
    i2c_master_bus_handle_t bus;
    i2c_master_dev_handle_t dev;
    bool                    bus_owned;
    enum icm42688p_accel_fs accel_fs;
    enum icm42688p_gyro_fs  gyro_fs;
};

static struct icm42688p_driver drv = {0};

static const float accel_fs_to_sens[] = {
    [ICM42688P_ACCEL_FS_16G] = ICM42688P_ACCEL_SENS_16G,
    [ICM42688P_ACCEL_FS_8G]  = ICM42688P_ACCEL_SENS_8G,
    [ICM42688P_ACCEL_FS_4G]  = ICM42688P_ACCEL_SENS_4G,
    [ICM42688P_ACCEL_FS_2G]  = ICM42688P_ACCEL_SENS_2G,
};

static const float gyro_fs_to_sens[] = {
    [ICM42688P_GYRO_FS_2000DPS]   = ICM42688P_GYRO_SENS_2000DPS,
    [ICM42688P_GYRO_FS_1000DPS]   = ICM42688P_GYRO_SENS_1000DPS,
    [ICM42688P_GYRO_FS_500DPS]    = ICM42688P_GYRO_SENS_500DPS,
    [ICM42688P_GYRO_FS_250DPS]    = ICM42688P_GYRO_SENS_250DPS,
    [ICM42688P_GYRO_FS_125DPS]    = ICM42688P_GYRO_SENS_125DPS,
    [ICM42688P_GYRO_FS_62_5DPS]   = ICM42688P_GYRO_SENS_62_5DPS,
    [ICM42688P_GYRO_FS_31_25DPS]  = ICM42688P_GYRO_SENS_31_25DPS,
    [ICM42688P_GYRO_FS_15_625DPS] = ICM42688P_GYRO_SENS_15_625DPS,
};

static esp_err_t write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(drv.dev, buf, sizeof(buf), I2C_TIMEOUT_MS);
}

static esp_err_t read_reg(uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(drv.dev, &reg, 1, val, 1, I2C_TIMEOUT_MS);
}

// Burst-read ICM42688P_BURST_DATA_LEN bytes starting at start_reg.
static esp_err_t read_burst(uint8_t start_reg, uint8_t *buf)
{
    return i2c_master_transmit_receive(drv.dev, &start_reg, 1,
                                       buf, ICM42688P_BURST_DATA_LEN,
                                       I2C_TIMEOUT_MS);
}

esp_err_t icm42688p_init(const icm42688p_config_t *config)
{
    ESP_RETURN_ON_FALSE(!drv.initialized, ESP_ERR_INVALID_STATE, TAG, "already initialized");
    ESP_RETURN_ON_FALSE(config != NULL,   ESP_ERR_INVALID_ARG,   TAG, "config must not be NULL");

    esp_err_t ret = ESP_OK;

    if (config->bus_handle != NULL) {
        drv.bus = config->bus_handle;
    } else {
        i2c_master_bus_config_t bus_cfg = {
            .i2c_port                     = I2C_NUM_0,
            .clk_source                   = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt            = 7,
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
                      err_bus, TAG, "failed to add ICM-42688-P device");

    // Soft reset; registers are accessible after 1 ms.
    ESP_GOTO_ON_ERROR(
        write_reg(ICM42688P_REG_DEVICE_CONFIG, ICM42688P_SOFT_RESET),
        err_dev, TAG, "soft reset failed");
    vTaskDelay(pdMS_TO_TICKS(1));

    uint8_t who_am_i;
    ESP_GOTO_ON_ERROR(
        read_reg(ICM42688P_REG_WHO_AM_I, &who_am_i),
        err_dev, TAG, "WHO_AM_I read failed");
    ESP_GOTO_ON_FALSE(who_am_i == ICM42688P_WHO_AM_I_VAL, ESP_ERR_NOT_FOUND,
                      err_dev, TAG, "WHO_AM_I mismatch: expected 0x%02X, got 0x%02X",
                      ICM42688P_WHO_AM_I_VAL, who_am_i);

    // Configure gyro: full-scale and ODR.
    ESP_GOTO_ON_ERROR(
        write_reg(ICM42688P_REG_GYRO_CONFIG0,
                  (uint8_t)((config->gyro_fs << 5) | config->odr)),
        err_dev, TAG, "GYRO_CONFIG0 write failed");

    // Configure accel: full-scale and ODR.
    ESP_GOTO_ON_ERROR(
        write_reg(ICM42688P_REG_ACCEL_CONFIG0,
                  (uint8_t)((config->accel_fs << 5) | config->odr)),
        err_dev, TAG, "ACCEL_CONFIG0 write failed");

    // Enable accel and gyro in low-noise mode.
    ESP_GOTO_ON_ERROR(
        write_reg(ICM42688P_REG_PWR_MGMT0,
                  ICM42688P_ACCEL_MODE_LN | ICM42688P_GYRO_MODE_LN),
        err_dev, TAG, "PWR_MGMT0 write failed");

    // Gyro low-noise startup requires ~45 ms; wait with margin.
    vTaskDelay(pdMS_TO_TICKS(ICM42688P_STARTUP_DELAY_MS));

    // Configure INT1: push-pull, active-high, pulsed (clears on status read or burst read).
    ESP_GOTO_ON_ERROR(
        write_reg(ICM42688P_REG_INT_CONFIG,
                  ICM42688P_INT1_DRIVE_PUSH_PULL | ICM42688P_INT1_ACTIVE_HIGH),
        err_dev, TAG, "INT_CONFIG write failed");

    // Route UI data-ready to INT1.
    ESP_GOTO_ON_ERROR(
        write_reg(ICM42688P_REG_INT_SOURCE0, ICM42688P_INT_SOURCE0_DRDY_EN),
        err_dev, TAG, "INT_SOURCE0 write failed");

    drv.accel_fs    = config->accel_fs;
    drv.gyro_fs     = config->gyro_fs;
    drv.initialized = true;

    ESP_LOGI(TAG, "ICM-42688-P initialized at I2C address 0x%02X", config->address);
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

void icm42688p_uninit(void)
{
    assert(drv.initialized && "icm42688p_uninit called before icm42688p_init");

    // Power down accel and gyro.
    write_reg(ICM42688P_REG_PWR_MGMT0,
              ICM42688P_ACCEL_MODE_OFF | ICM42688P_GYRO_MODE_OFF);

    i2c_master_bus_rm_device(drv.dev);
    if (drv.bus_owned) {
        i2c_del_master_bus(drv.bus);
    }
    memset(&drv, 0, sizeof(drv));
    ESP_LOGI(TAG, "ICM-42688-P uninitialized");
}

esp_err_t icm42688p_read_raw(icm42688p_raw_data_t *data)
{
    ESP_RETURN_ON_FALSE(drv.initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");
    ESP_RETURN_ON_FALSE(data != NULL,    ESP_ERR_INVALID_ARG,   TAG, "data must not be NULL");

    uint8_t buf[ICM42688P_BURST_DATA_LEN];
    ESP_RETURN_ON_ERROR(
        read_burst(ICM42688P_REG_TEMP_DATA1, buf),
        TAG, "burst read failed");

    // Registers are read MSB-first: TEMP, ACCEL XYZ, GYRO XYZ.
    data->temp    = (int16_t)((buf[0]  << 8) | buf[1]);
    data->accel_x = (int16_t)((buf[2]  << 8) | buf[3]);
    data->accel_y = (int16_t)((buf[4]  << 8) | buf[5]);
    data->accel_z = (int16_t)((buf[6]  << 8) | buf[7]);
    data->gyro_x  = (int16_t)((buf[8]  << 8) | buf[9]);
    data->gyro_y  = (int16_t)((buf[10] << 8) | buf[11]);
    data->gyro_z  = (int16_t)((buf[12] << 8) | buf[13]);

    return ESP_OK;
}

esp_err_t icm42688p_read(icm42688p_data_t *data)
{
    ESP_RETURN_ON_FALSE(data != NULL, ESP_ERR_INVALID_ARG, TAG, "data must not be NULL");

    icm42688p_raw_data_t raw;
    ESP_RETURN_ON_ERROR(icm42688p_read_raw(&raw), TAG, "raw read failed");

    const float accel_scale = 1.0f / accel_fs_to_sens[drv.accel_fs] * 9.80665f;
    const float gyro_scale  = 1.0f / gyro_fs_to_sens[drv.gyro_fs] * ICM42688P_DEG_TO_RAD;

    data->accel_x = (float)raw.accel_x * accel_scale;
    data->accel_y = (float)raw.accel_y * accel_scale;
    data->accel_z = (float)raw.accel_z * accel_scale;
    data->gyro_x  = (float)raw.gyro_x  * gyro_scale;
    data->gyro_y  = (float)raw.gyro_y  * gyro_scale;
    data->gyro_z  = (float)raw.gyro_z  * gyro_scale;
    data->temp    = (float)raw.temp / ICM42688P_TEMP_SENS + ICM42688P_TEMP_OFFSET;

    return ESP_OK;
}