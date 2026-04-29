/**
 * @file LED-manager.c
 * @brief Provide an interface and abstraction layer for controlling the
 *        faces of the LED Cube.
 *
 * @author William Crow
 * @date 4/5/2026
 * @last_modified 2026-04-28 00:14:44
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

#include "esp_check.h"
#include "esp_log.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_types.h"
#include "driver/gpio.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "LED-manager.h"
#include "LED-encoder.h"

#define LED_TASK_PRIO 5 //TODO: Revisit this when I have a better picture. Move to a central profile?

#define RMT_LED_PANEL_RESOLUTION_HZ (10*1000*1000) // 10MHz resolution, 1 tick = 0.1us (led strip needs a high resolution)


// Guesstimate of ~10ms to update all the LEDs on the cube. (Done in parallel so probably faster.)
// 50ms should be plenty of time for the mutex lock
#define LED_MANAGER_MUTEX_TIMEOUT_TICKS ((50 * CONFIG_FREERTOS_HZ) / 1000)
#define LED_MANAGER_RMT_WRITE_TIMEOUT LED_MANAGER_MUTEX_TIMEOUT_TICKS

static const char *TAG = "LED_manager";

struct LED_manager {
	volatile bool inited;

	volatile bool enabled;

	gpio_num_t face_pins[LED_MANAGER_CUBE_FACE_COUNT];
	gpio_num_t pwr_en_pin;

	rmt_channel_handle_t handlers[LED_MANAGER_CUBE_FACE_COUNT];

	rmt_encoder_handle_t encoder;

	// For now assume that the handlers do not need a lock, because they are not
	// being dynamically configured. The init and enabled bools are volatile so
	// that cross-task reads always fetch from memory rather than a cached register.
	SemaphoreHandle_t cache_mutex_lock;
	led_color_t pixel_cache[LED_MANAGER_CUBE_LED_COUNT];

	// Tracks in-flight rmt_tx_cleanup tasks so led_manager_uninit can drain
	// them before deleting channel handles.
	volatile int      pending_cleanup_tasks;
	portMUX_TYPE      cleanup_spinlock;
	SemaphoreHandle_t cleanup_drained_sem;
};

static struct LED_manager led_manager = {0};

/**
 * @brief Map LED Cube face enum to ESP32 GPIO.
 *
 * @param panel_enum The panel enum
 *
 * @return the gpio_num_t of panel's com channel.
 */
static gpio_num_t panel_enum_to_gpio_num(enum LED_manager_cube_face panel_enum)
{
	if (panel_enum < LED_MANAGER_CUBE_FACE_COUNT) {
		return led_manager.face_pins[panel_enum];
	}
	ESP_LOGE(TAG, "Invalid panel_enum: %d", panel_enum);
	return GPIO_NUM_NC;
}

/**
 * @brief Write values into the LED pixel cache.
 *
 * @param offset Zero-based index into the flat pixel cache at which to begin writing.
 * @param length Number of pixels to copy from values.
 * @param values Source array of at least length pixels.
 *
 * @note Aborts if offset or offset + length falls outside the valid
 *       cache range.
 */
static void update_led_cache(
		const int16_t offset,
		const uint16_t length,
		const led_color_t *values)
{
	if (unlikely(offset < 0 || offset + length > LED_MANAGER_CUBE_LED_COUNT)) {
		ESP_LOGE(TAG, "Invalid offset or length: %d, %d", offset, length);
		abort();
	}

	memcpy(led_manager.pixel_cache + offset, values, length * sizeof(led_color_t));
}

/**
 * @brief Compare a region of the LED pixel cache against a value array.
 *
 * @param offset Zero-based index into the flat pixel cache to begin comparing.
 * @param length Number of pixels to compare.
 * @param values Array of at least length pixels to compare against.
 *
 * @return true  if the cache region matches values (no update needed).
 * @return false if the cache region differs from values (update required).
 *
 * @note Aborts if offset or offset + length falls outside the valid
 *       cache range.
 */
[[maybe_unused]] static bool led_cache_check(
		const int16_t offset,
		const uint16_t length,
		const led_color_t *values)
{
	if (unlikely(offset < 0 || offset + length > LED_MANAGER_CUBE_LED_COUNT)) {
		ESP_LOGE(TAG, "Invalid offset or length: %d, %d", offset, length);
		abort();
	}

	return memcmp(led_manager.pixel_cache + offset, values, length * sizeof(led_color_t)) == 0;
}

