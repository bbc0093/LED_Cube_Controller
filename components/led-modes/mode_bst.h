/**
 * @file mode_bst.h
 * @brief BST (Basic Solid Texture) LED mode declaration.
 *
 * Each cube face is rendered as a single color with a linear brightness
 * gradient: 100% at pixel 0, 25% at pixel 63.
 *
 * Face → color mapping:
 *   TOP    = Red     BOTTOM = Green   LEFT = Blue
 *   RIGHT  = Yellow  FRONT  = White   BACK = Orange
 *
 * @author William Crow
 * @date 2026-04-29
 */

/*
 * @copyright
 * MIT License
 * Copyright (c) 2026 William Crow
 */

#ifndef LED_CUBE_CONTROLLER_MODE_BST_H
#define LED_CUBE_CONTROLLER_MODE_BST_H

#include "led_modes.h"

extern const led_mode_t mode_bst;

#endif // LED_CUBE_CONTROLLER_MODE_BST_H
