/*
 * e-Paper Display Driver for E1001 (800x480)
 * Supports common e-ink controllers like UC8179/IT8951
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// Display dimensions
#define EPD_WIDTH       800
#define EPD_HEIGHT      480

// Color definitions (for grayscale e-ink)
#define EPD_BLACK       0x00
#define EPD_WHITE       0xFF
#define EPD_GRAY1       0x40
#define EPD_GRAY2       0x80
#define EPD_GRAY3       0xC0

// Update modes
typedef enum {
    EPD_UPDATE_FULL,        // Full refresh (slow, no ghosting)
    EPD_UPDATE_PARTIAL,     // Partial refresh (faster, some ghosting)
    EPD_UPDATE_FAST         // Fast refresh (fastest, more ghosting)
} epd_update_mode_t;

// Display rotation
typedef enum {
    EPD_ROTATE_0,
    EPD_ROTATE_90,
    EPD_ROTATE_180,
    EPD_ROTATE_270
} epd_rotation_t;

/**
 * @brief Initialize e-Paper display
 * @return ESP_OK on success
 */
esp_err_t epd_init(void);

/**
 * @brief Deinitialize e-Paper display
 */
void epd_deinit(void);

/**
 * @brief Clear display to white
 */
void epd_clear(void);

/**
 * @brief Clear display to black
 */
void epd_clear_black(void);

/**
 * @brief Set display rotation
 * @param rotation Rotation enum value
 */
void epd_set_rotation(epd_rotation_t rotation);

/**
 * @brief Display a full framebuffer
 * @param buffer Framebuffer data (1 bit per pixel, 800*480/8 bytes)
 * @param mode Update mode
 */
void epd_display(const uint8_t *buffer, epd_update_mode_t mode);

/**
 * @brief Display grayscale image (4-level)
 * @param buffer 2 bits per pixel data
 */
void epd_display_grayscale(const uint8_t *buffer);

/**
 * @brief Update a partial region
 * @param buffer Image data for region
 * @param x X position
 * @param y Y position
 * @param w Width
 * @param h Height
 */
void epd_display_partial(const uint8_t *buffer, int x, int y, int w, int h);

/**
 * @brief Put display into deep sleep mode
 */
void epd_sleep(void);

/**
 * @brief Wake display from sleep
 */
void epd_wake(void);

/**
 * @brief Check if display is busy
 * @return true if busy
 */
bool epd_is_busy(void);

/**
 * @brief Wait for display to finish updating
 * @param timeout_ms Maximum time to wait
 * @return ESP_OK if ready, ESP_ERR_TIMEOUT if timeout
 */
esp_err_t epd_wait_busy(uint32_t timeout_ms);

/**
 * @brief Get framebuffer pointer for direct drawing
 * @return Pointer to framebuffer (EPD_WIDTH*EPD_HEIGHT/8 bytes)
 */
uint8_t *epd_get_framebuffer(void);

/**
 * @brief Set a pixel in the framebuffer
 * @param x X coordinate
 * @param y Y coordinate
 * @param color 0=black, 1=white
 */
void epd_set_pixel(int x, int y, uint8_t color);

/**
 * @brief Draw a horizontal line
 */
void epd_draw_hline(int x, int y, int w, uint8_t color);

/**
 * @brief Draw a vertical line
 */
void epd_draw_vline(int x, int y, int h, uint8_t color);

/**
 * @brief Draw a rectangle
 */
void epd_draw_rect(int x, int y, int w, int h, uint8_t color);

/**
 * @brief Fill a rectangle
 */
void epd_fill_rect(int x, int y, int w, int h, uint8_t color);

/**
 * @brief Draw text using built-in font
 * @param x X position
 * @param y Y position
 * @param text Text string
 * @param size Font size (1-3)
 * @param color Text color
 */
void epd_draw_text(int x, int y, const char *text, int size, uint8_t color);

/**
 * @brief Get text width in pixels
 */
int epd_get_text_width(const char *text, int size);
