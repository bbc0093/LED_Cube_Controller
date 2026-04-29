/**
 * @file mode_imu_test.h
 * @brief IMU-test LED mode declaration.
 *
 * Cycles through the HSV color wheel while rendering a vertical brightness
 * gradient that mirrors the cube's physical orientation:
 *   Top face    — 25 % brightness (all pixels uniform)
 *   Bottom face — 100 % brightness (all pixels uniform)
 *   Side faces  — linear gradient, 25 % at the top edge → 100 % at the bottom
 *
 * @author William Crow
 * @date 2026-04-29
 */

/*
 * @copyright
 * MIT License
 * Copyright (c) 2026 William Crow
 */

#ifndef LED_CUBE_CONTROLLER_MODE_IMU_TEST_H
#define LED_CUBE_CONTROLLER_MODE_IMU_TEST_H

#include "led_modes.h"

extern const led_mode_t mode_imu_test;

#endif // LED_CUBE_CONTROLLER_MODE_IMU_TEST_H