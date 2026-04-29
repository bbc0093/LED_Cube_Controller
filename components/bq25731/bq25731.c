/**
 * @file bq25731.c
 * @brief Driver for the Texas Instruments BQ25731 multi-phase buck battery charger over I2C.
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

#include "bq25731.h"
#include "bq25731_registers.h"

// ADC poll: check every 1 ms, give up after 50 attempts (50 ms).
#define ADC_POLL_INTERVAL_MS   1
#define ADC_POLL_MAX_ATTEMPTS  50

#define I2C_TIMEOUT_MS  100

static const char *TAG = "bq25731";

struct bq25731_driver {
    volatile bool           initialized;
    i2c_master_bus_handle_t bus;
    i2c_master_dev_handle_t dev;
    bool                    bus_owned;
    uint8_t                 rsr_mohm;   // Battery current sense resistor (mΩ).
    uint8_t                 rac_mohm;   // Input current sense resistor (mΩ).
};

static struct bq25731_driver drv = {0};

// All BQ25731 registers are 16-bit, LSByte first (little-endian).
static esp_err_t read_reg(uint8_t reg, uint16_t *out)
{
    uint8_t buf[2];
    ESP_RETURN_ON_ERROR(
        i2c_master_transmit_receive(drv.dev, &reg, 1, buf, sizeof(buf), I2C_TIMEOUT_MS),
        TAG, "read register 0x%02X failed", reg);
    *out = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
    return ESP_OK;
}

static esp_err_t write_reg(uint8_t reg, uint16_t value)
{
    uint8_t buf[3] = {reg, (uint8_t)(value & 0xFF), (uint8_t)(value >> 8)};
    return i2c_master_transmit(drv.dev, buf, sizeof(buf), I2C_TIMEOUT_MS);
}

// Read-modify-write: clear bits in mask, then OR in value.
static esp_err_t rmw_reg(uint8_t reg, uint16_t mask, uint16_t value)
{
    uint16_t current;
    ESP_RETURN_ON_ERROR(read_reg(reg, &current), TAG, "RMW read 0x%02X failed", reg);
    return write_reg(reg, (current & ~mask) | (value & mask));
}

static esp_err_t set_chrg_inhibit(bool inhibit)
{
    return rmw_reg(BQ25731_REG_CHARGE_OPTION0,
                   BQ25731_OPT0_CHRG_INHIBIT,
                   inhibit ? BQ25731_OPT0_CHRG_INHIBIT : 0u);
}

esp_err_t bq25731_init(const bq25731_config_t *config)
{
    ESP_RETURN_ON_FALSE(!drv.initialized, ESP_ERR_INVALID_STATE, TAG, "already initialized");
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG, "config must not be NULL");

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
        .device_address  = BQ25731_I2C_ADDR,
        .scl_speed_hz    = config->scl_speed_hz,
    };
    ESP_GOTO_ON_ERROR(i2c_master_bus_add_device(drv.bus, &dev_cfg, &drv.dev),
                      err_bus, TAG, "failed to add BQ25731 device");

    // Verify manufacturer ID and device ID before touching any charge registers.
    // Reading from 0x2E (ManufacturerID) as a 16-bit register also returns 0x2F (DeviceID)
    // in the high byte.
    uint16_t id_reg;
    ESP_GOTO_ON_ERROR(read_reg(BQ25731_REG_MANUFACTURER_ID, &id_reg),
                      err_dev, TAG, "failed to read ID registers");
    uint8_t mfr_id = (uint8_t)(id_reg & 0xFF);
    uint8_t dev_id = (uint8_t)(id_reg >> 8);
    ESP_GOTO_ON_FALSE(mfr_id == BQ25731_MANUFACTURER_ID, ESP_ERR_NOT_FOUND,
                      err_dev, TAG, "unexpected manufacturer ID 0x%02X (expected 0x%02X)",
                      mfr_id, BQ25731_MANUFACTURER_ID);
    ESP_GOTO_ON_FALSE(dev_id == BQ25731_DEVICE_ID, ESP_ERR_NOT_FOUND,
                      err_dev, TAG, "unexpected device ID 0x%02X (expected 0x%02X)",
                      dev_id, BQ25731_DEVICE_ID);
    ESP_LOGI(TAG, "BQ25731 identified (mfr=0x%02X dev=0x%02X)", mfr_id, dev_id);

    // Disable the watchdog timer (default is 175 s).  All other ChargeOption0 defaults are kept.
    ESP_GOTO_ON_ERROR(
        rmw_reg(BQ25731_REG_CHARGE_OPTION0,
                BQ25731_OPT0_WDTMR_MASK,
                BQ25731_OPT0_WDTMR_DISABLE),
        err_dev, TAG, "ChargeOption0 write failed");

    // Configure sense resistor selection to match the board hardware.
    // RSNS_RAC: 1 = 5 mΩ, 0 = 10 mΩ.  RSNS_RSR: 1 = 5 mΩ, 0 = 10 mΩ.
    uint16_t rsns_val = 0;
    if (config->rsr_mohm == 5) rsns_val |= BQ25731_OPT1_RSNS_RSR;
    if (config->rac_mohm == 5) rsns_val |= BQ25731_OPT1_RSNS_RAC;
    ESP_GOTO_ON_ERROR(
        rmw_reg(BQ25731_REG_CHARGE_OPTION1,
                BQ25731_OPT1_RSNS_RAC | BQ25731_OPT1_RSNS_RSR,
                rsns_val),
        err_dev, TAG, "ChargeOption1 write failed");

    // ChargeCurrent: 7-bit code in bits[12:6].  LSB = 128 mA at RSR = 5 mΩ.
    uint16_t ichg_lsb_ma = BQ25731_CHARGE_CURRENT_LSB_MA * BQ25731_CHARGE_CURRENT_RSR_REF / config->rsr_mohm;
    uint16_t ichg_reg    = (uint16_t)((config->charge_current_ma / ichg_lsb_ma) << 6) & BQ25731_CHARGE_CURRENT_MASK;
    ESP_GOTO_ON_ERROR(
        write_reg(BQ25731_REG_CHARGE_CURRENT, ichg_reg),
        err_dev, TAG, "ChargeCurrent write failed");

    // ChargeVoltage: 12-bit value in bits[14:3].  LSB = 8 mV.
    uint16_t volt_reg = config->charge_voltage_mv & BQ25731_CHARGE_VOLTAGE_MASK;
    ESP_GOTO_ON_ERROR(
        write_reg(BQ25731_REG_CHARGE_VOLTAGE, volt_reg),
        err_dev, TAG, "ChargeVoltage write failed");

    // IIN_HOST: 7-bit code in bits[14:8].  LSB = 100 mA at RAC = 5 mΩ.
    uint16_t iin_lsb_ma = BQ25731_IIN_HOST_LSB_MA * BQ25731_IIN_HOST_RAC_REF / config->rac_mohm;
    uint16_t iin_reg    = (uint16_t)((config->input_current_ma / iin_lsb_ma) << 8) & BQ25731_IIN_HOST_MASK;
    ESP_GOTO_ON_ERROR(
        write_reg(BQ25731_REG_IIN_HOST, iin_reg),
        err_dev, TAG, "IINHost write failed");

    drv.rsr_mohm    = config->rsr_mohm;
    drv.rac_mohm    = config->rac_mohm;
    drv.initialized = true;
    ESP_LOGI(TAG, "initialized: %u mV / %u mA, input limit %u mA (RSR=%u mΩ, RAC=%u mΩ)",
             config->charge_voltage_mv, config->charge_current_ma,
             config->input_current_ma, config->rsr_mohm, config->rac_mohm);
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

void bq25731_uninit(void)
{
    assert(drv.initialized && "bq25731_uninit called before bq25731_init");

    set_chrg_inhibit(true);

    i2c_master_bus_rm_device(drv.dev);
    if (drv.bus_owned) {
        i2c_del_master_bus(drv.bus);
    }
    memset(&drv, 0, sizeof(drv));
    ESP_LOGI(TAG, "uninitialized");
}

esp_err_t bq25731_read_status(bq25731_status_t *status)
{
    ESP_RETURN_ON_FALSE(drv.initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");
    ESP_RETURN_ON_FALSE(status != NULL, ESP_ERR_INVALID_ARG, TAG, "status must not be NULL");

    uint16_t reg;
    ESP_RETURN_ON_ERROR(read_reg(BQ25731_REG_CHARGER_STATUS, &reg),
                        TAG, "ChargerStatus read failed");

    status->ac_present  = (reg & BQ25731_STATUS_AC_STAT)    != 0;
    status->in_fchrg    = (reg & BQ25731_STATUS_IN_FCHRG)   != 0;
    status->in_vindpm   = (reg & BQ25731_STATUS_IN_VINDPM)  != 0;
    status->in_idpm     = (reg & BQ25731_STATUS_IN_IDPM)    != 0;
    status->ico_done    = (reg & BQ25731_STATUS_ICO_DONE)   != 0;
    status->fault       = (reg & BQ25731_STATUS_FAULT_MASK) != 0;
    return ESP_OK;
}

esp_err_t bq25731_read_adc(bq25731_adc_t *adc)
{
    ESP_RETURN_ON_FALSE(drv.initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");
    ESP_RETURN_ON_FALSE(adc != NULL, ESP_ERR_INVALID_ARG, TAG, "adc must not be NULL");

    // Trigger a one-shot conversion of all channels, preserving the 3.06 V
    // full-scale range for PSYS/CMPIN (ADC_FULLSCALE default = 1).
    ESP_RETURN_ON_ERROR(write_reg(BQ25731_REG_ADC_OPTION, BQ25731_ADC_ONESHOT_ALL),
                        TAG, "ADC start failed");

    // Poll until ADC_START self-clears (conversion complete).
    for (int i = 0; i < ADC_POLL_MAX_ATTEMPTS; i++) {
        vTaskDelay(pdMS_TO_TICKS(ADC_POLL_INTERVAL_MS));
        uint16_t opt;
        ESP_RETURN_ON_ERROR(read_reg(BQ25731_REG_ADC_OPTION, &opt),
                            TAG, "ADC poll failed");
        if (!(opt & BQ25731_ADC_OPT_START)) {
            break;
        }
        if (i == ADC_POLL_MAX_ATTEMPTS - 1) {
            ESP_LOGE(TAG, "ADC conversion timed out");
            return ESP_ERR_TIMEOUT;
        }
    }

    // Read all four ADC result register pairs.
    // Register layout (little-endian, low address = low byte):
    //   0x26/0x27: low byte = PSYS,  high byte = VBUS
    //   0x28/0x29: low byte = IDCHG[6:0], high byte = ICHG[6:0]
    //   0x2A/0x2B: low byte = CMPIN, high byte = IIN
    //   0x2C/0x2D: low byte = VBAT,  high byte = VSYS
    uint16_t vbus_psys, ibat, iin_cmpin, vsys_vbat;
    ESP_RETURN_ON_ERROR(read_reg(BQ25731_REG_ADC_VBUS_PSYS, &vbus_psys),
                        TAG, "ADC VBUS/PSYS read failed");
    ESP_RETURN_ON_ERROR(read_reg(BQ25731_REG_ADC_IBAT, &ibat),
                        TAG, "ADC IBAT read failed");
    ESP_RETURN_ON_ERROR(read_reg(BQ25731_REG_ADC_IIN_CMPIN, &iin_cmpin),
                        TAG, "ADC IIN/CMPIN read failed");
    ESP_RETURN_ON_ERROR(read_reg(BQ25731_REG_ADC_VSYS_VBAT, &vsys_vbat),
                        TAG, "ADC VSYS/VBAT read failed");

    uint8_t vbus_raw  = (uint8_t)(vbus_psys >> 8);
    uint8_t psys_raw  = (uint8_t)(vbus_psys & 0xFF);
    uint8_t ichg_raw  = (uint8_t)(ibat >> 8) & 0x7Fu;
    uint8_t idchg_raw = (uint8_t)(ibat & 0xFF) & 0x7Fu;
    uint8_t iin_raw   = (uint8_t)(iin_cmpin >> 8);
    uint8_t vbat_raw  = (uint8_t)(vsys_vbat & 0xFF);
    uint8_t vsys_raw  = (uint8_t)(vsys_vbat >> 8);

    // Current LSBs scale with sense resistor value relative to the 5 mΩ reference.
    uint16_t ichg_lsb_ma  = BQ25731_ADC_ICHG_LSB_MA  * BQ25731_ADC_IBAT_RSR_REF / drv.rsr_mohm;
    uint16_t idchg_lsb_ma = BQ25731_ADC_IDCHG_LSB_MA * BQ25731_ADC_IBAT_RSR_REF / drv.rsr_mohm;
    uint16_t iin_lsb_ma   = BQ25731_ADC_IIN_LSB_MA   * BQ25731_ADC_IIN_RAC_REF  / drv.rac_mohm;

    adc->vbus_v  = (float)vbus_raw  * BQ25731_ADC_VBUS_LSB_MV / 1000.0f;
    adc->psys_v  = (float)psys_raw  * BQ25731_ADC_PSYS_LSB_MV / 1000.0f;
    adc->ichg_a  = (float)ichg_raw  * ichg_lsb_ma              / 1000.0f;
    adc->idchg_a = (float)idchg_raw * idchg_lsb_ma             / 1000.0f;
    adc->iin_a   = (float)iin_raw   * iin_lsb_ma               / 1000.0f;
    adc->vbat_v  = ((float)vbat_raw * BQ25731_ADC_VBAT_LSB_MV + BQ25731_ADC_VBAT_BASE_MV) / 1000.0f;
    adc->vsys_v  = ((float)vsys_raw * BQ25731_ADC_VSYS_LSB_MV + BQ25731_ADC_VSYS_BASE_MV) / 1000.0f;

    return ESP_OK;
}

esp_err_t bq25731_set_charge_current(uint16_t current_ma)
{
    ESP_RETURN_ON_FALSE(drv.initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");
    uint16_t lsb_ma = BQ25731_CHARGE_CURRENT_LSB_MA * BQ25731_CHARGE_CURRENT_RSR_REF / drv.rsr_mohm;
    uint16_t reg    = (uint16_t)((current_ma / lsb_ma) << 6) & BQ25731_CHARGE_CURRENT_MASK;
    ESP_RETURN_ON_ERROR(
        write_reg(BQ25731_REG_CHARGE_CURRENT, reg),
        TAG, "ChargeCurrent write failed");
    ESP_LOGI(TAG, "charge current set to %u mA", (uint16_t)((reg >> 6) * lsb_ma));
    return ESP_OK;
}

esp_err_t bq25731_set_charge_voltage(uint16_t voltage_mv)
{
    ESP_RETURN_ON_FALSE(drv.initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");
    uint16_t reg = voltage_mv & BQ25731_CHARGE_VOLTAGE_MASK;
    ESP_RETURN_ON_ERROR(
        write_reg(BQ25731_REG_CHARGE_VOLTAGE, reg),
        TAG, "ChargeVoltage write failed");
    ESP_LOGI(TAG, "charge voltage set to %u mV", reg);
    return ESP_OK;
}

esp_err_t bq25731_enable_charging(void)
{
    ESP_RETURN_ON_FALSE(drv.initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");
    ESP_RETURN_ON_ERROR(set_chrg_inhibit(false), TAG, "enable charging failed");
    ESP_LOGI(TAG, "charging enabled");
    return ESP_OK;
}

esp_err_t bq25731_disable_charging(void)
{
    ESP_RETURN_ON_FALSE(drv.initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");
    ESP_RETURN_ON_ERROR(set_chrg_inhibit(true), TAG, "disable charging failed");
    ESP_LOGI(TAG, "charging disabled");
    return ESP_OK;
}