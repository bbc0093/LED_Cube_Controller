/**
 * @file led_modes.h
 * @brief LED mode framework — register modes and switch between them at runtime.
 *
 * Each mode is a static `led_mode_t` struct with function pointers.
 * The mode manager owns a single FreeRTOS task that serialises all LED writes
 * and drives animation ticks.
 *
 * Typical usage
 * -------------
 * 1. Implement a mode in its own .c file (see mode_bst.c as an example).
 * 2. Declare `extern const led_mode_t mode_xxx;` in a matching .h file.
 * 3. In main.c: call led_mode_manager_register() for each mode in the same
 *    order as the led_mode_id_t enum, then call led_mode_manager_start().
 * 4. Call led_mode_manager_set_mode() at any time from any task to switch.
 *
 * IMPORTANT: The led_mode_id_t enum values are indices into the registration
 * order.  LED_MODE_BST = 0 must be the first mode registered, LED_MODE_IMU_TEST
 * = 1 must be the second, and so on.  Mismatched order causes wrong modes to
 * activate.
 *
 * @author William Crow
 * @date 2026-04-29
 */

/*
 * @copyright
 * MIT License
 * Copyright (c) 2026 William Crow
 */

#ifndef LED_CUBE_CONTROLLER_LED_MODES_H
#define LED_CUBE_CONTROLLER_LED_MODES_H

#include <stdint.h>
#include "esp_err.h"

// ── Mode ID enum ──────────────────────────────────────────────────────────────
// Values are registration-order indices.  LED_MODE_COUNT is a sentinel equal
// to the total number of modes; it is NOT a valid mode to activate.

typedef enum {
    LED_MODE_BST       = 0,
    LED_MODE_IMU_TEST  = 1,
    LED_MODE_COUNT          // must equal the number of registered modes
} led_mode_id_t;

// ── Mode descriptor ───────────────────────────────────────────────────────────

typedef struct {
    const char *name;

    // Called once when this mode becomes active.  Must render the initial frame.
    // May be NULL (mode will be silent until its first tick).
    void (*on_enter)(void);

    // Called every tick_ms milliseconds while this mode is active.
    // NULL for static modes that only render in on_enter.
    void (*tick)(void);

    // Called once just before switching away from this mode.
    // NULL if no cleanup is needed.
    void (*on_exit)(void);

    // Tick interval in milliseconds.  Ignored when tick is NULL.
    uint32_t tick_ms;
} led_mode_t;

// ── Mode manager API ──────────────────────────────────────────────────────────

/**
 * @brief Register a mode.  Call before led_mode_manager_start(), in the same
 *        order as the led_mode_id_t enum values.
 */
esp_err_t led_mode_manager_register(const led_mode_t *mode);

/**
 * @brief Start the mode manager and activate LED_MODE_BST (index 0).
 *
 * Creates the command queue and spawns the manager task.
 */
esp_err_t led_mode_manager_start(void);

/**
 * @brief Switch to a mode by its enum ID.  Safe to call from any task.
 */
esp_err_t led_mode_manager_set_mode(led_mode_id_t mode);

/** @brief ID of the currently active mode. */
led_mode_id_t led_mode_manager_current_id(void);

/** @brief Name of the currently active mode, or NULL before start. */
const char *led_mode_manager_current_name(void);

/**
 * @brief Name of any registered mode by ID, or NULL if out of range.
 */
const char *led_mode_manager_get_name(led_mode_id_t mode);

/** @brief Number of registered modes. */
int led_mode_manager_mode_count(void);

#endif // LED_CUBE_CONTROLLER_LED_MODES_H