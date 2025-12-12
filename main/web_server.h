/*
 * Web Server - HTTP server with WebUI for image management
 */

#pragma once

#include "esp_err.h"
#include "storage_manager.h"

/**
 * @brief Initialize and start web server
 * @return ESP_OK on success
 */
esp_err_t webserver_start(void);

/**
 * @brief Stop web server
 */
void webserver_stop(void);

/**
 * @brief Check if web server is running
 */
bool webserver_is_running(void);

/**
 * @brief Set callback for settings changes
 */
typedef void (*settings_change_cb_t)(const app_settings_t *settings, void *ctx);
void webserver_set_settings_callback(settings_change_cb_t cb, void *ctx);

/**
 * @brief Set callback for image changes
 */
typedef void (*image_change_cb_t)(const char *filename, bool added, void *ctx);
void webserver_set_image_callback(image_change_cb_t cb, void *ctx);

/**
 * @brief Force refresh of web UI (after external changes)
 */
void webserver_notify_refresh(void);
