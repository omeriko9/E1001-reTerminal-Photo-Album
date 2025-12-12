/*
 * Display Overlay - Date/Time, Temperature, Battery indicators
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "esp_err.h"
#include "epaper_driver.h"

// Overlay position
typedef enum {
    OVERLAY_POS_TOP_LEFT,
    OVERLAY_POS_TOP_RIGHT,
    OVERLAY_POS_BOTTOM_LEFT,
    OVERLAY_POS_BOTTOM_RIGHT
} overlay_pos_t;

// Overlay configuration
typedef struct {
    bool show_datetime;
    bool show_temperature;
    bool show_battery;
    bool show_wifi;
    int8_t timezone_offset;
    int font_size;              // 1-3
    uint8_t datetime_color;     // 0=black, 1=white
    overlay_pos_t datetime_pos;
    overlay_pos_t temp_pos;
    overlay_pos_t battery_pos;
} overlay_config_t;

/**
 * @brief Get default overlay configuration
 */
void overlay_get_default_config(overlay_config_t *config);

/**
 * @brief Draw all overlays on the framebuffer
 * @param fb Framebuffer to draw on
 * @param config Overlay configuration
 * @param battery_percent Current battery percentage
 * @param temp_celsius Current temperature in Celsius (or -999 if unavailable)
 * @param wifi_connected WiFi connection status
 */
void overlay_draw(uint8_t *fb, const overlay_config_t *config,
                  uint8_t battery_percent, float temp_celsius, bool wifi_connected);

/**
 * @brief Draw date/time overlay
 */
void overlay_draw_datetime(uint8_t *fb, int x, int y, int font_size, 
                           uint8_t color, int8_t tz_offset);

/**
 * @brief Draw battery indicator
 */
void overlay_draw_battery(uint8_t *fb, int x, int y, int font_size,
                          uint8_t color, uint8_t percent);

/**
 * @brief Draw temperature
 */
void overlay_draw_temperature(uint8_t *fb, int x, int y, int font_size,
                              uint8_t color, float celsius);

/**
 * @brief Draw WiFi indicator
 */
void overlay_draw_wifi(uint8_t *fb, int x, int y, int font_size,
                       uint8_t color, bool connected);

/**
 * @brief Set time from SNTP or manual
 * @param epoch Unix timestamp
 */
void overlay_set_time(time_t epoch);

/**
 * @brief Sync time via SNTP
 * @return ESP_OK on success
 */
esp_err_t overlay_sync_time(void);
