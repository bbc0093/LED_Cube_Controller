/**
 * @file icm42688p.h
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

#ifndef LED_CUBE_CONTROLLER_ICM42688P_H
#define LED_CUBE_CONTROLLER_ICM42688P_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

// I2C address options, selected by the AP_AD0 pin.
enum icm42688p_address {
    ICM42688P_ADDR_AD0_GND = 0x68,
    ICM42688P_ADDR_AD0_VCC = 0x69,
};

// Accelerometer full-scale range.  Encoded values match ACCEL_CONFIG0[7:5].
enum icm42688p_accel_fs {
    ICM42688P_ACCEL_FS_16G = 0,
    ICM42688P_ACCEL_FS_8G,
    ICM42688P_ACCEL_FS_4G,
    ICM42688P_ACCEL_FS_2G,
};

// Gyroscope full-scale range.  Encoded values match GYRO_CONFIG0[7:5].
enum icm42688p_gyro_fs {
    ICM42688P_GYRO_FS_2000DPS = 0,
    ICM42688P_GYRO_FS_1000DPS,
    ICM42688P_GYRO_FS_500DPS,
    ICM42688P_GYRO_FS_250DPS,
    ICM42688P_GYRO_FS_125DPS,
    ICM42688P_GYRO_FS_62_5DPS,
    ICM42688P_GYRO_FS_31_25DPS,
    ICM42688P_GYRO_FS_15_625DPS,
};

// Output data rate.  Encoded values match GYRO_CONFIG0[3:0] and ACCEL_CONFIG0[3:0].
enum icm42688p_odr {
    ICM42688P_ODR_32KHZ  = 1,
    ICM42688P_ODR_16KHZ  = 2,
    ICM42688P_ODR_8KHZ   = 3,
    ICM42688P_ODR_4KHZ   = 4,
    ICM42688P_ODR_2KHZ   = 5,
    ICM42688P_ODR_1KHZ   = 6,
    ICM42688P_ODR_200HZ  = 7,
    ICM42688P_ODR_100HZ  = 8,
    ICM42688P_ODR_50HZ   = 9,
    ICM42688P_ODR_25HZ   = 10,
    ICM42688P_ODR_12_5HZ = 11,
};

typedef struct {
    i2c_master_bus_handle_t  bus_handle;    // Optional: pre-created bus handle to share. NULL = create a new bus.
    enum icm42688p_address   address;       // I2C address; set by the AP_AD0 pin.
    uint32_t                 scl_speed_hz;  // Typically 400000; device supports up to 1 MHz.
    enum icm42688p_accel_fs  accel_fs;
    enum icm42688p_gyro_fs   gyro_fs;
    enum icm42688p_odr       odr;
} icm42688p_config_t;

// Raw 16-bit output straight from the sensor registers.
typedef struct {
    int16_t accel_x;
    int16_t accel_y;
    int16_t accel_z;
    int16_t gyro_x;
    int16_t gyro_y;
    int16_t gyro_z;
    int16_t temp;
} icm42688p_raw_data_t;

// Sensor data scaled to SI units.
typedef struct {
    float accel_x; // m/s²
    float accel_y;
    float accel_z;
    float gyro_x;  // rad/s
    float gyro_y;
    float gyro_z;
    float temp;    // °C
} icm42688p_data_t;

/**
 * @brief Initialize the ICM-42688-P driver.
 *
 * Optionally creates the I2C bus, adds the device, performs a soft reset,
 * verifies the WHO_AM_I register, configures the sensor, and enables
 * low-noise mode.  Blocks for ~50 ms while the sensors reach steady state.
 *
 * @param config Driver configuration.  Must not be NULL.
 *
 * @return
 *      - ESP_OK on success.
 *      - ESP_ERR_INVALID_STATE if already initialized.
 *      - ESP_ERR_NOT_FOUND if WHO_AM_I does not match the expected value.
 *      - Any error forwarded from the I2C driver.
 */
esp_err_t icm42688p_init(const icm42688p_config_t *config);

/**
 * @brief Shut down the ICM-42688-P driver.
 *
 * Puts the sensor into low-power mode, removes the I2C device, and
 * optionally deinitializes the I2C bus.
 *
 * @note Aborts if called before icm42688p_init().
 */
void icm42688p_uninit(void);

/**
 * @brief Read raw 16-bit sensor values from the device.
 *
 * @param data Output structure populated with the raw register values.
 *
 * @return
 *      - ESP_OK on success.
 *      - ESP_ERR_INVALID_STATE if the driver is not initialized.
 *      - Any error forwarded from the I2C driver.
 */
esp_err_t icm42688p_read_raw(icm42688p_raw_data_t *data);

/**
 * @brief Read sensor data scaled to SI units.
 *
 * @param data Output structure populated with scaled values.
 *
 * @return
 *      - ESP_OK on success.
 *      - ESP_ERR_INVALID_STATE if the driver is not initialized.
 *      - Any error forwarded from the I2C driver.
 */
esp_err_t icm42688p_read(icm42688p_data_t *data);

#endif // LED_CUBE_CONTROLLER_ICM42688P_H