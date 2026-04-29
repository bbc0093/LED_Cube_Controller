/**
 * @file mcp9701a.c
 * @brief Driver for MCP9701/MCP9701A linear active thermistor ICs via ESP32 ADC.
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
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "mcp9701a.h"

// MCP9701A datasheet: output at 0 °C = 400 mV; temperature coefficient = 19.5 mV/°C.
#define MCP9701A_V0_MV          400.0f
#define MCP9701A_TC_MV_PER_C    19.5f

// Uncalibrated fallback: ADC_ATTEN_DB_12 practical full scale on ESP32-S3 is ~3100 mV.
#define MCP9701A_UNCAL_FULLSCALE_MV  3100
#define MCP9701A_ADC_MAX_RAW         4095

#define MUTEX_TIMEOUT_MS  50

static const char *TAG = "mcp9701a";

struct mcp9701a_driver {
    volatile bool initialized;
    size_t sensor_count;
    mcp9701a_sensor_cfg_t sensors[MCP9701A_MAX_SENSORS];
    // Indexed by adc_unit_t value (ADC_UNIT_1 = 0, ADC_UNIT_2 = 1).
    adc_oneshot_unit_handle_t adc_units[2];
    bool adc_unit_created[2];
    adc_cali_handle_t cali_handles[MCP9701A_MAX_SENSORS];
    bool cali_enabled[MCP9701A_MAX_SENSORS];
    SemaphoreHandle_t mutex;
};

static struct mcp9701a_driver drv = {0};

// Attempt to create a calibration handle for one channel.  Returns true on
// success and writes the handle to *handle; logs a warning and returns false
// if calibration data is unavailable (eFuse not burnt).
static bool create_calibration(adc_unit_t unit, adc_channel_t channel,
                                adc_cali_handle_t *handle)
{
    esp_err_t ret = ESP_FAIL;
    *handle = NULL;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cfg = {
        .unit_id  = unit,
        .chan     = channel,
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ret = adc_cali_create_scheme_curve_fitting(&cfg, handle);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t cfg = {
        .unit_id  = unit,
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ret = adc_cali_create_scheme_line_fitting(&cfg, handle);
#endif

    if (ret == ESP_OK) {
        return true;
    }

    if (ret == ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG, "ADC calibration not supported (eFuse not burnt); using uncalibrated mode");
    } else if (ret != ESP_FAIL) {
        ESP_LOGE(TAG, "ADC calibration init failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGW(TAG, "ADC calibration scheme not available on this build");
    }
    return false;
}

static void delete_calibration(adc_cali_handle_t handle)
{
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_delete_scheme_curve_fitting(handle);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_delete_scheme_line_fitting(handle);
#endif
}

// Must be called with drv.mutex held.
static esp_err_t read_sensor_locked(size_t idx, mcp9701a_data_t *data)
{
    int raw;
    ESP_RETURN_ON_ERROR(
        adc_oneshot_read(drv.adc_units[drv.sensors[idx].adc_unit],
                         drv.sensors[idx].adc_channel, &raw),
        TAG, "ADC read failed for sensor %zu", idx);

    data->raw = raw;

    if (drv.cali_enabled[idx]) {
        ESP_RETURN_ON_ERROR(
            adc_cali_raw_to_voltage(drv.cali_handles[idx], raw, &data->voltage_mv),
            TAG, "Calibration conversion failed for sensor %zu", idx);
    } else {
        data->voltage_mv = (raw * MCP9701A_UNCAL_FULLSCALE_MV) / MCP9701A_ADC_MAX_RAW;
    }

    data->temperature_c = ((float)data->voltage_mv - MCP9701A_V0_MV) / MCP9701A_TC_MV_PER_C;
    return ESP_OK;
}

esp_err_t mcp9701a_init(const mcp9701a_sensor_cfg_t *sensors, size_t count)
{
    ESP_RETURN_ON_FALSE(!drv.initialized, ESP_ERR_INVALID_STATE, TAG, "already initialized");
    ESP_RETURN_ON_FALSE(sensors != NULL, ESP_ERR_INVALID_ARG, TAG, "sensors must not be NULL");
    ESP_RETURN_ON_FALSE(count > 0 && count <= MCP9701A_MAX_SENSORS, ESP_ERR_INVALID_ARG,
                        TAG, "count must be 1..%d", MCP9701A_MAX_SENSORS);

    esp_err_t ret = ESP_OK;

    drv.mutex = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(drv.mutex != NULL, ESP_ERR_NO_MEM, TAG, "failed to create mutex");

    drv.sensor_count = count;
    for (size_t i = 0; i < count; i++) {
        drv.sensors[i] = sensors[i];
    }

    // Create one ADC oneshot unit per distinct ADC unit referenced.
    for (size_t i = 0; i < count; i++) {
        adc_unit_t unit = sensors[i].adc_unit;
        if (!drv.adc_unit_created[unit]) {
            adc_oneshot_unit_init_cfg_t unit_cfg = {.unit_id = unit};
            ESP_GOTO_ON_ERROR(
                adc_oneshot_new_unit(&unit_cfg, &drv.adc_units[unit]),
                err, TAG, "failed to create ADC unit %d", unit);
            drv.adc_unit_created[unit] = true;
        }
    }

    // Configure each sensor channel and attempt calibration.
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    for (size_t i = 0; i < count; i++) {
        ESP_GOTO_ON_ERROR(
            adc_oneshot_config_channel(drv.adc_units[sensors[i].adc_unit],
                                   sensors[i].adc_channel, &chan_cfg),
            err, TAG, "failed to configure channel for sensor %zu", i);

        drv.cali_enabled[i] = create_calibration(sensors[i].adc_unit,
                                                  sensors[i].adc_channel,
                                                  &drv.cali_handles[i]);
    }

    drv.initialized = true;
    ESP_LOGI(TAG, "initialized with %zu sensor(s)", count);
    return ESP_OK;

err:
    for (size_t i = 0; i < count; i++) {
        if (drv.cali_enabled[i] && drv.cali_handles[i]) {
            delete_calibration(drv.cali_handles[i]);
        }
    }
    for (size_t i = 0; i < 2; i++) {
        if (drv.adc_unit_created[i]) {
            adc_oneshot_del_unit(drv.adc_units[i]);
        }
    }
    vSemaphoreDelete(drv.mutex);
    memset(&drv, 0, sizeof(drv));
    return ret;
}

void mcp9701a_uninit(void)
{
    assert(drv.initialized && "mcp9701a_uninit called before mcp9701a_init");

    for (size_t i = 0; i < drv.sensor_count; i++) {
        if (drv.cali_enabled[i] && drv.cali_handles[i]) {
            delete_calibration(drv.cali_handles[i]);
        }
    }
    for (size_t i = 0; i < 2; i++) {
        if (drv.adc_unit_created[i]) {
            adc_oneshot_del_unit(drv.adc_units[i]);
        }
    }
    vSemaphoreDelete(drv.mutex);
    memset(&drv, 0, sizeof(drv));
    ESP_LOGI(TAG, "uninitialized");
}

esp_err_t mcp9701a_read(size_t sensor_idx, mcp9701a_data_t *data)
{
    ESP_RETURN_ON_FALSE(drv.initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");
    ESP_RETURN_ON_FALSE(data != NULL, ESP_ERR_INVALID_ARG, TAG, "data must not be NULL");
    ESP_RETURN_ON_FALSE(sensor_idx < drv.sensor_count, ESP_ERR_INVALID_ARG,
                        TAG, "sensor_idx %zu out of range (count=%zu)",
                        sensor_idx, drv.sensor_count);

    if (xSemaphoreTake(drv.mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "mutex timeout");
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t ret = read_sensor_locked(sensor_idx, data);
    xSemaphoreGive(drv.mutex);
    return ret;
}

esp_err_t mcp9701a_read_all(mcp9701a_data_t *out, size_t count)
{
    ESP_RETURN_ON_FALSE(drv.initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");
    ESP_RETURN_ON_FALSE(out != NULL, ESP_ERR_INVALID_ARG, TAG, "out must not be NULL");
    ESP_RETURN_ON_FALSE(count >= drv.sensor_count, ESP_ERR_INVALID_ARG,
                        TAG, "output array too small (%zu < %zu)", count, drv.sensor_count);

    if (xSemaphoreTake(drv.mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "mutex timeout");
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t ret = ESP_OK;
    for (size_t i = 0; i < drv.sensor_count && ret == ESP_OK; i++) {
        ret = read_sensor_locked(i, &out[i]);
    }
    xSemaphoreGive(drv.mutex);
    return ret;
}