/**
 * @file mode_bst.c
 * @brief BST (Basic Solid Texture) LED mode.
 *
 * Static mode — renders once on entry, no animation tick.
 * Each face displays one color with a linear brightness gradient:
 *   pixel 0 → 100% brightness, pixel 63 → 25% brightness.
 *
 * @author William Crow
 * @date 2026-04-29
 */

/*
 * @copyright
 * MIT License
 * Copyright (c) 2026 William Crow
 */

#include "LED-manager.h"
#include "led_modes.h"
#include "mode_bst.h"

// Base colors per face, in LED_manager_cube_face enum order.
// { R, G, B } — full brightness values; the gradient scaler reduces these.
static const uint8_t s_face_rgb[LED_MANAGER_CUBE_FACE_COUNT][3] = {
    {255,   0,   0},   // TOP    — Red
    {  0, 255,   0},   // BOTTOM — Green
    {  0,   0, 255},   // LEFT   — Blue
    {255, 255,   0},   // RIGHT  — Yellow
    {255, 255, 255},   // FRONT  — White
    {255, 128,   0},   // BACK   — Orange
};

static void on_enter(void)
{
    led_color_t pixels[LED_MANAGER_PANEL_LED_COUNT];

    for (int face = 0; face < LED_MANAGER_CUBE_FACE_COUNT; face++) {
        uint8_t r = s_face_rgb[face][0];
        uint8_t g = s_face_rgb[face][1];
        uint8_t b = s_face_rgb[face][2];

        for (int i = 0; i < LED_MANAGER_PANEL_LED_COUNT; i++) {
            // Linear interpolation: 1.0 at pixel 0, 0.25 at pixel 63.
            float scale = 1.0f - 0.75f * ((float)i / (LED_MANAGER_PANEL_LED_COUNT - 1));
            pixels[i] = LED_MANGER_RGB_TO_COLOR(
                (uint8_t)(r * scale),
                (uint8_t)(g * scale),
                (uint8_t)(b * scale));
        }

        led_manager_set_face_from_array((enum LED_manager_cube_face)face, pixels, false);
    }
}

const led_mode_t mode_bst = {
    .name     = "bst",
    .on_enter = on_enter,
    .tick     = NULL,
    .on_exit  = NULL,
    .tick_ms  = 0,
};
