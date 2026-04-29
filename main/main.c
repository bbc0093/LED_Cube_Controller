/**
 * @file main.c
 * @brief Main entry point for the LED Cube Controller.
 *
 * @author William Crow
 * @date 4/5/2026
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

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"

#include "peripherals.h"

#include "wifi-manager.h"
#include "LED-manager.h"
#include "icm42688p.h"
#include "mcp9701a.h"
#include "tmp102.h"
#include "max17049.h"
#include "bq25731.h"

static const char *TAG = "main";

// Shared I2C bus handle — created once and passed to all I2C drivers.
static i2c_master_bus_handle_t i2c_bus;

// Task handles used by ISRs to wake their respective handler tasks.
static TaskHandle_t s_nalrt_task;
static TaskHandle_t s_chrg_ok_task;
static TaskHandle_t s_tmp102_alert_task;
static TaskHandle_t s_prochot_task;
static TaskHandle_t s_imu_drdy_task;

// ── MAX17049 nALRT interrupt ──────────────────────────────────────────────────

// ISR: fires on the falling edge of nALRT (active-low).
// Unblocks the handler task; all I2C work happens there.
static void IRAM_ATTR nalrt_isr(void *arg)
{
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(s_nalrt_task, &woken);
    portYIELD_FROM_ISR(woken);
}

// Handler task: reads SOC/voltage and clears the alert flags so the nALRT
// pin de-asserts.
static void nalrt_task(void *arg)
{
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        max17049_data_t data;
        if (max17049_read(&data) == ESP_OK) {
            ESP_LOGW(TAG, "MAX17049 low-battery alert: SOC %.1f %%, %.2f V",
                     data.soc_pct, data.voltage_v);
        }
        ESP_ERROR_CHECK(max17049_clear_alert());
    }
}

// ── BQ25731 CHRG_OK interrupt ─────────────────────────────────────────────────

// ISR: fires on either edge of CHRG_OK (open-drain active-high).
static void IRAM_ATTR chrg_ok_isr(void *arg)
{
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(s_chrg_ok_task, &woken);
    portYIELD_FROM_ISR(woken);
}

// Handler task: reads the pin level and charger status to determine whether
// charging just started, stopped cleanly, or a fault occurred.
static void chrg_ok_task(void *arg)
{
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        int level = gpio_get_level(PERIPH_BQ25731_CHRG_OK_PIN);

        bq25731_status_t status;
        if (bq25731_read_status(&status) != ESP_OK) {
            continue;
        }

        if (level) {
            ESP_LOGI(TAG, "BQ25731 CHRG_OK asserted: charging active (fast=%d)",
                     status.in_fchrg);
        } else {
            if (status.fault) {
                ESP_LOGE(TAG, "BQ25731 CHRG_OK de-asserted: fault detected");
            } else if (!status.ac_present) {
                ESP_LOGI(TAG, "BQ25731 CHRG_OK de-asserted: input removed");
            } else {
                ESP_LOGI(TAG, "BQ25731 CHRG_OK de-asserted: charge complete or suspended");
            }
        }
    }
}

// ── TMP102 ALERT interrupt ────────────────────────────────────────────────────

// ISR: fires on the falling edge of ALERT (active-low open-drain).
static void IRAM_ATTR tmp102_alert_isr(void *arg)
{
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(s_tmp102_alert_task, &woken);
    portYIELD_FROM_ISR(woken);
}

// Handler task: reads and logs the temperature that crossed a limit.
// NOTE: the ALERT pin will not assert until T_HIGH/T_LOW thresholds are
// written to the TMP102 configuration registers.
static void tmp102_alert_task(void *arg)
{
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        float temp_c;
        if (tmp102_read_temperature(&temp_c) == ESP_OK) {
            ESP_LOGW(TAG, "TMP102 alert: %.4f °C", temp_c);
        }
    }
}

// ── BQ25731 PROCHOT# interrupt ────────────────────────────────────────────────

// ISR: fires on the falling edge of PROCHOT# (active-low open-drain).
static void IRAM_ATTR prochot_isr(void *arg)
{
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(s_prochot_task, &woken);
    portYIELD_FROM_ISR(woken);
}

// Handler task: reads charger status and logs the protection fault.
static void prochot_task(void *arg)
{
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        bq25731_status_t status;
        if (bq25731_read_status(&status) == ESP_OK) {
            ESP_LOGE(TAG, "BQ25731 PROCHOT# asserted: ac=%d fchrg=%d vindpm=%d idpm=%d fault=%d",
                     status.ac_present, status.in_fchrg,
                     status.in_vindpm, status.in_idpm, status.fault);
        }
    }
}

// ── ICM-42688-P INT1 data-ready interrupt ─────────────────────────────────────

// ISR: fires on the rising edge of INT1 (active-high push-pull) when a new
// sensor sample is ready.
static void IRAM_ATTR imu_drdy_isr(void *arg)
{
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(s_imu_drdy_task, &woken);
    portYIELD_FROM_ISR(woken);
}

// Handler task: reads and logs the latest IMU sample at DEBUG level.
// Upgrade to ESP_LOGI or post to a queue when the data is needed by other tasks.
static void imu_drdy_task(void *arg)
{
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        icm42688p_data_t data;
        if (icm42688p_read(&data) == ESP_OK) {
            ESP_LOGD(TAG,
                     "IMU: ax=%.3f ay=%.3f az=%.3f gx=%.3f gy=%.3f gz=%.3f T=%.1f",
                     data.accel_x, data.accel_y, data.accel_z,
                     data.gyro_x,  data.gyro_y,  data.gyro_z, data.temp);
        }
    }
}

// ── Peripheral initialisation helpers ────────────────────────────────────────

static void init_i2c_bus(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port                     = PERIPH_I2C_PORT,
        .sda_io_num                   = PERIPH_I2C_SDA_PIN,
        .scl_io_num                   = PERIPH_I2C_SCL_PIN,
        .clk_source                   = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt            = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &i2c_bus));
    ESP_LOGI(TAG, "I2C bus created (SDA=%d SCL=%d %lu Hz)",
             PERIPH_I2C_SDA_PIN, PERIPH_I2C_SCL_PIN, PERIPH_I2C_SPEED_HZ);
}

static void init_led_manager(void)
{
    led_manager_config_t cfg = {
        .top_pin    = PERIPH_LED_TOP_PIN,
        .bottom_pin = PERIPH_LED_BOTTOM_PIN,
        .left_pin   = PERIPH_LED_LEFT_PIN,
        .right_pin  = PERIPH_LED_RIGHT_PIN,
        .front_pin  = PERIPH_LED_FRONT_PIN,
        .back_pin   = PERIPH_LED_BACK_PIN,
        .pwr_en_pin = PERIPH_LED_PWR_EN_PIN,
    };
    led_manager_init(&cfg);
    ESP_ERROR_CHECK(led_manager_enable());
    ESP_LOGI(TAG, "LED manager ready");
}

static void init_imu(void)
{
    icm42688p_config_t cfg = {
        .bus_handle   = i2c_bus,
        .address      = PERIPH_IMU_ADDRESS,
        .scl_speed_hz = PERIPH_I2C_SPEED_HZ,
        .accel_fs     = PERIPH_IMU_ACCEL_FS,
        .gyro_fs      = PERIPH_IMU_GYRO_FS,
        .odr          = PERIPH_IMU_ODR,
    };
    ESP_ERROR_CHECK(icm42688p_init(&cfg));
    ESP_LOGI(TAG, "ICM-42688-P ready");
}

static void init_thermistors(void)
{
    mcp9701a_sensor_cfg_t sensors[PERIPH_THERM_COUNT] = {
        {.adc_unit = PERIPH_THERM_BAT0_ADC_UNIT,   .adc_channel = PERIPH_THERM_BAT0_ADC_CH},
        {.adc_unit = PERIPH_THERM_BAT1_ADC_UNIT,   .adc_channel = PERIPH_THERM_BAT1_ADC_CH},
        {.adc_unit = PERIPH_THERM_FRONT_ADC_UNIT,  .adc_channel = PERIPH_THERM_FRONT_ADC_CH},
        {.adc_unit = PERIPH_THERM_BACK_ADC_UNIT,   .adc_channel = PERIPH_THERM_BACK_ADC_CH},
        {.adc_unit = PERIPH_THERM_TOP_ADC_UNIT,    .adc_channel = PERIPH_THERM_TOP_ADC_CH},
        {.adc_unit = PERIPH_THERM_BOTTOM_ADC_UNIT, .adc_channel = PERIPH_THERM_BOTTOM_ADC_CH},
        {.adc_unit = PERIPH_THERM_LEFT_ADC_UNIT,   .adc_channel = PERIPH_THERM_LEFT_ADC_CH},
        {.adc_unit = PERIPH_THERM_RIGHT_ADC_UNIT,  .adc_channel = PERIPH_THERM_RIGHT_ADC_CH},
    };
    ESP_ERROR_CHECK(mcp9701a_init(sensors, PERIPH_THERM_COUNT));
    ESP_LOGI(TAG, "MCP9701A thermistors ready (%d sensor(s))", PERIPH_THERM_COUNT);
}

static void init_temp_sensor(void)
{
    tmp102_config_t cfg = {
        .bus_handle    = i2c_bus,
        .address       = PERIPH_TMP102_ADDRESS,
        .scl_speed_hz  = PERIPH_I2C_SPEED_HZ,
        .conv_rate     = TMP102_CONV_RATE_4HZ,
        .extended_mode = false,
    };
    ESP_ERROR_CHECK(tmp102_init(&cfg));
    ESP_LOGI(TAG, "TMP102 ready");
}

static void init_fuel_gauge(void)
{
    max17049_config_t cfg = {
        .bus_handle          = i2c_bus,
        .scl_speed_hz        = PERIPH_I2C_SPEED_HZ,
        .alert_threshold_pct = PERIPH_MAX17049_ALERT_PCT,
        .rcomp               = PERIPH_MAX17049_RCOMP,
    };
    ESP_ERROR_CHECK(max17049_init(&cfg));
    ESP_LOGI(TAG, "MAX17049G ready");
}

static void init_charger(void)
{
    bq25731_config_t cfg = {
        .bus_handle        = i2c_bus,
        .scl_speed_hz      = PERIPH_I2C_SPEED_HZ,
        .rsr_mohm          = PERIPH_BQ25731_RSR_MOHM,
        .rac_mohm          = PERIPH_BQ25731_RAC_MOHM,
        .charge_voltage_mv = PERIPH_BQ25731_CHARGE_MV,
        .charge_current_ma = PERIPH_BQ25731_CHARGE_MA,
        .input_current_ma  = PERIPH_BQ25731_INPUT_MA,
    };
    ESP_ERROR_CHECK(bq25731_init(&cfg));
    ESP_LOGI(TAG, "BQ25731 ready (%u mV / %u mA, input limit %u mA)",
             PERIPH_BQ25731_CHARGE_MV, PERIPH_BQ25731_CHARGE_MA, PERIPH_BQ25731_INPUT_MA);
}

static void init_nalrt_interrupt(void)
{
    if (PERIPH_MAX17049_NALRT_PIN == GPIO_NUM_NC) {
        ESP_LOGW(TAG, "MAX17049 nALRT pin not configured — alert interrupt disabled");
        return;
    }

    xTaskCreate(nalrt_task, "nalrt", 4096, NULL, 6, &s_nalrt_task);

    gpio_config_t io_cfg = {
        .pin_bit_mask = (1ULL << PERIPH_MAX17049_NALRT_PIN),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,    // External pull-up on board
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_NEGEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_cfg));
    ESP_ERROR_CHECK(gpio_isr_handler_add(PERIPH_MAX17049_NALRT_PIN, nalrt_isr, NULL));
    ESP_LOGI(TAG, "MAX17049 nALRT interrupt armed on GPIO %d", PERIPH_MAX17049_NALRT_PIN);
}

static void init_chrg_ok_interrupt(void)
{
    if (PERIPH_BQ25731_CHRG_OK_PIN == GPIO_NUM_NC) {
        ESP_LOGW(TAG, "BQ25731 CHRG_OK pin not configured — charge-status interrupt disabled");
        return;
    }

    xTaskCreate(chrg_ok_task, "chrg_ok", 4096, NULL, 6, &s_chrg_ok_task);

    gpio_config_t io_cfg = {
        .pin_bit_mask = (1ULL << PERIPH_BQ25731_CHRG_OK_PIN),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,    // External pull-up on board
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_ANYEDGE,      // Detect both start and stop of charging
    };
    ESP_ERROR_CHECK(gpio_config(&io_cfg));
    ESP_ERROR_CHECK(gpio_isr_handler_add(PERIPH_BQ25731_CHRG_OK_PIN, chrg_ok_isr, NULL));
    ESP_LOGI(TAG, "BQ25731 CHRG_OK interrupt armed on GPIO %d", PERIPH_BQ25731_CHRG_OK_PIN);
}

static void init_tmp102_interrupt(void)
{
    if (PERIPH_TMP102_ALERT_PIN == GPIO_NUM_NC) {
        ESP_LOGW(TAG, "TMP102 ALERT pin not configured — alert interrupt disabled");
        return;
    }

    xTaskCreate(tmp102_alert_task, "tmp102_alert", 2048, NULL, 6, &s_tmp102_alert_task);

    gpio_config_t io_cfg = {
        .pin_bit_mask = (1ULL << PERIPH_TMP102_ALERT_PIN),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,    // External pull-up on board
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_NEGEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_cfg));
    ESP_ERROR_CHECK(gpio_isr_handler_add(PERIPH_TMP102_ALERT_PIN, tmp102_alert_isr, NULL));
    ESP_LOGI(TAG, "TMP102 ALERT interrupt armed on GPIO %d", PERIPH_TMP102_ALERT_PIN);
}

static void init_prochot_interrupt(void)
{
    if (PERIPH_BQ25731_PROCHOT_PIN == GPIO_NUM_NC) {
        ESP_LOGW(TAG, "BQ25731 PROCHOT# pin not configured — protection-fault interrupt disabled");
        return;
    }

    xTaskCreate(prochot_task, "prochot", 2048, NULL, 7, &s_prochot_task);

    gpio_config_t io_cfg = {
        .pin_bit_mask = (1ULL << PERIPH_BQ25731_PROCHOT_PIN),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,    // External pull-up on board
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_NEGEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_cfg));
    ESP_ERROR_CHECK(gpio_isr_handler_add(PERIPH_BQ25731_PROCHOT_PIN, prochot_isr, NULL));
    ESP_LOGI(TAG, "BQ25731 PROCHOT# interrupt armed on GPIO %d", PERIPH_BQ25731_PROCHOT_PIN);
}

static void init_imu_drdy_interrupt(void)
{
    if (PERIPH_IMU_INT_PIN == GPIO_NUM_NC) {
        ESP_LOGW(TAG, "IMU INT pin not configured — data-ready interrupt disabled");
        return;
    }

    xTaskCreate(imu_drdy_task, "imu_drdy", 4096, NULL, 5, &s_imu_drdy_task);

    gpio_config_t io_cfg = {
        .pin_bit_mask = (1ULL << PERIPH_IMU_INT_PIN),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,    // Active-high push-pull — no pull needed
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_POSEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_cfg));
    ESP_ERROR_CHECK(gpio_isr_handler_add(PERIPH_IMU_INT_PIN, imu_drdy_isr, NULL));
    ESP_LOGI(TAG, "IMU INT1 data-ready interrupt armed on GPIO %d", PERIPH_IMU_INT_PIN);
}

// ── Entry point ───────────────────────────────────────────────────────────────

void app_main(void)
{
    // NVS is required by the Wi-Fi driver.
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Bring up peripherals in dependency order.
    init_i2c_bus();
    init_led_manager();
    init_imu();
    init_thermistors();
    init_temp_sensor();
    init_fuel_gauge();
    init_charger();

    // Install the shared GPIO ISR service once before arming any pin interrupts.
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    init_nalrt_interrupt();
    init_chrg_ok_interrupt();
    init_tmp102_interrupt();
    init_prochot_interrupt();
    init_imu_drdy_interrupt();

    ESP_LOGI(TAG, "all peripherals initialized");

    wifi_init_sta();
}