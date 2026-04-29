/**
 * @file status_server.h
 * @brief HTTP status page — serves live sensor data over Wi-Fi.
 *
 * @author William Crow
 * @date 2026-04-29
 */

/*
 * @copyright
 * MIT License
 * Copyright (c) 2026 William Crow
 */

#ifndef LED_CUBE_CONTROLLER_STATUS_SERVER_H
#define LED_CUBE_CONTROLLER_STATUS_SERVER_H

/**
 * @brief Start the HTTP status server.
 *
 * Registers two routes:
 *   GET /           — HTML status dashboard (auto-refreshes every 2 s)
 *   GET /api/status — JSON snapshot of all sensor readings
 *
 * Call after Wi-Fi is connected and all sensor drivers are initialized.
 */
void status_server_start(void);

#endif // LED_CUBE_CONTROLLER_STATUS_SERVER_H