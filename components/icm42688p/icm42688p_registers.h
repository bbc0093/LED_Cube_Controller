/**
 * @file icm42688p_registers.h
 * @brief ICM-42688-P register map and field definitions (private).
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

#ifndef ICM42688P_REGISTERS_H
#define ICM42688P_REGISTERS_H

// Bank 0 registers
#define ICM42688P_REG_DEVICE_CONFIG  0x11
#define ICM42688P_REG_TEMP_DATA1     0x1D
#define ICM42688P_REG_TEMP_DATA0     0x1E
#define ICM42688P_REG_ACCEL_DATA_X1  0x1F
#define ICM42688P_REG_ACCEL_DATA_X0  0x20
#define ICM42688P_REG_ACCEL_DATA_Y1  0x21
#define ICM42688P_REG_ACCEL_DATA_Y0  0x22
#define ICM42688P_REG_ACCEL_DATA_Z1  0x23
#define ICM42688P_REG_ACCEL_DATA_Z0  0x24
#define ICM42688P_REG_GYRO_DATA_X1   0x25
#define ICM42688P_REG_GYRO_DATA_X0   0x26
#define ICM42688P_REG_GYRO_DATA_Y1   0x27
#define ICM42688P_REG_GYRO_DATA_Y0   0x28
#define ICM42688P_REG_GYRO_DATA_Z1   0x29
#define ICM42688P_REG_GYRO_DATA_Z0   0x2A
#define ICM42688P_REG_PWR_MGMT0      0x4E
#define ICM42688P_REG_GYRO_CONFIG0   0x4F
#define ICM42688P_REG_ACCEL_CONFIG0  0x50
#define ICM42688P_REG_WHO_AM_I       0x75
#define ICM42688P_REG_REG_BANK_SEL   0x76

// Expected WHO_AM_I value
#define ICM42688P_WHO_AM_I_VAL       0x47

// I2C device addresses (selected by AP_AD0 pin)
#define ICM42688P_I2C_ADDR_AD0_GND   0x68u
#define ICM42688P_I2C_ADDR_AD0_VCC   0x69u

// DEVICE_CONFIG
#define ICM42688P_SOFT_RESET         0x01

// PWR_MGMT0 field encodings
#define ICM42688P_ACCEL_MODE_OFF     (0x0u << 4)
#define ICM42688P_ACCEL_MODE_LN      (0x3u << 4) // Low Noise
#define ICM42688P_GYRO_MODE_OFF      (0x0u << 2)
#define ICM42688P_GYRO_MODE_STANDBY  (0x1u << 2)
#define ICM42688P_GYRO_MODE_LN       (0x3u << 2) // Low Noise

// Accel sensitivity in LSB/g for each full-scale range (ACCEL_CONFIG0[7:5])
#define ICM42688P_ACCEL_SENS_16G     2048.0f
#define ICM42688P_ACCEL_SENS_8G      4096.0f
#define ICM42688P_ACCEL_SENS_4G      8192.0f
#define ICM42688P_ACCEL_SENS_2G      16384.0f

// Gyro sensitivity in LSB/dps for each full-scale range (GYRO_CONFIG0[7:5])
#define ICM42688P_GYRO_SENS_2000DPS    16.4f
#define ICM42688P_GYRO_SENS_1000DPS    32.8f
#define ICM42688P_GYRO_SENS_500DPS     65.5f
#define ICM42688P_GYRO_SENS_250DPS     131.0f
#define ICM42688P_GYRO_SENS_125DPS     262.1f
#define ICM42688P_GYRO_SENS_62_5DPS    524.3f
#define ICM42688P_GYRO_SENS_31_25DPS   1048.6f
#define ICM42688P_GYRO_SENS_15_625DPS  2097.2f

// Temperature formula: T(°C) = raw / ICM42688P_TEMP_SENS + ICM42688P_TEMP_OFFSET
#define ICM42688P_TEMP_SENS    132.48f
#define ICM42688P_TEMP_OFFSET  25.0f

// Conversion factor: degrees-per-second → radians-per-second
#define ICM42688P_DEG_TO_RAD   0.017453292519943295f

// Number of bytes for a full sensor burst read (temp + accel xyz + gyro xyz)
#define ICM42688P_BURST_DATA_LEN  14

// INT_CONFIG (0x06) — INT1 signal behaviour
#define ICM42688P_REG_INT_CONFIG         0x06
#define ICM42688P_INT1_DRIVE_PUSH_PULL   (1u << 1) // 0 = open-drain, 1 = push-pull
#define ICM42688P_INT1_ACTIVE_HIGH       (1u << 0) // 0 = active-low,  1 = active-high

// INT_SOURCE0 (0x65) — interrupt routing to INT1 pin
#define ICM42688P_REG_INT_SOURCE0        0x65
#define ICM42688P_INT_SOURCE0_DRDY_EN    (1u << 3) // Route UI data-ready to INT1

#endif // ICM42688P_REGISTERS_H