/**
 * @brief Convert led colors to bytes that can be written to the LEDs.
 *
 * @note  The LED expect colors in GRB.
 *
 * @param colors the LED manager colors to convert
 * @param len the length of the colors array
 * @param bytes the bytes array populated with values to write.
 *           - This will be len * 3 bytes long
 *           - This array is allocated by this function and must be freed
 *           - For safety, this must be null when passed to the function.
 * @return
 *      - ESP_ERR_INVALID_ARG for any invalid arguments
*       - ESP_ERR_NO_MEM out of memory when creating bytes array
 *      - ESP_OK if creating the array successfully
 */
static esp_err_t led_manager_led_color_to_bytes(
		const led_color_t *colors, const uint16_t len, uint8_t **bytes)
{
	if (unlikely(colors == nullptr || *bytes != nullptr)) {
		ESP_LOGE(TAG, "Invalid arguments for %s", __func__);
		return ESP_ERR_INVALID_ARG;
	}

	*bytes = malloc(len * 3);
	if (unlikely(*bytes == nullptr)) {
		ESP_LOGE(TAG, "Failed to allocate %d bytes for %s", len * 3, __func__);
		return ESP_ERR_NO_MEM;
	}

	static_assert(LED_MANAGER_LED_BIT_DEPTH == 8, "This needs to be updated if bit depth is not 8");
	for (int i = 0, b_i = 0; i < len; i++, b_i += 3) {
		// No need to mask atm, because we are saving in a uint8_t array.
		(*bytes)[b_i + 0] = colors[i] >> LED_MANAGER_LED_BIT_DEPTH; // G
		(*bytes)[b_i + 1] = colors[i] >> (LED_MANAGER_LED_BIT_DEPTH * 2); // R
		(*bytes)[b_i + 2] = colors[i]; // B
	}

	return ESP_OK;
}

void led_manager_init(const led_manager_config_t *config)
{
	if (unlikely(led_manager.inited)) {
		ESP_LOGE(TAG, "Tried to init LED manager when already inited");
		abort();
	}

	led_manager.face_pins[LED_MANAGER_CUBE_FACE_TOP]    = config->top_pin;
	led_manager.face_pins[LED_MANAGER_CUBE_FACE_BOTTOM] = config->bottom_pin;
	led_manager.face_pins[LED_MANAGER_CUBE_FACE_LEFT]   = config->left_pin;
	led_manager.face_pins[LED_MANAGER_CUBE_FACE_RIGHT]  = config->right_pin;
	led_manager.face_pins[LED_MANAGER_CUBE_FACE_FRONT]  = config->front_pin;
	led_manager.face_pins[LED_MANAGER_CUBE_FACE_BACK]   = config->back_pin;
	led_manager.pwr_en_pin = config->pwr_en_pin;

	ESP_LOGI(TAG, "Create RMT TX channels");
	for (enum LED_manager_cube_face face = 0; face < LED_MANAGER_CUBE_FACE_COUNT; face++) {
		rmt_tx_channel_config_t tx_chan_config = {
			.clk_src           = RMT_CLK_SRC_DEFAULT, // select source clock
			.gpio_num          = panel_enum_to_gpio_num(face),
			.mem_block_symbols = 256, // increase the block size can make the LED less flickering
			.resolution_hz     = RMT_LED_PANEL_RESOLUTION_HZ,
			// TODO: Check if this depth is sufficient.
			.trans_queue_depth = 4, // set the number of transactions that can be pending in the background
			.flags.with_dma    = true, // offload to the DMA controller
		};
		ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_chan_config, &led_manager.handlers[face]));
	}
	ESP_LOGI(TAG, "Install led strip encoder");
	ESP_ERROR_CHECK(led_encoder_new_rmt(RMT_LED_PANEL_RESOLUTION_HZ, &led_manager.encoder));

	ESP_LOGI(TAG, "Setup LED 5V Enable");
	gpio_config_t io_conf = {
		.pin_bit_mask = 1ULL << led_manager.pwr_en_pin,
		.mode         = GPIO_MODE_OUTPUT,
	};
	ESP_ERROR_CHECK(gpio_config(&io_conf));

	ESP_LOGI(TAG, "Create LED cache mutex lock");
	led_manager.cache_mutex_lock = xSemaphoreCreateMutex();
	if (unlikely(led_manager.cache_mutex_lock == nullptr)) {
		ESP_LOGE(TAG, "Failed to create LED manager mutex lock");
		abort();
	}

	led_manager.pending_cleanup_tasks = 0;
	led_manager.cleanup_spinlock      = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
	led_manager.cleanup_drained_sem   = xSemaphoreCreateBinary();
	if (unlikely(led_manager.cleanup_drained_sem == nullptr)) {
		ESP_LOGE(TAG, "Failed to create cleanup drain semaphore");
		abort();
	}
	// Give once so uninit passes immediately if no tasks were ever spawned.
	xSemaphoreGive(led_manager.cleanup_drained_sem);

	led_manager.inited = true;
}

