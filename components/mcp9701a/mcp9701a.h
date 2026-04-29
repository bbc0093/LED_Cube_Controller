/**
 * @file mcp9701a.h
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

#ifndef LED_CUBE_CONTROLLER_MCP9701A_H
#define LED_CUBE_CONTROLLER_MCP9701A_H

#include <stddef.h>
#include "esp_err.h"
#include "esp_adc/adc_oneshot.h"

// Maximum number of sensors managed by one driver instance.
#define MCP9701A_MAX_SENSORS  8

// Configuration for a single MCP9701A sensor.
typedef struct {
    adc_unit_t    adc_unit;
    adc_channel_t adc_channel;
} mcp9701a_sensor_cfg_t;

// Output from a single sensor read.
typedef struct {
    int   raw;           // Raw 12-bit ADC count (0–4095)
    int   voltage_mv;    // Calibrated output voltage in millivolts
    float temperature_c; // Temperature in °C
} mcp9701a_data_t;

/**
 * @brief Initialize the MCP9701A driver with one or more sensors.
 *
 * Configures ADC oneshot units and per-channel calibration (curve-fitting
 * scheme preferred; falls back to line-fitting when unavailable).  All
 * sensors are sampled at ADC_ATTEN_DB_12 (0–3.1 V), which covers the full
 * MCP9701A output range of ~0–2838 mV.
 *
 * @param sensors   Array of sensor configurations.  Must not be NULL.
 * @param count     Number of sensors (1–MCP9701A_MAX_SENSORS).
 *
 * @return
 *      - ESP_OK on success.
 *      - ESP_ERR_INVALID_STATE if already initialized.
 *      - ESP_ERR_INVALID_ARG   if sensors is NULL or count is out of range.
 *      - ESP_ERR_NO_MEM        if mutex or ADC unit allocation fails.
 *      - Any error forwarded from the ADC driver.
 */
esp_err_t mcp9701a_init(const mcp9701a_sensor_cfg_t *sensors, size_t count);

/**
 * @brief Shut down the MCP9701A driver.
 *
 * Deletes calibration handles, ADC oneshot units, and the mutex then zeros
 * the driver state.
 *
 * @note Aborts if called before mcp9701a_init().
 */
void mcp9701a_uninit(void);

/**
 * @brief Read a single sensor by index.
 *
 * @param sensor_idx  Zero-based index into the array passed to mcp9701a_init().
 * @param data        Output populated with raw count, voltage, and temperature.
 *
 * @return
 *      - ESP_OK on success.
 *      - ESP_ERR_INVALID_STATE if the driver is not initialized.
 *      - ESP_ERR_INVALID_ARG   if sensor_idx is out of range or data is NULL.
 *      - ESP_ERR_TIMEOUT       if the internal mutex could not be acquired.
 *      - Any error forwarded from the ADC driver.
 */
esp_err_t mcp9701a_read(size_t sensor_idx, mcp9701a_data_t *data);

/**
 * @brief Read all sensors in one mutex-protected call.
 *
 * @param out    Output array.  Must have room for at least the sensor count
 *               that was passed to mcp9701a_init().
 * @param count  Capacity of out.  Must be >= sensor count from init.
 *
 * @return
 *      - ESP_OK on success.
 *      - ESP_ERR_INVALID_STATE if the driver is not initialized.
 *      - ESP_ERR_INVALID_ARG   if out is NULL or count is too small.
 *      - ESP_ERR_TIMEOUT       if the internal mutex could not be acquired.
 *      - Any error forwarded from the ADC driver.
 */
esp_err_t mcp9701a_read_all(mcp9701a_data_t *out, size_t count);

#endif // LED_CUBE_CONTROLLER_MCP9701A_H