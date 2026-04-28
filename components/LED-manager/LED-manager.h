/**
 * @file LED-manager.h
 * @brief Provide an interface and abstraction layer for controlling the
 *        faces of the LED Cube.
 *
 * @author William Crow
 * @date 4/5/2026
 * @last_modified 2026-04-27 23:41:46
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

#ifndef LED_CUBE_CONTROLLER_LED_MANAGER_H
#define LED_CUBE_CONTROLLER_LED_MANAGER_H

#include <stdint.h>
#include "esp_err.h"

enum LED_manager_cube_face {
	LED_MANAGER_CUBE_FACE_TOP = 0,
	LED_MANAGER_CUBE_FACE_BOTTOM,
	LED_MANAGER_CUBE_FACE_LEFT,
	LED_MANAGER_CUBE_FACE_RIGHT,
	LED_MANAGER_CUBE_FACE_FRONT,
	LED_MANAGER_CUBE_FACE_BACK,
	LED_MANAGER_CUBE_FACE_COUNT
};

#define LED_MANAGER_LED_BIT_DEPTH       8 //WS2812B
#define LED_MANAGER_LED_SIZE_MASK      ((1U << LED_MANAGER_LED_BIT_DEPTH) - 1)
#define LED_MANAGER_PANEL_LED_COUNT_1D  8
#define LED_MANAGER_PANEL_LED_COUNT \
	(LED_MANAGER_PANEL_LED_COUNT_1D * LED_MANAGER_PANEL_LED_COUNT_1D)
#define LED_MANAGER_CUBE_LED_COUNT \
	(LED_MANAGER_PANEL_LED_COUNT * LED_MANAGER_CUBE_FACE_COUNT)

typedef uint32_t led_color_t; // RGB

static_assert(LED_MANAGER_LED_BIT_DEPTH * 3 <= sizeof(led_color_t) * 8,
              "LED Color size exceeds led_color_t");

#define LED_MANGER_RGB_TO_COLOR(r, g, b) \
	((((r) & LED_MANAGER_LED_SIZE_MASK) << (LED_MANAGER_LED_BIT_DEPTH * 2)) + \
	 (((g) & LED_MANAGER_LED_SIZE_MASK) << LED_MANAGER_LED_BIT_DEPTH) + \
	 ((b) & LED_MANAGER_LED_SIZE_MASK))

#define LED_MANAGER_APPLY_ALPHA(a, c) \
	((a) * (c) >> LED_MANAGER_LED_BIT_DEPTH)
#define LED_MANGER_ARGB_TO_COLOR(a, r, g, b) \
	(((LED_MANAGER_APPLY_ALPHA(r, a) & LED_MANAGER_LED_SIZE_MASK) << (LED_MANAGER_LED_BIT_DEPTH * 2)) + \
	 ((LED_MANAGER_APPLY_ALPHA(g, a) & LED_MANAGER_LED_SIZE_MASK) << LED_MANAGER_LED_BIT_DEPTH) + \
	 (LED_MANAGER_APPLY_ALPHA(b, a) & LED_MANAGER_LED_SIZE_MASK))

/*
 * The LED manager abstracts the physical LED panels so the rest of the codebase
 * can remain independent of the installation’s layout. All panels use a consistent
 * logical addressing scheme: pixels are indexed right to left and top‑to‑bottom,
 * regardless of their actual physical orientation.
 *
 * Each panel is addressed as if you are facing it directly. For the top and bottom
 * panels, imagine rotating from the front face until you are directly facing them.
 *
 * - The top panel’s pixel 0 is adjacent to the back and left panels.
 * - The bottom panel’s pixel 0 is adjacent to the front and left panels.
 */

// Map an X, Y coordinate to the 1-D panel address space.
#define LED_MANGER_2D_TO_INDEX(x, y) \
    ((y) * LED_MANAGER_PANEL_LED_COUNT_1D + (x))

// Map panel, X, Y coordinate to the 1-D cube address space.
#define LED_MANAGER_P_2D_TO_INDEX(panel, x, y) \
	((panel) * LED_MANAGER_PANEL_LED_COUNT + (y) * LED_MANAGER_PANEL_LED_COUNT_1D  + (x))

// Map panel, 1-D panel address space to 1-D cube address space
#define LED_MANAGER_P_INDEX_TO_INDEX(panel, index) \
	((panel) * LED_MANAGER_PANEL_LED_COUNT + (index))

/**
 * @brief Initialize the LED manager
 *
 * @note The manger initializes in the disabled state. led_manager_enable()
 *       must be called before the panels are accessed.
 */
void led_manager_init(void);

/**
 * @brief Cleanup and being down the LED manager.
 *
 * @note To prevent this function from having to wait for RMT transactions,
 *       it will not turn off the LEDs before bringing down the controller.
 */