void led_manager_uninit(void)
{
	if (unlikely(!led_manager.inited)) {
		ESP_LOGE(TAG, "Tried to uninit LED manager when not inited");
		abort();
	}

	if (led_manager.enabled) {
		led_manager_disable();
	}

	// Wait for any in-flight rmt_tx_cleanup tasks to finish before destroying
	// channel handles. led_manager_disable() sets enabled=false first, so those
	// tasks exit their wait loops quickly and decrement the counter.
	xSemaphoreTake(led_manager.cleanup_drained_sem, portMAX_DELAY);
	portENTER_CRITICAL(&led_manager.cleanup_spinlock);
	int pending = led_manager.pending_cleanup_tasks;
	portEXIT_CRITICAL(&led_manager.cleanup_spinlock);
	while (pending > 0) {
		xSemaphoreGive(led_manager.cleanup_drained_sem);
		xSemaphoreTake(led_manager.cleanup_drained_sem, portMAX_DELAY);
		portENTER_CRITICAL(&led_manager.cleanup_spinlock);
		pending = led_manager.pending_cleanup_tasks;
		portEXIT_CRITICAL(&led_manager.cleanup_spinlock);
	}

	for (enum LED_manager_cube_face face = 0; face < LED_MANAGER_CUBE_FACE_COUNT; face++) {
		ESP_ERROR_CHECK(rmt_del_channel(led_manager.handlers[face]));
	}

	led_encoder_cleanup_rmt(&led_manager.encoder);

	vSemaphoreDelete(led_manager.cleanup_drained_sem);
	vSemaphoreDelete(led_manager.cache_mutex_lock);

	memset(&led_manager, 0, sizeof(led_manager));
}

/**
 * @brief FreeRTOS task that waits for all RMT TX transfers to complete and
 *        then frees the associated pixel byte arrays.
 *
 * @param data Pointer to a heap-allocated array of @c LED_MANAGER_CUBE_FACE_COUNT
 *             @c uint8_t* pointers, each pointing to a heap-allocated pixel byte
 *             buffer for the corresponding cube face. Both the inner buffers and
 *             the outer array are freed before this task exits.
 *
 * @note If an RMT timeout or failure is detected while the manager is disabled,
 *       the error is silently ignored (the disabled path deliberately interrupts
 *       in-flight transfers). Any other RMT error causes an abort.
 */
static void rmt_tx_cleanup(void *data)
{
	uint8_t **pixel_arrays = data; // length is guaranteed to be LED_MANAGER_CUBE_FACE_COUNT
	esp_err_t ret = ESP_OK;

	for (enum LED_manager_cube_face face = 0; face < LED_MANAGER_CUBE_FACE_COUNT; face++) {
		// Skip any faces not written in this task
		if (pixel_arrays[face] == nullptr)
			continue;

		ret = rmt_tx_wait_all_done(led_manager.handlers[face],
		                           LED_MANAGER_RMT_WRITE_TIMEOUT);
		if (unlikely((ret == ESP_ERR_TIMEOUT || ret == ESP_FAIL) && led_manager.enabled == false)) {
			// Error probably caused by us disabling the rmt. Ignore
			continue;
		} else if (unlikely(ret != ESP_OK)) {
			ESP_LOGE(TAG, "Failed to wait for RMT TX to finish");
			abort();
		}
	}

	for (enum LED_manager_cube_face face = 0; face < LED_MANAGER_CUBE_FACE_COUNT; face++) {
		free(pixel_arrays[face]);
	}
	free(pixel_arrays);

	portENTER_CRITICAL(&led_manager.cleanup_spinlock);
	led_manager.pending_cleanup_tasks--;
	bool last = (led_manager.pending_cleanup_tasks == 0);
	portEXIT_CRITICAL(&led_manager.cleanup_spinlock);
	if (last) {
		xSemaphoreGive(led_manager.cleanup_drained_sem);
	}

	vTaskDelete(nullptr);
}

