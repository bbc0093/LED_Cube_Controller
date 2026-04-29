/**
 * @file peripherals.h
 * @brief Hardware pin mapping and peripheral configuration for the LED Cube Controller.
 *
 * Edit the constants in this file to match your PCB layout before building.
 * All I2C devices share a single bus (PERIPH_I2C_PORT) created in app_main().
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

#ifndef LED_CUBE_CONTROLLER_PERIPHERALS_H
#define LED_CUBE_CONTROLLER_PERIPHERALS_H

#include "esp_adc/adc_oneshot.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "tmp102.h"
#include "icm42688p.h"

// ── LED panels (WS2812B via RMT) ─────────────────────────────────────────────
// One data line per cube face; all driven by the ESP32-S3 RMT peripheral.
// PWR_EN is active-high and gates the 5 V supply to all six panels.
#define PERIPH_LED_TOP_PIN     GPIO_NUM_17
#define PERIPH_LED_BOTTOM_PIN  GPIO_NUM_10
#define PERIPH_LED_LEFT_PIN    GPIO_NUM_12
#define PERIPH_LED_RIGHT_PIN   GPIO_NUM_11
#define PERIPH_LED_FRONT_PIN   GPIO_NUM_13
#define PERIPH_LED_BACK_PIN    GPIO_NUM_9
#define PERIPH_LED_PWR_EN_PIN  GPIO_NUM_18

// ── Shared I2C bus ────────────────────────────────────────────────────────────
// All I2C peripherals (TMP102, MAX17049G, BQ25731, ICM-42688-P) share this bus.
#define PERIPH_I2C_PORT      I2C_NUM_0
#define PERIPH_I2C_SDA_PIN   GPIO_NUM_38
#define PERIPH_I2C_SCL_PIN   GPIO_NUM_39
#define PERIPH_I2C_SPEED_HZ  400000

// ── TMP102 temperature sensor ─────────────────────────────────────────────────
// Address is set by the ADD0 pin; see enum tmp102_address.
// ALERT is open-drain active-low; asserts when temperature crosses T_HIGH or
// T_LOW thresholds programmed in the sensor's limit registers.
// Connect to a GPIO with an external pull-up.
// NOTE: the driver does not yet configure thresholds or handle this interrupt.
// NOTE: GPIO 33–37 are used for Octal PSRAM on some ESP32-S3 module variants
//       (e.g. N8R8); verify your module's datasheet before using GPIO 33 here.
#define PERIPH_TMP102_ADDRESS    TMP102_ADDR_GND
#define PERIPH_TMP102_ALERT_PIN  GPIO_NUM_33

// ── MAX17049G fuel gauge ──────────────────────────────────────────────────────
// I2C address is fixed at 0x36 (no address pin).
// nALRT is open-drain active-low; connect to a GPIO with an external pull-up.
// Call max17049_clear_alert() inside the ISR or a deferred task to de-assert.
#define PERIPH_MAX17049_NALRT_PIN   GPIO_NUM_46
#define PERIPH_MAX17049_ALERT_PCT   10          // Assert nALRT when SOC < 10 %
#define PERIPH_MAX17049_RCOMP       0x97        // ModelGauge compensation (tune per battery App Note)

// ── BQ25731 battery charger ───────────────────────────────────────────────────
// I2C address is fixed at 0x6B (no address pin).
// CHRG_OK is open-drain active-high (connect to a GPIO with an external pull-up).
// HIGH = charger active (pre-charge or fast-charge).
// LOW  = charger idle, charge complete, or fault.
// PROCHOT# is open-drain active-low; asserts when a protection threshold is
// exceeded (input overcurrent, battery overcurrent, overtemperature, etc.).
// Connect to a GPIO with an external pull-up.
#define PERIPH_BQ25731_CHRG_OK_PIN  GPIO_NUM_45
#define PERIPH_BQ25731_PROCHOT_PIN  GPIO_NUM_40
#define PERIPH_BQ25731_RSR_MOHM     5       // Battery current sense resistor (mΩ): 5 or 10
#define PERIPH_BQ25731_RAC_MOHM     5       // Input current sense resistor (mΩ): 5 or 10
#define PERIPH_BQ25731_CHARGE_MV    8400    // 2S Li+ full-charge voltage (4.20 V × 2 cells)
#define PERIPH_BQ25731_CHARGE_MA    2048    // Charge current; must be a multiple of 128 mA (at RSR = 5 mΩ)
#define PERIPH_BQ25731_INPUT_MA     3000    // Input current limit; must be a multiple of 100 mA (at RAC = 5 mΩ)

// ── ICM-42688-P IMU (I2C, shared bus) ────────────────────────────────────────
// Address is set by the AP_AD0 pin: GND = 0x68, VCC = 0x69.
// INT1 is active-high push-pull by default; connect to a GPIO with no pull resistor.
#define PERIPH_IMU_ADDRESS     ICM42688P_ADDR_AD0_GND
#define PERIPH_IMU_INT_PIN     GPIO_NUM_21

// Accelerometer full-scale range: ±2 G, ±4 G, ±8 G, or ±16 G.
// Smaller range = finer resolution; larger range = handles bigger shocks without saturating.
#define PERIPH_IMU_ACCEL_FS    ICM42688P_ACCEL_FS_4G

// Gyroscope full-scale range: ±15.625 – ±2000 °/s.
// Smaller range = finer resolution; larger range = handles faster rotation without saturating.
#define PERIPH_IMU_GYRO_FS     ICM42688P_GYRO_FS_500DPS

// Output data rate: how many sensor samples per second the IMU produces.
// 100 Hz is a good balance between responsiveness and I2C bus load.
#define PERIPH_IMU_ODR         ICM42688P_ODR_100HZ

// ── MCP9701A thermistors (ADC) ────────────────────────────────────────────────
// 8 sensors total: 2 battery cells + 6 cube faces.
// All on ADC_UNIT_1 (channels 0–9, GPIOs 1–10) — ADC_UNIT_2 is unavailable when Wi-Fi is active.
//
// ESP32-S3 ADC1 channel → GPIO mapping:
//   ADC_CHANNEL_0 = GPIO  1      ADC_CHANNEL_5 = GPIO  6
//   ADC_CHANNEL_1 = GPIO  2      ADC_CHANNEL_6 = GPIO  7
//   ADC_CHANNEL_2 = GPIO  3      ADC_CHANNEL_7 = GPIO  8
//   ADC_CHANNEL_3 = GPIO  4      ADC_CHANNEL_8 = GPIO  9
//   ADC_CHANNEL_4 = GPIO  5      ADC_CHANNEL_9 = GPIO 10
//
// Update PERIPH_THERM_COUNT and channel assignments to match your PCB layout.
#define PERIPH_THERM_COUNT  8

// Battery pack sensors
#define PERIPH_THERM_BAT0_ADC_UNIT  ADC_UNIT_1
#define PERIPH_THERM_BAT0_ADC_CH    ADC_CHANNEL_0

#define PERIPH_THERM_BAT1_ADC_UNIT  ADC_UNIT_1
#define PERIPH_THERM_BAT1_ADC_CH    ADC_CHANNEL_1

// Cube face sensors
#define PERIPH_THERM_FRONT_ADC_UNIT  ADC_UNIT_1
#define PERIPH_THERM_FRONT_ADC_CH    ADC_CHANNEL_4

#define PERIPH_THERM_BACK_ADC_UNIT   ADC_UNIT_1
#define PERIPH_THERM_BACK_ADC_CH     ADC_CHANNEL_5

#define PERIPH_THERM_TOP_ADC_UNIT    ADC_UNIT_1
#define PERIPH_THERM_TOP_ADC_CH      ADC_CHANNEL_2

#define PERIPH_THERM_BOTTOM_ADC_UNIT ADC_UNIT_1
#define PERIPH_THERM_BOTTOM_ADC_CH   ADC_CHANNEL_3

#define PERIPH_THERM_LEFT_ADC_UNIT   ADC_UNIT_1
#define PERIPH_THERM_LEFT_ADC_CH     ADC_CHANNEL_6

#define PERIPH_THERM_RIGHT_ADC_UNIT  ADC_UNIT_1
#define PERIPH_THERM_RIGHT_ADC_CH    ADC_CHANNEL_7

#endif // LED_CUBE_CONTROLLER_PERIPHERALS_H