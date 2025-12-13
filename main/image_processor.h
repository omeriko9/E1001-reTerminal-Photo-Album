/*
 * Image Processor - Convert images to e-ink format
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// Output format
typedef enum {
    IMG_FORMAT_1BPP,     // 1-bit black/white
    IMG_FORMAT_2BPP,     // 2-bit grayscale (4 levels)
    IMG_FORMAT_4BPP      // 4-bit grayscale (16 levels)
} img_format_t;

// Dithering algorithm
typedef enum {
    DITHER_NONE,         // Simple threshold
    DITHER_FLOYD,        // Floyd-Steinberg
    DITHER_ATKINSON,     // Atkinson (better for e-ink)
    DITHER_ORDERED       // Ordered dithering (Bayer)
} dither_algorithm_t;

// Processing options
typedef struct {
    uint16_t target_width;
    uint16_t target_height;
    img_format_t format;
    dither_algorithm_t dither;
    uint8_t threshold;      // For simple threshold (0-255)
    bool invert;            // Invert colors
    bool fit_mode;          // true=fit, false=fill
} img_process_opts_t;

/**
 * @brief Get default processing options
 */
void img_get_default_opts(img_process_opts_t *opts);

/**
 * @brief Detect image format from data
 * @param data Image data
 * @param size Data size
 * @return Format string ("bmp", "jpg", "png", "raw", "unknown")
 */
const char *img_detect_format(const uint8_t *data, size_t size);

/**
 * @brief Process image data for e-ink display
 * @param input Input image data (BMP, JPG, PNG, or raw)
 * @param input_size Input data size
 * @param output Output buffer for e-ink format
 * @param output_size Size of output buffer
 * @param opts Processing options
 * @return ESP_OK on success
 */
esp_err_t img_process(const uint8_t *input, size_t input_size,
                      uint8_t *output, size_t output_size,
                      const img_process_opts_t *opts);

/**
 * @brief Process image file for e-ink display (streaming from file)
 * @param filename Full path to image file
 * @param output Output buffer for e-ink format
 * @param output_size Size of output buffer
 * @param opts Processing options
 * @return ESP_OK on success
 */
esp_err_t img_process_file(const char *filename,
                           uint8_t *output, size_t output_size,
                           const img_process_opts_t *opts);

/**
 * @brief Decode BMP file to raw RGB
 * @param input BMP data
 * @param input_size BMP data size
 * @param output Output buffer (allocated by function)
 * @param width Output width
 * @param height Output height
 * @return ESP_OK on success
 */
esp_err_t img_decode_bmp(const uint8_t *input, size_t input_size,
                         uint8_t **output, uint16_t *width, uint16_t *height);

/**
 * @brief Convert RGB to 1-bit e-ink format
 * @param rgb RGB data (3 bytes per pixel)
 * @param width Image width
 * @param height Image height
 * @param output Output 1-bit buffer
 * @param opts Processing options
 */
void img_rgb_to_1bpp(const uint8_t *rgb, uint16_t width, uint16_t height,
                     uint8_t *output, const img_process_opts_t *opts);

/**
 * @brief Scale image to target size
 * @param input Input RGB data
 * @param in_w Input width
 * @param in_h Input height
 * @param output Output RGB buffer
 * @param out_w Output width
 * @param out_h Output height
 * @param fit If true, fit image within output (keep margins). If false, cover output (zoom in).
 */
void img_scale(const uint8_t *input, uint16_t in_w, uint16_t in_h,
               uint8_t *output, uint16_t out_w, uint16_t out_h, bool fit);

/**
 * @brief Check if raw buffer is valid e-ink format
 */
bool img_is_valid_epd_buffer(const uint8_t *data, size_t size);

/**
 * @brief Process uploaded image (generate thumbnail and optimized binary)
 * @param filename Full path to uploaded file
 * @return ESP_OK on success
 */
esp_err_t img_process_upload(const char *filename);