/**
 * @brief Transmit a contiguous sub-range of the LED pixel cache to all cube
 *        faces via RMT.
 *
 * @param offset Zero-based pixel index within the cache at which to begin
 *               the transmission.
 * @param length Number of pixels to transmit starting at offset.
 *
 * @return
 *      - ESP_OK on success.
 *      - ESP_ERR_NO_MEM if pixel byte buffers could not be allocated.
 *      - ESP_ERR_NOT_FOUND if an RMT TX channel queue is full (back-pressure:
 *        caller is submitting updates faster than the hardware can drain them).
 *      - ESP_FAIL if color conversion fails or the cleanup task cannot be created.
 */
static esp_err_t rmt_tx_write_leds_from_cache(uint16_t offset, uint16_t length)
{
	esp_err_t ret = ESP_OK;
	enum LED_manager_cube_face start_face, stop_face;
	// Use calloc so that error handling can check if a child array was allocated.
	// Always allocate a pointer for each face, assuming that <6 words of waisted RAM
	// is better than the additional logic needed to handle varying lengths.
	uint8_t **pixel_arrays = calloc(LED_MANAGER_CUBE_FACE_COUNT, sizeof(uint8_t *));

	if (unlikely(pixel_arrays == nullptr)) {
		ESP_LOGE(TAG, "Failed to allocate %d bytes for %s",
		         LED_MANAGER_CUBE_FACE_COUNT * sizeof(uint8_t *), __func__);
		return ESP_ERR_NO_MEM;
	}

	start_face = offset / LED_MANAGER_PANEL_LED_COUNT;
	stop_face = (offset + length - 1) / LED_MANAGER_PANEL_LED_COUNT;

	for (enum LED_manager_cube_face face = start_face; face <= stop_face; face++) {
		const led_color_t *colors = &led_manager.pixel_cache[face * LED_MANAGER_PANEL_LED_COUNT];
		// queue_nonblocking: return ESP_ERR_NOT_FOUND instead of blocking when
		// the per-channel queue is full. This prevents holding cache_mutex_lock
		// for an unbounded time when the caller is updating LEDs faster than
		// the RMT hardware can drain them.
		const rmt_transmit_config_t tx_config = {
			.flags.queue_nonblocking = 1,
		};
		if (led_manager_led_color_to_bytes(colors, LED_MANAGER_PANEL_LED_COUNT, &pixel_arrays[face]) != ESP_OK) {
			ESP_LOGE(TAG, "LED color to bytes failed");
			for (enum LED_manager_cube_face _face = start_face; _face < face; _face++) {
				rmt_tx_wait_all_done(led_manager.handlers[_face], portMAX_DELAY);
			}
			ret = ESP_FAIL;
			goto err;
		}
		// Flush RGB values to LEDs
		ret = rmt_transmit(led_manager.handlers[face],
		                   led_manager.encoder, pixel_arrays[face],
		                   LED_MANAGER_PANEL_LED_COUNT * 3,
		                   &tx_config);
		if (unlikely(ret != ESP_OK)) {
			ESP_LOGE(TAG, "rmt_transmit failed on face %d: %s", face, esp_err_to_name(ret));
			// Wait for any successfully-started transmissions before freeing buffers.
			for (enum LED_manager_cube_face _face = start_face; _face < face; _face++) {
				rmt_tx_wait_all_done(led_manager.handlers[_face], portMAX_DELAY);
			}
			goto err;
		}
	}

	// Increment before spawning so uninit can't race past the drain check.
	portENTER_CRITICAL(&led_manager.cleanup_spinlock);
	led_manager.pending_cleanup_tasks++;
	portEXIT_CRITICAL(&led_manager.cleanup_spinlock);

	// 1kB stack seems like it should be plenty, unsure how much the wait functions take
	if (xTaskCreate(rmt_tx_cleanup, "rmt_tx_all_cleanup",
	                1024, pixel_arrays, LED_TASK_PRIO, nullptr) != pdPASS) {
		ESP_LOGE(TAG, "Failed to create task for %s", __func__);
		portENTER_CRITICAL(&led_manager.cleanup_spinlock);
		led_manager.pending_cleanup_tasks--;
		portEXIT_CRITICAL(&led_manager.cleanup_spinlock);
		for (enum LED_manager_cube_face face = start_face; face <= stop_face; face++) {
			rmt_tx_wait_all_done(led_manager.handlers[face], portMAX_DELAY);
		}
		ret = ESP_FAIL;
		goto err;
	}

	return ret;
err:
	for (enum LED_manager_cube_face face = 0; face < LED_MANAGER_CUBE_FACE_COUNT; face++) {
		free(pixel_arrays[face]);
	}
	free(pixel_arrays);
	return ret;
}