void led_manager_uninit(void);

/**
 * @brief Enable the LED panels
 *
 * @note This will acquire a PM lock preventing low power mode from being enabled.
 * @note This will not turn on any of the LEDs unless a color is set.
 *
 * @return
 *      - ESP_OK on success.
 *      - ESP_ERR_NO_MEM if pixel byte buffers could not be allocated.
 *      - ESP_ERR_NOT_FOUND if an RMT TX channel queue is full.
 *      - ESP_FAIL if the cache mutex timed out or the cleanup task could not be created.
 *      - ESP_ERR_INVALID_STATE if called when the manager is already enabled.
 */
esp_err_t led_manager_enable(void);

/**
 * @brief Disable the LED panels
 *
 * @note This will release the PM lock.
 * @note All LEDs will be set to off before RMT channels are disabled.
 *
 * @return
 *      - ESP_ERR_INVALID_STATE if called when the manager is not enabled.
 */
esp_err_t led_manager_disable(void);

/**
 * @brief Set an individual pixel in the LED manager.
 *
 * @param face The face of the cube to edit.
 * @param idx The index of the pixel to edit.
 * @param color The color to set the pixel to.
 * @param force Bypass the cache and force the RMT transaction.
 *
 * @return
 *      - ESP_OK on success.
 *      - ESP_ERR_INVALID_ARG if face >= LED_MANAGER_CUBE_FACE_COUNT or idx >= LED_MANAGER_PANEL_LED_COUNT.
 *      - ESP_ERR_NO_MEM if a pixel byte buffer could not be allocated.
 *      - ESP_ERR_NOT_FOUND if an RMT TX channel queue is full.
 *      - ESP_FAIL if the cache mutex timed out or the cleanup task could not be created.
 */
esp_err_t led_manager_set_pixel(
		enum LED_manager_cube_face face, uint8_t idx, led_color_t color, bool force);

/**
 * @brief Set one whole face of the cube to a color.
 *
 * @param face The face of the cube to edit.
 * @param color The color to set the face to.
 * @param force Bypass the cache and force the RMT transaction.
 *
 * @return
 *      - ESP_OK on success.
 *      - ESP_ERR_INVALID_ARG if face >= LED_MANAGER_CUBE_FACE_COUNT.
 *      - ESP_ERR_NO_MEM if a pixel byte buffer could not be allocated.
 *      - ESP_ERR_NOT_FOUND if an RMT TX channel queue is full.
 *      - ESP_FAIL if the cache mutex timed out or the cleanup task could not be created.
 */
esp_err_t led_manager_set_face(enum LED_manager_cube_face face, led_color_t color, bool force);

/**
 * @brief Set the whole cube to a color.
 *
 * @param color The color to set the cube to.
 * @param force Bypass the cache and force the RMT transaction.
 *
 * @return
 *      - ESP_OK on success.
 *      - ESP_ERR_NO_MEM if a pixel byte buffer could not be allocated.
 *      - ESP_ERR_NOT_FOUND if an RMT TX channel queue is full.
 *      - ESP_FAIL if the cache mutex timed out or the cleanup task could not be created.
 */
esp_err_t led_manager_set_all(led_color_t color, bool force);

/**
 * @brief Set the whole cube using an array.
 *
 * The faces are addressed in the order of the Enums.
 *
 * @param color_array array of LED colors to set.
 * @param force Bypass the cache and force the RMT transaction.
 *
 * @return
 *      - ESP_OK on success.
 *      - ESP_ERR_NO_MEM if a pixel byte buffer could not be allocated.
 *      - ESP_ERR_NOT_FOUND if an RMT TX channel queue is full.
 *      - ESP_FAIL if the cache mutex timed out or the cleanup task could not be created.
 */
esp_err_t led_manager_set_color_from_array(
		led_color_t color_array[static LED_MANAGER_CUBE_LED_COUNT], bool force);

/**
 * @brief Set one face of the cube using an array.
 *
 * @param face The face of the cube to edit.
 * @param color_array array of LED colors to set.
 * @param force Bypass the cache and force the RMT transaction.
 *
 * @return
 *      - ESP_OK on success.
 *      - ESP_ERR_INVALID_ARG if face >= LED_MANAGER_CUBE_FACE_COUNT.
 *      - ESP_ERR_NO_MEM if a pixel byte buffer could not be allocated.
 *      - ESP_ERR_NOT_FOUND if an RMT TX channel queue is full.
 *      - ESP_FAIL if the cache mutex timed out or the cleanup task could not be created.
 */
esp_err_t led_manager_set_face_from_array(
		enum LED_manager_cube_face face,
		led_color_t color_array[static LED_MANAGER_PANEL_LED_COUNT],
		bool force);

#endif //LED_CUBE_CONTROLLER_LED_MANAGER_H
