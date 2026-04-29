/**
 * @file tmp102.h
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

#ifndef LED_CUBE_CONTROLLER_TMP102_H
#define LED_CUBE_CONTROLLER_TMP102_H

#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

// I2C address determined by the ADD0 pin wiring.
enum tmp102_address {
    TMP102_ADDR_GND = 0x48, // ADD0 → GND
    TMP102_ADDR_VCC = 0x49, // ADD0 → V+
    TMP102_ADDR_SDA = 0x4A, // ADD0 → SDA
    TMP102_ADDR_SCL = 0x4B, // ADD0 → SCL
};

// Continuous conversion rate.  Encoded values match CR[1:0] in the config register.
enum tmp102_conv_rate {
    TMP102_CONV_RATE_0_25HZ = 0,
    TMP102_CONV_RATE_1HZ    = 1,
    TMP102_CONV_RATE_4HZ    = 2, // default
    TMP102_CONV_RATE_8HZ    = 3,
};

typedef struct {
    i2c_port_num_t        i2c_port;
    int                   sda_pin;
    int                   scl_pin;
    i2c_master_bus_handle_t bus_handle;   // Optional: pre-created bus handle to share. NULL = create a new bus.
    enum tmp102_address   address;
    uint32_t              scl_speed_hz;    // Typically 100000 or 400000.
    enum tmp102_conv_rate conv_rate;
    bool                  extended_mode;   // False = 12-bit (±0.0625 °C), true = 13-bit.
} tmp102_config_t;

// Raw sensor output and the derived temperature.
typedef struct {
    int16_t raw;          // Signed 12- or 13-bit ADC count from the temperature register.
    float   temperature_c;
} tmp102_data_t;

/**
 * @brief Initialize the TMP102 driver.
 *
 * Optionally initializes the I2C master bus, adds the device, and writes the
 * configuration register to set the conversion rate and resolution mode.
 *
 * @param config Driver configuration.  Must not be NULL.
 *
 * @return
 *      - ESP_OK on success.
 *      - ESP_ERR_INVALID_STATE if already initialized.
 *      - ESP_ERR_INVALID_ARG   if config is NULL.
 *      - Any error forwarded from the I2C driver.
 */
esp_err_t tmp102_init(const tmp102_config_t *config);

/**
 * @brief Shut down the TMP102 driver.
 *
 * Puts the sensor into shutdown mode, removes the I2C device, and optionally
 * deinitializes the I2C bus.
 *
 * @note Aborts if called before tmp102_init().
 */
void tmp102_uninit(void);

/**
 * @brief Read the current temperature from the sensor.
 *
 * @param data Output populated with the raw register value and temperature in °C.
 *
 * @return
 *      - ESP_OK on success.
 *      - ESP_ERR_INVALID_STATE if the driver is not initialized.
 *      - ESP_ERR_INVALID_ARG   if data is NULL.
 *      - Any error forwarded from the I2C driver.
 */
esp_err_t tmp102_read(tmp102_data_t *data);

/**
 * @brief Convenience wrapper that returns only the temperature in °C.
 *
 * @param temperature_c Output temperature.  Must not be NULL.
 *
 * @return Same as tmp102_read().
 */
esp_err_t tmp102_read_temperature(float *temperature_c);

#endif // LED_CUBE_CONTROLLER_TMP102_H