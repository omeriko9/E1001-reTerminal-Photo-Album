/*
 * Storage Manager - SD Card and NVS for images and settings
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// Maximum images supported
#define MAX_IMAGES 100
#define MAX_FILENAME_LEN 64
#define IMAGES_DIR "/sdcard/images"

// Image info structure
typedef struct {
    char filename[MAX_FILENAME_LEN];
    uint32_t size;
    uint32_t width;
    uint32_t height;
    bool valid;
} image_info_t;

// Settings structure
typedef struct {
    uint32_t carousel_interval_sec;     // Interval between image changes
    uint32_t wifi_timeout_sec;          // WiFi auto-off timeout
    uint32_t deep_sleep_sec;            // Max deep sleep duration
    bool show_datetime;                 // Show date/time overlay
    bool show_temperature;              // Show temperature overlay
    bool show_battery;                  // Show battery level overlay
    bool show_wifi;                     // Show WiFi status overlay
    int8_t timezone_offset;             // UTC offset in hours
    bool auto_brightness;               // Auto brightness (if supported)
    uint8_t current_image_index;        // Currently displayed image
    char ap_ssid[33];                   // AP SSID
    char ap_password[65];               // AP password
    bool provisioned;                   // WiFi has been configured
    bool random_order;                  // Random image order
    bool fit_mode;                      // Fit image to screen (keep margins)
} app_settings_t;

/**
 * @brief Initialize storage manager (SD card and NVS)
 * @return ESP_OK on success
 */
esp_err_t storage_init(void);

/**
 * @brief Deinitialize storage manager
 */
void storage_deinit(void);

/**
 * @brief Check if SD card is mounted
 */
bool storage_sd_mounted(void);

/**
 * @brief Mount SD card
 */
esp_err_t storage_mount_sd(void);

/**
 * @brief Unmount SD card
 */
void storage_unmount_sd(void);

/**
 * @brief Get list of images on SD card
 * @param images Output array of image info
 * @param max_count Maximum number of images to return
 * @return Number of images found
 */
int storage_get_images(image_info_t *images, int max_count);

/**
 * @brief Get image count
 */
int storage_get_image_count(void);

/**
 * @brief Get image by index
 * @param index Image index
 * @param info Output image info
 * @return ESP_OK if found
 */
esp_err_t storage_get_image_by_index(int index, image_info_t *info);

/**
 * @brief Load image data from SD card
 * @param filename Image filename
 * @param buffer Output buffer (must be freed by caller)
 * @param size Output size of data
 * @return ESP_OK on success
 */
esp_err_t storage_load_image(const char *filename, uint8_t **buffer, size_t *size);

/**
 * @brief Save image to SD card
 * @param filename Image filename
 * @param data Image data
 * @param size Data size
 * @return ESP_OK on success
 */
esp_err_t storage_save_image(const char *filename, const uint8_t *data, size_t size);

/**
 * @brief Delete image from SD card
 * @param filename Image filename
 * @return ESP_OK on success
 */
esp_err_t storage_delete_image(const char *filename);

/**
 * @brief Delete all images from SD card
 * @return ESP_OK on success
 */
esp_err_t storage_delete_all_images(void);

/**
 * @brief Load application settings from NVS
 * @param settings Output settings structure
 * @return ESP_OK on success
 */
esp_err_t storage_load_settings(app_settings_t *settings);

/**
 * @brief Save application settings to NVS
 * @param settings Settings to save
 * @return ESP_OK on success
 */
esp_err_t storage_save_settings(const app_settings_t *settings);

/**
 * @brief Reset settings to defaults
 */
void storage_reset_settings(app_settings_t *settings);

/**
 * @brief Get free space on SD card in bytes
 */
uint64_t storage_get_free_space(void);

/**
 * @brief Get total space on SD card in bytes
 */
uint64_t storage_get_total_space(void);

/**
 * @brief Format SD card
 * @return ESP_OK on success
 */
esp_err_t storage_format_sd(void);

/**
 * @brief Create images directory if it doesn't exist
 */
esp_err_t storage_create_images_dir(void);

/**
 * @brief Check for images missing optimizations and process them
 */
void storage_process_missing_optimizations(void);
