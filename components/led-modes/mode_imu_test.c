/**
 * @file mode_imu_test.c
 * @brief IMU-test LED mode.
 *
 * Cycles the full HSV color wheel (one revolution ≈ 8 s at 30 ms/tick) while
 * imposing a vertical brightness gradient that matches the cube's orientation:
 *
 *   Top face    — 25 % of the current hue color, uniform across all 64 pixels.
 *   Bottom face — 100 % of the current hue color, uniform across all 64 pixels.
 *   Side faces  — per-row linear interpolation:
 *                   row 0 (top edge, adjacent to TOP face)    → 25 %
 *                   row 7 (bottom edge, adjacent to BOTTOM face) → 100 %
 *
 * Panel addressing follows the LED manager convention: pixels are indexed
 * right-to-left, top-to-bottom when facing each panel directly.  Row 0 of
 * every side panel is therefore its physical top edge.
 *
 * @author William Crow
 * @date 2026-04-29
 */

/*
 * @copyright
 * MIT License
 * Copyright (c) 2026 William Crow
 */

#include <stdint.h>

#include "LED-manager.h"
#include "led_modes.h"
#include "mode_imu_test.h"

// Hue cycles 0-255 (maps to 0-360 °).  One full revolution = 256 ticks × 30 ms ≈ 7.7 s.
static uint8_t s_hue = 0;

static const enum LED_manager_cube_face SIDE_FACES[] = {
    LED_MANAGER_CUBE_FACE_LEFT,
    LED_MANAGER_CUBE_FACE_RIGHT,
    LED_MANAGER_CUBE_FACE_FRONT,
    LED_MANAGER_CUBE_FACE_BACK,
};
#define SIDE_FACE_COUNT  (sizeof(SIDE_FACES) / sizeof(SIDE_FACES[0]))

// ── HSV → RGB conversion ──────────────────────────────────────────────────────
// h: 0-255 (wraps 0-360°), s and v implicitly 255 (fully saturated, full value).
// The caller scales the output channels down for dimming.

static void hue_to_rgb(uint8_t h, uint8_t *r, uint8_t *g, uint8_t *b)
{
    uint8_t region    = h / 43;          // 0-5  (each covers 60°)
    uint8_t remainder = (h % 43) * 6;    // 0-255, linear within the region

    uint8_t q = 255 - remainder;         // falling edge
    uint8_t t = remainder;               // rising edge

    switch (region) {
        case 0:  *r = 255; *g = t;   *b = 0;   break;
        case 1:  *r = q;   *g = 255; *b = 0;   break;
        case 2:  *r = 0;   *g = 255; *b = t;   break;
        case 3:  *r = 0;   *g = q;   *b = 255; break;
        case 4:  *r = t;   *g = 0;   *b = 255; break;
        default: *r = 255; *g = 0;   *b = q;   break;
    }
}

// ── Core render ───────────────────────────────────────────────────────────────

static void render(uint8_t hue)
{
    uint8_t r, g, b;
    hue_to_rgb(hue, &r, &g, &b);

    // Top face — uniform 25 %.
    led_manager_set_face(LED_MANAGER_CUBE_FACE_TOP,
                         LED_MANGER_RGB_TO_COLOR(r / 4, g / 4, b / 4),
                         false);

    // Bottom face — uniform 100 %.
    led_manager_set_face(LED_MANAGER_CUBE_FACE_BOTTOM,
                         LED_MANGER_RGB_TO_COLOR(r, g, b),
                         false);

    // Side faces — gradient per row.  Build the pixel array once; all four
    // sides share the same brightness profile for the current hue.
    led_color_t pixels[LED_MANAGER_PANEL_LED_COUNT];
    for (int i = 0; i < LED_MANAGER_PANEL_LED_COUNT; i++) {
        int row = i / LED_MANAGER_PANEL_LED_COUNT_1D;  // 0 (top) … 7 (bottom)

        // scale: 0.25 at row 0 → 1.0 at row 7
        float scale = 0.25f + 0.75f * ((float)row / (LED_MANAGER_PANEL_LED_COUNT_1D - 1));
        pixels[i] = LED_MANGER_RGB_TO_COLOR(
            (uint8_t)(r * scale),
            (uint8_t)(g * scale),
            (uint8_t)(b * scale));
    }

    for (size_t f = 0; f < SIDE_FACE_COUNT; f++) {
        led_manager_set_face_from_array(SIDE_FACES[f], pixels, false);
    }
}

// ── Mode callbacks ────────────────────────────────────────────────────────────

static void on_enter(void)
{
    s_hue = 0;
    render(s_hue);
}

static void tick(void)
{
    s_hue++;
    render(s_hue);
}

const led_mode_t mode_imu_test = {
    .name     = "imu-test",
    .on_enter = on_enter,
    .tick     = tick,
    .on_exit  = NULL,
    .tick_ms  = 30,
};