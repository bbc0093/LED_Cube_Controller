/**
 * @file led_modes.c
 * @brief LED mode manager implementation.
 *
 * @author William Crow
 * @date 2026-04-29
 */

/*
 * @copyright
 * MIT License
 * Copyright (c) 2026 William Crow
 */

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "led_modes.h"

#define LED_MODE_MAX_MODES  16

static const char *TAG = "led_modes";

static const led_mode_t  *s_modes[LED_MODE_MAX_MODES];
static int                s_mode_count = 0;
static volatile led_mode_id_t s_current = LED_MODE_BST;
static QueueHandle_t      s_cmd_queue  = NULL;
static TaskHandle_t       s_task       = NULL;

// ── Manager task ──────────────────────────────────────────────────────────────

static void mode_manager_task(void *arg)
{
    for (;;) {
        const led_mode_t *cur = s_modes[s_current];

        // Static modes (tick = NULL) sleep until a mode-switch command arrives.
        TickType_t timeout = (cur->tick && cur->tick_ms > 0)
                             ? pdMS_TO_TICKS(cur->tick_ms)
                             : portMAX_DELAY;

        uint8_t next_idx;
        if (xQueueReceive(s_cmd_queue, &next_idx, timeout) == pdTRUE) {
            // Mode switch command.
            if (cur->on_exit) {
                cur->on_exit();
            }
            s_current = (led_mode_id_t)next_idx;
            cur = s_modes[s_current];
            ESP_LOGI(TAG, "switched to mode \"%s\" (%d)", cur->name, (int)s_current);
            if (cur->on_enter) {
                cur->on_enter();
            }
        } else {
            // Tick timeout — drive animation.
            if (cur->tick) {
                cur->tick();
            }
        }
    }
}

// ── Public API ────────────────────────────────────────────────────────────────

esp_err_t led_mode_manager_register(const led_mode_t *mode)
{
    ESP_RETURN_ON_FALSE(mode != NULL, ESP_ERR_INVALID_ARG, TAG, "mode must not be NULL");
    ESP_RETURN_ON_FALSE(s_mode_count < LED_MODE_MAX_MODES, ESP_ERR_NO_MEM,
                        TAG, "mode registry full (%d max)", LED_MODE_MAX_MODES);

    s_modes[s_mode_count++] = mode;
    ESP_LOGI(TAG, "registered mode [%d] \"%s\"", s_mode_count - 1, mode->name);
    return ESP_OK;
}

esp_err_t led_mode_manager_start(void)
{
    ESP_RETURN_ON_FALSE(s_mode_count > 0, ESP_ERR_INVALID_STATE,
                        TAG, "no modes registered");
    ESP_RETURN_ON_FALSE(s_task == NULL, ESP_ERR_INVALID_STATE,
                        TAG, "already started");

    s_cmd_queue = xQueueCreate(4, sizeof(uint8_t));
    ESP_RETURN_ON_FALSE(s_cmd_queue != NULL, ESP_ERR_NO_MEM,
                        TAG, "failed to create command queue");

    // Activate the first mode synchronously before the task starts so the
    // initial render happens before app_main returns.
    s_current = LED_MODE_BST;
    ESP_LOGI(TAG, "activating mode \"%s\"", s_modes[0]->name);
    if (s_modes[0]->on_enter) {
        s_modes[0]->on_enter();
    }

    BaseType_t ok = xTaskCreate(mode_manager_task, "led_mode", 4096, NULL, 4, &s_task);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM,
                        TAG, "failed to create manager task");

    return ESP_OK;
}

esp_err_t led_mode_manager_set_mode(led_mode_id_t mode)
{
    ESP_RETURN_ON_FALSE((int)mode < s_mode_count, ESP_ERR_INVALID_ARG,
                        TAG, "mode id %d out of range (%d registered)", (int)mode, s_mode_count);

    uint8_t idx = (uint8_t)mode;
    if (xQueueSend(s_cmd_queue, &idx, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "mode switch command queue full");
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

led_mode_id_t led_mode_manager_current_id(void)
{
    return s_current;
}

const char *led_mode_manager_current_name(void)
{
    if (s_mode_count == 0) return NULL;
    return s_modes[s_current]->name;
}

const char *led_mode_manager_get_name(led_mode_id_t mode)
{
    if ((int)mode >= s_mode_count) return NULL;
    return s_modes[mode]->name;
}

int led_mode_manager_mode_count(void)
{
    return s_mode_count;
}