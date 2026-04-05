/**
 * @file LED-manager.c
 * @brief Provide an interface and abstraction layer for controlling the
 *        faces of the LED Cube.
 *
 * @author William Crow
 * @date 4/5/2026
 * @last_modified 4/5/2026
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


#define RMT_LED_PANEL_RESOLUTION_HZ (10*1000*1000) // 10MHz resolution, 1 tick = 0.1us (led strip needs a high resolution)
#define RMT_LED_PANEL_BOT_GPIO_NUM   0
#define RMT_LED_PANEL_TOP_GPIO_NUM   0
#define RMT_LED_PANEL_LEFT_GPIO_NUM  0
#define RMT_LED_PANEL_RIGHT_GPIO_NUM 0
#define RMT_LED_PANEL_FRONT_GPIO_NUM 0
#define RMT_LED_PANEL_BACK_GPIO_NUM  0

static const char *TAG = "LED_manager";

#define LED_PER_PANEL 64