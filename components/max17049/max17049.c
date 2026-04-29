/**
 * @file max17049.c
 * @brief Driver for the Analog Devices MAX17049G ModelGauge fuel gauge IC over I2C.
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

#include "max17049.h"

// Fixed 7-bit I2C address (cannot be changed on this device).
#define MAX17049_I2C_ADDR  0x36

// Register addresses (all registers are 16-bit, MSByte first).
#define REG_VCELL    0x02
#define REG_SOC      0x04
#define REG_MODE     0x06
#define REG_VERSION  0x08
#define REG_CONFIG   0x0C
#define REG_CRATE    0x16
#define REG_STATUS   0x1A

// VCELL: 156.25 µV per LSB for the dual-cell MAX17049.
// Full-scale: 0xFFFF × 156.25 µV ≈ 10.24 V (covers a 2S Li+ stack).
#define VCELL_UV_PER_LSB  156.25f

// SOC: full 16-bit value / 256 gives percent.
#define SOC_SCALE  (1.0f / 256.0f)

// CRATE: 0.208 %/hr per LSB, signed.
#define CRATE_PCT_HR_PER_LSB  0.208f

// CONFIG register layout (16-bit, MSByte = RCOMP):
//   Bit 7:  SLEEP
//   Bit 6:  ALSC  – alert on 1% SOC change
//   Bit 5:  ALRT  – alert flag (write 0 to clear)
//   Bits [4:0]: ATHD – empty alert threshold encoded as (32 – threshold%)
#define CFG_SLEEP_BIT   7
#define CFG_ALRT_BIT    5
#define CFG_ATHD_MASK   0x1Fu
#define RCOMP_DEFAULT   0x97u
#define ATHD_DEFAULT    0x1Cu  // 32 - 4 = 28 = 0x1C → 4% alert threshold

// STATUS register alert flags (write 0 to clear each bit).
#define STATUS_RI  (1u << 15)  // Reset indicator (set on POR)
#define STATUS_VH  (1u << 14)  // Voltage high alert
#define STATUS_VL  (1u << 13)  // Voltage low alert
#define STATUS_VR  (1u << 12)  // Voltage reset alert
#define STATUS_HD  (1u << 11)  // SOC empty alert
#define STATUS_SC  (1u << 10)  // SOC 1% change alert
#define STATUS_ALERT_FLAGS  (STATUS_RI | STATUS_VH | STATUS_VL | STATUS_VR | STATUS_HD | STATUS_SC)

// MODE register: write this to trigger a quick-start.
#define MODE_QUICKSTART  0x4000u

#define I2C_TIMEOUT_MS  100

static const char *TAG = "max17049";

struct max17049_driver {
    volatile bool           initialized;
    i2c_master_bus_handle_t bus;
    i2c_master_dev_handle_t dev;
    bool                    bus_owned;
};

static struct max17049_driver drv = {0};

static esp_err_t read_reg(uint8_t reg, uint16_t *out)
{
    uint8_t buf[2];
    ESP_RETURN_ON_ERROR(
        i2c_master_transmit_receive(drv.dev, &reg, 1, buf, sizeof(buf), I2C_TIMEOUT_MS),
        TAG, "read register 0x%02X failed", reg);
    *out = ((uint16_t)buf[0] << 8) | buf[1];
    return ESP_OK;
}

static esp_err_t write_reg(uint8_t reg, uint16_t value)
{
    uint8_t buf[3] = {reg, (uint8_t)(value >> 8), (uint8_t)(value & 0xFF)};
    return i2c_master_transmit(drv.dev, buf, sizeof(buf), I2C_TIMEOUT_MS);
}

esp_err_t max17049_init(const max17049_config_t *config)
{
    ESP_RETURN_ON_FALSE(!drv.initialized, ESP_ERR_INVALID_STATE, TAG, "already initialized");
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG, "config must not be NULL");
    ESP_RETURN_ON_FALSE(config->alert_threshold_pct >= 1 && config->alert_threshold_pct <= 32,
                        ESP_ERR_INVALID_ARG, TAG, "alert_threshold_pct must be 1..32");

    esp_err_t ret = ESP_OK;

    if (config->bus_handle != NULL) {
        drv.bus = config->bus_handle;
    } else {
        i2c_master_bus_config_t bus_cfg = {
            .i2c_port                     = config->i2c_port,
            .sda_io_num                   = config->sda_pin,
            .scl_io_num                   = config->scl_pin,
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
        .device_address  = MAX17049_I2C_ADDR,
        .scl_speed_hz    = config->scl_speed_hz,
    };
    ESP_GOTO_ON_ERROR(i2c_master_bus_add_device(drv.bus, &dev_cfg, &drv.dev),
                      err_bus, TAG, "failed to add MAX17049 device");

    // Clear the power-on reset indicator in STATUS so we start from a known state.
    uint16_t status;
    ESP_GOTO_ON_ERROR(read_reg(REG_STATUS, &status), err_dev, TAG, "STATUS read failed");
    if (status & STATUS_RI) {
        ESP_GOTO_ON_ERROR(write_reg(REG_STATUS, status & ~STATUS_ALERT_FLAGS),
                          err_dev, TAG, "STATUS clear failed");
    }

    // Write CONFIG: RCOMP | SLEEP=0 | ALSC=0 | ALRT=0 | ATHD.
    uint8_t rcomp = config->rcomp ? config->rcomp : RCOMP_DEFAULT;
    uint8_t athd  = (uint8_t)(32u - config->alert_threshold_pct) & CFG_ATHD_MASK;
    ESP_GOTO_ON_ERROR(write_reg(REG_CONFIG, ((uint16_t)rcomp << 8) | athd),
                      err_dev, TAG, "CONFIG write failed");

    drv.initialized = true;
    ESP_LOGI(TAG, "initialized (RCOMP=0x%02X, alert=%d%%)", rcomp, config->alert_threshold_pct);
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

void max17049_uninit(void)
{
    assert(drv.initialized && "max17049_uninit called before max17049_init");

    // Set SLEEP bit in CONFIG to put the device into low-power mode.
    uint16_t cfg;
    if (read_reg(REG_CONFIG, &cfg) == ESP_OK) {
        write_reg(REG_CONFIG, cfg | (1u << CFG_SLEEP_BIT));
    }

    i2c_master_bus_rm_device(drv.dev);
    if (drv.bus_owned) {
        i2c_del_master_bus(drv.bus);
    }
    memset(&drv, 0, sizeof(drv));
    ESP_LOGI(TAG, "uninitialized");
}

esp_err_t max17049_read(max17049_data_t *data)
{
    ESP_RETURN_ON_FALSE(drv.initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");
    ESP_RETURN_ON_FALSE(data != NULL, ESP_ERR_INVALID_ARG, TAG, "data must not be NULL");

    uint16_t vcell, soc, crate, cfg;
    ESP_RETURN_ON_ERROR(read_reg(REG_VCELL,  &vcell), TAG, "VCELL read failed");
    ESP_RETURN_ON_ERROR(read_reg(REG_SOC,    &soc),   TAG, "SOC read failed");
    ESP_RETURN_ON_ERROR(read_reg(REG_CRATE,  &crate), TAG, "CRATE read failed");
    ESP_RETURN_ON_ERROR(read_reg(REG_CONFIG, &cfg),   TAG, "CONFIG read failed");

    data->voltage_v    = (float)vcell * VCELL_UV_PER_LSB / 1e6f;
    data->soc_pct      = (float)soc   * SOC_SCALE;
    data->crate_pct_hr = (float)(int16_t)crate * CRATE_PCT_HR_PER_LSB;
    data->alert        = (cfg & (1u << CFG_ALRT_BIT)) != 0;
    return ESP_OK;
}

esp_err_t max17049_read_voltage(float *voltage_v)
{
    ESP_RETURN_ON_FALSE(voltage_v != NULL, ESP_ERR_INVALID_ARG, TAG, "voltage_v must not be NULL");
    uint16_t vcell;
    ESP_RETURN_ON_ERROR(read_reg(REG_VCELL, &vcell), TAG, "VCELL read failed");
    *voltage_v = (float)vcell * VCELL_UV_PER_LSB / 1e6f;
    return ESP_OK;
}

esp_err_t max17049_read_soc(float *soc_pct)
{
    ESP_RETURN_ON_FALSE(soc_pct != NULL, ESP_ERR_INVALID_ARG, TAG, "soc_pct must not be NULL");
    uint16_t soc;
    ESP_RETURN_ON_ERROR(read_reg(REG_SOC, &soc), TAG, "SOC read failed");
    *soc_pct = (float)soc * SOC_SCALE;
    return ESP_OK;
}

esp_err_t max17049_clear_alert(void)
{
    ESP_RETURN_ON_FALSE(drv.initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");

    // Clear ALRT in CONFIG.
    uint16_t cfg;
    ESP_RETURN_ON_ERROR(read_reg(REG_CONFIG, &cfg), TAG, "CONFIG read failed");
    ESP_RETURN_ON_ERROR(write_reg(REG_CONFIG, cfg & ~(1u << CFG_ALRT_BIT)),
                        TAG, "CONFIG write failed");

    // Clear all alert flags in STATUS (write 0 to each flag bit; EnVR at bit 9 is preserved).
    uint16_t status;
    ESP_RETURN_ON_ERROR(read_reg(REG_STATUS, &status), TAG, "STATUS read failed");
    ESP_RETURN_ON_ERROR(write_reg(REG_STATUS, status & ~STATUS_ALERT_FLAGS),
                        TAG, "STATUS write failed");
    return ESP_OK;
}

esp_err_t max17049_quick_start(void)
{
    ESP_RETURN_ON_FALSE(drv.initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");
    ESP_RETURN_ON_ERROR(write_reg(REG_MODE, MODE_QUICKSTART), TAG, "MODE write failed");
    ESP_LOGI(TAG, "quick-start triggered");
    return ESP_OK;
}