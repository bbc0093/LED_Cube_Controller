/**
 * @file LED-encoder.h
 * @brief Library for driving WS2812B LEDs
 *
 * Heavily based on espressif/led_strip example
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

#ifndef LED_CUBE_CONTROLLER_LED_ENCODER_H
#define LED_CUBE_CONTROLLER_LED_ENCODER_H
#include <stdint.h>
#include "driver/rmt_encoder.h"

/**
 * @brief Create RMT encoder for encoding LED pixels into RMT symbols
 *
 * @param[in] clock_resolution_hz the refresh rate in Hz
 * @param[out] ret_encoder Returned encoder handle
 * @return
 *      - ESP_ERR_INVALID_ARG for any invalid arguments
 *      - ESP_ERR_NO_MEM out of memory when creating led strip encoder
 *      - ESP_OK if creating encoder successfully
 */
esp_err_t led_encoder_new_rmt(
    uint32_t clock_resolution_hz,
    rmt_encoder_handle_t* ret_encoder);

/**
 * @brief Clean up the object created by led_encoder_new_rmt
 *
 * @param encoder the object to clean up
 */
void led_encoder_cleanup_rmt(rmt_encoder_handle_t *encoder);
#endif //LED_CUBE_CONTROLLER_LED_ENCODER_H