/**
 * @brief Transmit the full LED pixel cache to all cube faces via RMT.
 *
 * Converts the cached pixel data to the GBR byte format expected by the
 * LED strips, kicks off simultaneous RMT transmissions on every face channel,
 * and spawns a cleanup task (@ref rmt_tx_cleanup) to free the byte buffers
 * once all transfers complete.
 *
 * @return
 *      - ESP_OK on success.
 *      - ESP_ERR_NO_MEM if a pixel byte buffer or the cleanup task could not
 *        be allocated.
 *      - ESP_FAIL if the cleanup task could not be created.
 *      - ESP_ERR_NOT_FOUND if an RMT TX channel queue is full.
 *      - Any error forwarded from @ref rmt_tx_write_leds_from_cache.
 */
static esp_err_t rmt_tx_write_all_leds_from_cache(void)
{
	return rmt_tx_write_leds_from_cache(0, LED_MANAGER_CUBE_LED_COUNT);
}

esp_err_t led_manager_enable(void)
{
	esp_err_t ret = ESP_OK;
	bool mutex_locked = false;

	if (unlikely(!led_manager.inited)) {
		ESP_LOGE(TAG, "Tried to enable LED manager when not inited");
		abort();
	}

	if (unlikely(led_manager.enabled)) {
		ESP_LOGE(TAG, "Tried to enable LED manager when already enabled");
		return ESP_ERR_INVALID_STATE;
	}

	ESP_LOGI(TAG, "Enabled LED 5V");
	ESP_ERROR_CHECK(gpio_set_level(led_manager.pwr_en_pin, 1));

	ESP_LOGI(TAG, "Enable RMT TX channels");
	for (enum LED_manager_cube_face face = 0; face < LED_MANAGER_CUBE_FACE_COUNT; face++) {
		ESP_ERROR_CHECK(rmt_enable(led_manager.handlers[face]));
	}

	led_manager.enabled = true;

	if (xSemaphoreTake(led_manager.cache_mutex_lock, LED_MANAGER_MUTEX_TIMEOUT_TICKS) != pdTRUE) {
		ESP_LOGE(TAG, "Failed to take mutex lock");
		ret = ESP_FAIL;
		goto err;
	}
	mutex_locked = true;

	// Sync LEDs with the cache
	ESP_GOTO_ON_ERROR(rmt_tx_write_all_leds_from_cache(), err, TAG, "Failed to update LEDs on enable");

	xSemaphoreGive(led_manager.cache_mutex_lock);
	mutex_locked = false;

	ESP_LOGI(TAG, "LED manager enabled");

	return ESP_OK;
err:
	// Note: This can only be jumped to after the rmt_enable calls.
	//       If the rmts are not enabled led_manager_disable() will fail.
	if (mutex_locked) {
		xSemaphoreGive(led_manager.cache_mutex_lock);
	}
	led_manager_disable();
	return ret;
}

esp_err_t led_manager_disable(void)
{
	if (unlikely(!led_manager.inited)) {
		ESP_LOGE(TAG, "Tried to disable LED manager when not inited");
		abort();
	}

	if (unlikely(!led_manager.enabled)) {
		ESP_LOGE(TAG, "Tried to disable LED manager when not enabled");
		return ESP_ERR_INVALID_STATE;
	}

	// This must be set before RMTs are disabled to inform the error handling
	// to ignore truncated sends.
	led_manager.enabled = false;

	ESP_LOGI(TAG, "Disable RMT TX channels");
	for (enum LED_manager_cube_face face = 0; face < LED_MANAGER_CUBE_FACE_COUNT; face++) {
		ESP_ERROR_CHECK(rmt_disable(led_manager.handlers[face]));
	}
	ESP_LOGI(TAG, "Disabled LED 5V");
	ESP_ERROR_CHECK(gpio_set_level(led_manager.pwr_en_pin, 0));
	ESP_LOGI(TAG, "LED manager disabled");

	return ESP_OK;
}

