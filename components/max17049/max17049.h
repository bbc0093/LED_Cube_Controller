/**
 * @file max17049.h
 * @brief Driver for the Analog Devices MAX17049G ModelGauge fuel gauge IC over I2C.
 *
 * The MAX17049 is the dual-cell (2S Li+) variant of the MAX17048 family.
 * Its I2C address is fixed at 0x36 and cannot be changed.
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

#ifndef LED_CUBE_CONTROLLER_MAX17049_H
#define LED_CUBE_CONTROLLER_MAX17049_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

typedef struct {
    i2c_port_num_t i2c_port;
    int            sda_pin;
    int            scl_pin;
    i2c_master_bus_handle_t bus_handle; // Optional: pre-created bus handle to share. NULL = create a new bus.
    uint32_t       scl_speed_hz;    // Typically 100000 or 400000.
    uint8_t        alert_threshold_pct; // SOC% below which the nALRT pin asserts (1–32, default 4).
    uint8_t        rcomp;               // ModelGauge compensation byte (0x00–0xFF, default 0x97).
} max17049_config_t;

// Battery measurements from one read cycle.
typedef struct {
    float voltage_v;    // Battery stack voltage (two cells in series).
    float soc_pct;      // State of charge (0.0–100.0 %).
    float crate_pct_hr; // Charge / discharge rate in %/hr.  Positive = charging.
    bool  alert;        // True if any alert flag is set in the CONFIG register.
} max17049_data_t;

/**
 * @brief Initialize the MAX17049 driver.
 *
 * Optionally creates the I2C master bus, adds the device at address 0x36,
 * clears the power-on reset indicator, and writes the configuration register
 * with the requested alert threshold and RCOMP value.
 *
 * @param config Driver configuration.  Must not be NULL.
 *
 * @return
 *      - ESP_OK on success.
 *      - ESP_ERR_INVALID_STATE if already initialized.
 *      - ESP_ERR_INVALID_ARG   if config is NULL or alert_threshold_pct is out of range.
 *      - Any error forwarded from the I2C driver.
 */
esp_err_t max17049_init(const max17049_config_t *config);

/**
 * @brief Shut down the MAX17049 driver.
 *
 * Puts the device into sleep mode, removes the I2C device, and optionally
 * deinitializes the I2C bus.
 *
 * @note Aborts if called before max17049_init().
 */
void max17049_uninit(void);

/**
 * @brief Read voltage, state of charge, and charge rate in one I2C transaction set.
 *
 * @param data Output populated with all measurements.
 *
 * @return
 *      - ESP_OK on success.
 *      - ESP_ERR_INVALID_STATE if the driver is not initialized.
 *      - ESP_ERR_INVALID_ARG   if data is NULL.
 *      - Any error forwarded from the I2C driver.
 */
esp_err_t max17049_read(max17049_data_t *data);

/**
 * @brief Read only the battery stack voltage.
 *
 * @param voltage_v Output voltage in volts.  Must not be NULL.
 *
 * @return Same as max17049_read().
 */
esp_err_t max17049_read_voltage(float *voltage_v);

/**
 * @brief Read only the state of charge.
 *
 * @param soc_pct Output SOC in percent (0.0–100.0).  Must not be NULL.
 *
 * @return Same as max17049_read().
 */
esp_err_t max17049_read_soc(float *soc_pct);

/**
 * @brief Clear all pending alert flags in the CONFIG and STATUS registers.
 *
 * Call this after handling an nALRT interrupt to de-assert the pin.
 *
 * @return
 *      - ESP_OK on success.
 *      - ESP_ERR_INVALID_STATE if the driver is not initialized.
 *      - Any error forwarded from the I2C driver.
 */
esp_err_t max17049_clear_alert(void);

/**
 * @brief Trigger a ModelGauge quick-start.
 *
 * Forces the algorithm to restart SOC estimation from the current cell voltage.
 * Useful after connecting a pre-charged battery.
 *
 * @return
 *      - ESP_OK on success.
 *      - ESP_ERR_INVALID_STATE if the driver is not initialized.
 *      - Any error forwarded from the I2C driver.
 */
esp_err_t max17049_quick_start(void);

#endif // LED_CUBE_CONTROLLER_MAX17049_H