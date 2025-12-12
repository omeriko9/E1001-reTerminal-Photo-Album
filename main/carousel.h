/*
 * Image Carousel - Automatic image rotation with overlays
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "storage_manager.h"

// Carousel state
typedef enum {
    CAROUSEL_STATE_IDLE,
    CAROUSEL_STATE_DISPLAYING,
    CAROUSEL_STATE_SLEEPING
} carousel_state_t;

/**
 * @brief Initialize carousel
 * @return ESP_OK on success
 */
esp_err_t carousel_init(void);

/**
 * @brief Start carousel (automatic rotation)
 */
void carousel_start(void);

/**
 * @brief Stop carousel
 */
void carousel_stop(void);

/**
 * @brief Show next image
 */
void carousel_next(void);

/**
 * @brief Show previous image
 */
void carousel_prev(void);

/**
 * @brief Show specific image by index
 * @param index Image index
 */
void carousel_show_index(int index);

/**
 * @brief Refresh current image (after settings change)
 */
void carousel_refresh(void);

/**
 * @brief Update settings
 * @param settings New settings
 */
void carousel_update_settings(const app_settings_t *settings);

/**
 * @brief Get current carousel state
 */
carousel_state_t carousel_get_state(void);

/**
 * @brief Get current image index
 */
int carousel_get_current_index(void);

/**
 * @brief Handle button press
 * @param button Button number (0-2)
 */
void carousel_handle_button(int button);

/**
 * @brief Show connected IP screen for 5 seconds
 * @param ip_addr IP address string
 */
void carousel_show_connected_ip(const char *ip_addr);

/**
 * @brief Main carousel task (internal use)
 */
void carousel_task(void *arg);