/**
 * @brief Update the cache and transmit all contiguous dirty pixel runs in a region.
 *
 * Scans the pixel cache over [offset, offset + length) comparing each entry
 * against the corresponding element of values. Contiguous pixels that differ
 * are coalesced into a single run; the cache is updated, and an RMT transmission
 * is queued for each run.
 *
 * @note: This was implemented assuming that LED pixel write will be the choke
 *        point of the process and should be minimized.
 * @note: Each batch spawns a cleanup task that waits for the transaction to finish.
 *
 * TODO: Need to evaluate if this is going to eat all of the ram. Should we
 *       limit the number of batches?
 *
 * @param offset Base index into the flat pixel cache.
 * @param length Number of pixels to scan.
 * @param values New pixel values to compare against the cache and write.
 * @param force  If true, treat every pixel as dirty regardless of cache state.
 *
 * @note The caller must hold cache_mutex_lock before calling this function.
 *
 * @return
 *      - ESP_OK on success.
 *      - Any error forwarded from rmt_tx_write_leds_from_cache.
 */
static esp_err_t write_dirty_runs(
		int16_t offset, int16_t length, const led_color_t *values, bool force)
{
	esp_err_t ret = ESP_OK;
	int16_t run_start = -1;

	for (int16_t i = 0; i <= length; i++) {
		bool pixel_dirty;
		pixel_dirty = (i < length) && (force || led_manager.pixel_cache[offset + i] != values[i]);

		if (pixel_dirty && run_start == -1) {
			run_start = i;
		} else if (!pixel_dirty && run_start != -1) {
			int16_t run_len = i - run_start;
			update_led_cache(offset + run_start, run_len, &values[run_start]);
			ESP_GOTO_ON_ERROR(
				rmt_tx_write_leds_from_cache(offset + run_start, run_len),
				err, TAG, "Failed to write dirty run at offset %d", offset + run_start);
			run_start = -1;
		}
	}

err:
	return ret;
}

esp_err_t led_manager_set_pixel(
		enum LED_manager_cube_face face, uint8_t idx, led_color_t color, bool force)
{
	if (unlikely(face >= LED_MANAGER_CUBE_FACE_COUNT || idx >= LED_MANAGER_PANEL_LED_COUNT)) {
		ESP_LOGE(TAG, "Invalid face %d or idx %d", face, idx);
		return ESP_ERR_INVALID_ARG;
	}

	esp_err_t ret = ESP_OK;
	int16_t offset = LED_MANAGER_P_INDEX_TO_INDEX(face, idx);
	bool mutex_locked = false;

	if (xSemaphoreTake(led_manager.cache_mutex_lock, LED_MANAGER_MUTEX_TIMEOUT_TICKS) != pdTRUE) {
		ESP_LOGE(TAG, "Failed to take mutex lock");
		ret = ESP_FAIL;
		goto err;
	}
	mutex_locked = true;

	if (!force && led_manager.pixel_cache[offset] == color) {
		//Nothing to do
		xSemaphoreGive(led_manager.cache_mutex_lock);
		return ret;
	}

	update_led_cache(offset, 1, &color);

	ESP_GOTO_ON_ERROR(rmt_tx_write_leds_from_cache(offset, 1),
	                  err, TAG, "Failed to update LEDs");

	xSemaphoreGive(led_manager.cache_mutex_lock);
	mutex_locked = false;

	return ret;
err:
	if (mutex_locked) {
		xSemaphoreGive(led_manager.cache_mutex_lock);
	}
	return ret;
}

esp_err_t led_manager_set_face(
		enum LED_manager_cube_face face, led_color_t color, bool force)
{
	if (unlikely(face >= LED_MANAGER_CUBE_FACE_COUNT)) {
		ESP_LOGE(TAG, "Invalid face %d", face);
		return ESP_ERR_INVALID_ARG;
	}

	esp_err_t ret = ESP_OK;
	int16_t offset = LED_MANAGER_P_INDEX_TO_INDEX(face, 0);
	bool mutex_locked = false;

	led_color_t fill[LED_MANAGER_PANEL_LED_COUNT];
	for (int i = 0; i < LED_MANAGER_PANEL_LED_COUNT; i++) {
		fill[i] = color;
	}

