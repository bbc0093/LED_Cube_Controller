/**
 * @file bq25731.h
 * @brief Driver for the Texas Instruments BQ25731 multi-phase buck battery charger over I2C.
 *
 * The BQ25731 is the dual-phase variant of the BQ25730 family.  It supports
 * wide input voltage (3.5–24 V) and programmable charge voltage up to 19.2 V,
 * making it suitable for 1S–4S Li+ battery packs.
 *
 * I2C address is fixed at 0x6B (no address pin).
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

#ifndef LED_CUBE_CONTROLLER_BQ25731_H
#define LED_CUBE_CONTROLLER_BQ25731_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

typedef struct {
    i2c_port_num_t          i2c_port;
    int                     sda_pin;
    int                     scl_pin;
    i2c_master_bus_handle_t bus_handle;        // Optional: pre-created bus handle to share. NULL = create a new bus.
    uint32_t                scl_speed_hz;      // Typically 100000 or 400000.
    uint8_t                 rsr_mohm;          // Battery current sense resistor value in mΩ (5 or 10).
    uint8_t                 rac_mohm;          // Input current sense resistor value in mΩ (5 or 10).
    uint16_t                charge_voltage_mv; // Target charge voltage in mV; must be a multiple of 8 mV.
    uint16_t                charge_current_ma; // Charge current in mA; must be a multiple of 128 mA (at RSR = 5 mΩ).
    uint16_t                input_current_ma;  // Input current limit in mA; must be a multiple of 100 mA (at RAC = 5 mΩ).
} bq25731_config_t;

// Charger operating state decoded from the ChargerStatus register.
typedef struct {
    bool ac_present;  // AC adapter detected on VBUS.
    bool in_fchrg;    // Fast-charge phase active (constant current).
    bool in_vindpm;   // Input voltage regulation (VINDPM) active.
    bool in_idpm;     // Input current regulation (IIN_DPM) active.
    bool ico_done;    // Input current optimisation complete.
    bool fault;       // One or more fault bits set (AC OVP, latch-off, SYSOVP, etc.).
} bq25731_status_t;

// ADC measurements.  Call bq25731_read_adc() to trigger a conversion and populate.
typedef struct {
    float vbus_v;   // Input (VBUS) voltage in V.
    float psys_v;   // PSYS pin voltage in V (proportional to total system power).
    float ichg_a;   // Battery charge current in A.
    float idchg_a;  // Battery discharge current in A.
    float iin_a;    // Input current in A.
    float vbat_v;   // Battery pack voltage in V.
    float vsys_v;   // System rail voltage in V.
} bq25731_adc_t;

/**
 * @brief Initialize the BQ25731 driver.
 *
 * Optionally creates the I2C bus, adds the device, verifies the manufacturer
 * and device IDs, disables the watchdog timer, configures sense resistor
 * selection, and writes the initial charge current, charge voltage, and input
 * current limit registers.  Charging begins automatically when a valid input
 * voltage is present.
 *
 * @param config Driver configuration.  Must not be NULL.
 *
 * @return
 *      - ESP_OK on success.
 *      - ESP_ERR_INVALID_STATE if already initialized.
 *      - ESP_ERR_INVALID_ARG   if config is NULL.
 *      - ESP_ERR_NOT_FOUND     if the manufacturer or device ID does not match.
 *      - Any error forwarded from the I2C driver.
 */
esp_err_t bq25731_init(const bq25731_config_t *config);

/**
 * @brief Shut down the BQ25731 driver.
 *
 * Disables charging, removes the I2C device, and optionally deinitializes
 * the I2C bus.
 *
 * @note Aborts if called before bq25731_init().
 */
void bq25731_uninit(void);

/**
 * @brief Read the current charger operating status.
 *
 * @param status Output populated from the ChargerStatus register.
 *
 * @return
 *      - ESP_OK on success.
 *      - ESP_ERR_INVALID_STATE if the driver is not initialized.
 *      - ESP_ERR_INVALID_ARG   if status is NULL.
 *      - Any error forwarded from the I2C driver.
 */
esp_err_t bq25731_read_status(bq25731_status_t *status);

/**
 * @brief Trigger a one-shot ADC conversion and read all results.
 *
 * Blocks for up to 50 ms while the conversion completes.
 *
 * @param adc Output populated with all measured voltages and currents.
 *
 * @return
 *      - ESP_OK on success.
 *      - ESP_ERR_INVALID_STATE if the driver is not initialized.
 *      - ESP_ERR_INVALID_ARG   if adc is NULL.
 *      - ESP_ERR_TIMEOUT       if the ADC conversion did not complete in time.
 *      - Any error forwarded from the I2C driver.
 */
esp_err_t bq25731_read_adc(bq25731_adc_t *adc);

/**
 * @brief Update the charge current register.
 *
 * The value is rounded down to the nearest hardware step (128 mA at RSR = 5 mΩ,
 * 64 mA at RSR = 10 mΩ).
 *
 * @param current_ma New charge current in mA.
 *
 * @return
 *      - ESP_OK on success.
 *      - ESP_ERR_INVALID_STATE if the driver is not initialized.
 *      - Any error forwarded from the I2C driver.
 */
esp_err_t bq25731_set_charge_current(uint16_t current_ma);

/**
 * @brief Update the charge voltage register.
 *
 * The value is rounded down to the nearest 8 mV step.
 *
 * @param voltage_mv New charge voltage in mV.
 *
 * @return
 *      - ESP_OK on success.
 *      - ESP_ERR_INVALID_STATE if the driver is not initialized.
 *      - Any error forwarded from the I2C driver.
 */
esp_err_t bq25731_set_charge_voltage(uint16_t voltage_mv);

/**
 * @brief Enable charging by clearing the CHRG_INHIBIT bit in ChargeOption0.
 *
 * @return
 *      - ESP_OK on success.
 *      - ESP_ERR_INVALID_STATE if the driver is not initialized.
 *      - Any error forwarded from the I2C driver.
 */
esp_err_t bq25731_enable_charging(void);

/**
 * @brief Disable charging by setting the CHRG_INHIBIT bit in ChargeOption0.
 *
 * The charger continues to power the system from the input; only the battery
 * charge path is disabled.
 *
 * @return
 *      - ESP_OK on success.
 *      - ESP_ERR_INVALID_STATE if the driver is not initialized.
 *      - Any error forwarded from the I2C driver.
 */
esp_err_t bq25731_disable_charging(void);

#endif // LED_CUBE_CONTROLLER_BQ25731_H