	if (xSemaphoreTake(led_manager.cache_mutex_lock, LED_MANAGER_MUTEX_TIMEOUT_TICKS) != pdTRUE) {
		ESP_LOGE(TAG, "Failed to take mutex lock");
		ret = ESP_FAIL;
		goto err;
	}
	mutex_locked = true;

	ESP_GOTO_ON_ERROR(
		write_dirty_runs(offset, LED_MANAGER_PANEL_LED_COUNT, fill, force),
		err, TAG, "Failed to update LEDs");

	xSemaphoreGive(led_manager.cache_mutex_lock);
	mutex_locked = false;

	return ret;
err:
	if (mutex_locked) {
		xSemaphoreGive(led_manager.cache_mutex_lock);
	}
	return ret;
}

esp_err_t led_manager_set_all(led_color_t color, bool force)
{
	esp_err_t ret = ESP_OK;
	bool mutex_locked = false;

	led_color_t fill[LED_MANAGER_PANEL_LED_COUNT];
	for (int i = 0; i < LED_MANAGER_PANEL_LED_COUNT; i++) {
		fill[i] = color;
	}

	if (xSemaphoreTake(led_manager.cache_mutex_lock, LED_MANAGER_MUTEX_TIMEOUT_TICKS) != pdTRUE) {
		ESP_LOGE(TAG, "Failed to take mutex lock");
		ret = ESP_FAIL;
		goto err;
	}
	mutex_locked = true;

	for (enum LED_manager_cube_face face = 0; face < LED_MANAGER_CUBE_FACE_COUNT; face++) {
		int16_t offset = LED_MANAGER_P_INDEX_TO_INDEX(face, 0);
		ESP_GOTO_ON_ERROR(
			write_dirty_runs(offset, LED_MANAGER_PANEL_LED_COUNT, fill, force),
			err, TAG, "Failed to update LEDs on face %d", face);
	}

	xSemaphoreGive(led_manager.cache_mutex_lock);
	mutex_locked = false;

	return ret;
err:
	if (mutex_locked) {
		xSemaphoreGive(led_manager.cache_mutex_lock);
	}
	return ret;
}

esp_err_t led_manager_set_face_from_array(
		enum LED_manager_cube_face face,
		led_color_t color_array[static LED_MANAGER_PANEL_LED_COUNT],
		bool force)
{
	if (unlikely(face >= LED_MANAGER_CUBE_FACE_COUNT)) {
		ESP_LOGE(TAG, "Invalid face %d", face);
		return ESP_ERR_INVALID_ARG;
	}

	esp_err_t ret = ESP_OK;
	int16_t offset = LED_MANAGER_P_INDEX_TO_INDEX(face, 0);
	bool mutex_locked = false;

	if (xSemaphoreTake(led_manager.cache_mutex_lock, LED_MANAGER_MUTEX_TIMEOUT_TICKS) != pdTRUE) {
		ESP_LOGE(TAG, "Failed to take mutex lock");
		ret = ESP_FAIL;
		goto err;
	}
	mutex_locked = true;

	ESP_GOTO_ON_ERROR(
		write_dirty_runs(offset, LED_MANAGER_PANEL_LED_COUNT, color_array, force),
		err, TAG, "Failed to update LEDs");

	xSemaphoreGive(led_manager.cache_mutex_lock);
	mutex_locked = false;

	return ret;
err:
	if (mutex_locked) {
		xSemaphoreGive(led_manager.cache_mutex_lock);
	}
	return ret;
}

esp_err_t led_manager_set_color_from_array(
		led_color_t color_array[static LED_MANAGER_CUBE_LED_COUNT],
		bool force)
{
	esp_err_t ret = ESP_OK;
	bool mutex_locked = false;

	if (xSemaphoreTake(led_manager.cache_mutex_lock, LED_MANAGER_MUTEX_TIMEOUT_TICKS) != pdTRUE) {
		ESP_LOGE(TAG, "Failed to take mutex lock");
		ret = ESP_FAIL;
		goto err;
	}
	mutex_locked = true;

	ESP_GOTO_ON_ERROR(
		write_dirty_runs(0, LED_MANAGER_CUBE_LED_COUNT,
		                 color_array, force),
		err, TAG, "Failed to update LEDs");

	xSemaphoreGive(led_manager.cache_mutex_lock);
	mutex_locked = false;

	return ret;
err:
	if (mutex_locked) {
		xSemaphoreGive(led_manager.cache_mutex_lock);
	}
	return ret;
}
