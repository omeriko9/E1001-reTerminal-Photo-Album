/*
 * Storage Manager Implementation
 */

#include "storage_manager.h"
#include "board_config.h"

#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "driver/gpio.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "storage";

// SD card handle
static sdmmc_card_t *s_card = NULL;
static bool s_sd_mounted = false;

// NVS keys
#define NVS_KEY_SETTINGS "settings"

// Path buffer size (must be larger than IMAGES_DIR + MAX_FILENAME_LEN)
#define PATH_MAX_LEN 320

esp_err_t storage_init(void) {
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Configure SD power enable
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIN_SD_EN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    gpio_config(&io_conf);
    
    // Configure SD card detect
    io_conf.pin_bit_mask = (1ULL << PIN_SD_DET);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf);
    
    // Enable SD card power
    gpio_set_level(PIN_SD_EN, 1);
    vTaskDelay(pdMS_TO_TICKS(100));  // Power stabilization
    
    // Try to mount SD card
    storage_mount_sd();
    
    ESP_LOGI(TAG, "Storage initialized");
    return ESP_OK;
}

void storage_deinit(void) {
    storage_unmount_sd();
    gpio_set_level(PIN_SD_EN, 0);  // Power off SD
}

bool storage_sd_mounted(void) {
    return s_sd_mounted;
}

esp_err_t storage_mount_sd(void) {
    if (s_sd_mounted) {
        return ESP_OK;
    }
    
    // Check card detect
    if (gpio_get_level(PIN_SD_DET) != 0) {
        ESP_LOGW(TAG, "No SD card detected");
        return ESP_ERR_NOT_FOUND;
    }
    
    // SD card mount configuration
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };
    
    // SPI bus is initialized in main.c
    // Just configure the SD card device
    
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI_HOST_USED;
    host.max_freq_khz = 20000;  // 20 MHz
    
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_SD_CS;
    slot_config.host_id = SPI_HOST_USED;
    
    ESP_LOGI(TAG, "Mounting SD card...");
    
    esp_err_t ret = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &slot_config, 
                                            &mount_config, &s_card);
    
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SD card: %s", esp_err_to_name(ret));
        }
        return ret;
    }
    
    s_sd_mounted = true;
    
    // Print card info
    sdmmc_card_print_info(stdout, s_card);
    
    // Create images directory
    storage_create_images_dir();
    
    ESP_LOGI(TAG, "SD card mounted at %s", SD_MOUNT_POINT);
    return ESP_OK;
}

void storage_unmount_sd(void) {
    if (!s_sd_mounted) return;
    
    esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, s_card);
    s_card = NULL;
    s_sd_mounted = false;
    
    ESP_LOGI(TAG, "SD card unmounted");
}

int storage_get_images(image_info_t *images, int max_count) {
    if (!s_sd_mounted || !images) return 0;
    
    DIR *dir = opendir(IMAGES_DIR);
    if (!dir) {
        ESP_LOGW(TAG, "Cannot open images directory");
        return 0;
    }
    
    int count = 0;
    struct dirent *entry;
    
    while ((entry = readdir(dir)) != NULL && count < max_count) {
        // Skip hidden files and directories
        if (entry->d_name[0] == '.') continue;
        if (entry->d_type == DT_DIR) continue;
        
        // Check for supported extensions
        const char *ext = strrchr(entry->d_name, '.');
        if (!ext) continue;
        
        if (strcasecmp(ext, ".bin") != 0 && 
            strcasecmp(ext, ".raw") != 0 &&
            strcasecmp(ext, ".bmp") != 0 &&
            strcasecmp(ext, ".jpg") != 0 &&
            strcasecmp(ext, ".jpeg") != 0 &&
            strcasecmp(ext, ".png") != 0) {
            continue;
        }
        
        strncpy(images[count].filename, entry->d_name, MAX_FILENAME_LEN - 1);
        images[count].filename[MAX_FILENAME_LEN - 1] = '\0';
        
        // Get file size
        char path[PATH_MAX_LEN];
        snprintf(path, sizeof(path), "%s/%s", IMAGES_DIR, entry->d_name);
        
        struct stat st;
        if (stat(path, &st) == 0) {
            images[count].size = st.st_size;
        }
        
        // Default dimensions (will be updated when loaded)
        images[count].width = 800;
        images[count].height = 480;
        images[count].valid = true;
        
        count++;
    }
    
    closedir(dir);
    ESP_LOGI(TAG, "Found %d images", count);
    return count;
}

int storage_get_image_count(void) {
    if (!s_sd_mounted) return 0;
    
    DIR *dir = opendir(IMAGES_DIR);
    if (!dir) return 0;
    
    int count = 0;
    struct dirent *entry;
    
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        if (entry->d_type == DT_DIR) continue;
        
        const char *ext = strrchr(entry->d_name, '.');
        if (!ext) continue;
        
        if (strcasecmp(ext, ".bin") == 0 || 
            strcasecmp(ext, ".raw") == 0 ||
            strcasecmp(ext, ".bmp") == 0 ||
            strcasecmp(ext, ".jpg") == 0 ||
            strcasecmp(ext, ".jpeg") == 0 ||
            strcasecmp(ext, ".png") == 0) {
            count++;
        }
    }
    
    closedir(dir);
    return count;
}

esp_err_t storage_get_image_by_index(int index, image_info_t *info) {
    if (!s_sd_mounted || !info) return ESP_ERR_INVALID_ARG;
    
    DIR *dir = opendir(IMAGES_DIR);
    if (!dir) {
        return ESP_ERR_NOT_FOUND;
    }
    
    int count = 0;
    struct dirent *entry;
    bool found = false;
    
    while ((entry = readdir(dir)) != NULL) {
        // Skip hidden files and directories
        if (entry->d_name[0] == '.') continue;
        if (entry->d_type == DT_DIR) continue;
        
        // Check for supported extensions
        const char *ext = strrchr(entry->d_name, '.');
        if (!ext) continue;
        
        if (strcasecmp(ext, ".bin") != 0 && 
            strcasecmp(ext, ".raw") != 0 &&
            strcasecmp(ext, ".bmp") != 0 &&
            strcasecmp(ext, ".jpg") != 0 &&
            strcasecmp(ext, ".jpeg") != 0 &&
            strcasecmp(ext, ".png") != 0) {
            continue;
        }
        
        if (count == index) {
            // Found it!
            strncpy(info->filename, entry->d_name, MAX_FILENAME_LEN - 1);
            info->filename[MAX_FILENAME_LEN - 1] = '\0';
            
            // Get file size
            char path[PATH_MAX_LEN];
            snprintf(path, sizeof(path), "%s/%s", IMAGES_DIR, entry->d_name);
            
            struct stat st;
            if (stat(path, &st) == 0) {
                info->size = st.st_size;
            }
            
            info->width = 800;
            info->height = 480;
            info->valid = true;
            
            found = true;
            break;
        }
        
        count++;
    }
    
    closedir(dir);
    return found ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t storage_load_image(const char *filename, uint8_t **buffer, size_t *size) {
    if (!s_sd_mounted || !filename || !buffer || !size) {
        return ESP_ERR_INVALID_ARG;
    }
    
    char path[PATH_MAX_LEN];
    snprintf(path, sizeof(path), "%s/%s", IMAGES_DIR, filename);
    
    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open file: %s", path);
        return ESP_ERR_NOT_FOUND;
    }
    
    // Get file size
    fseek(f, 0, SEEK_END);
    *size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    // Allocate buffer
    *buffer = heap_caps_malloc(*size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!*buffer) {
        *buffer = malloc(*size);
    }
    if (!*buffer) {
        fclose(f);
        ESP_LOGE(TAG, "Cannot allocate %d bytes", *size);
        return ESP_ERR_NO_MEM;
    }
    
    // Read file
    size_t read = fread(*buffer, 1, *size, f);
    fclose(f);
    
    if (read != *size) {
        free(*buffer);
        *buffer = NULL;
        return ESP_ERR_INVALID_SIZE;
    }
    
    ESP_LOGI(TAG, "Loaded %s (%d bytes)", filename, *size);
    return ESP_OK;
}

esp_err_t storage_save_image(const char *filename, const uint8_t *data, size_t size) {
    if (!s_sd_mounted || !filename || !data) {
        return ESP_ERR_INVALID_ARG;
    }
    
    char path[PATH_MAX_LEN];
    snprintf(path, sizeof(path), "%s/%s", IMAGES_DIR, filename);
    
    FILE *f = fopen(path, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Cannot create file: %s", path);
        return ESP_FAIL;
    }
    
    size_t written = fwrite(data, 1, size, f);
    fclose(f);
    
    if (written != size) {
        unlink(path);
        return ESP_ERR_INVALID_SIZE;
    }
    
    ESP_LOGI(TAG, "Saved %s (%d bytes)", filename, size);
    return ESP_OK;
}

esp_err_t storage_delete_image(const char *filename) {
    if (!s_sd_mounted || !filename) {
        return ESP_ERR_INVALID_ARG;
    }
    
    char path[PATH_MAX_LEN];
    snprintf(path, sizeof(path), "%s/%s", IMAGES_DIR, filename);
    
    if (unlink(path) != 0) {
        ESP_LOGE(TAG, "Cannot delete: %s", path);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Deleted %s", filename);
    return ESP_OK;
}

void storage_reset_settings(app_settings_t *settings) {
    memset(settings, 0, sizeof(app_settings_t));
    
    settings->carousel_interval_sec = DEFAULT_CAROUSEL_INTERVAL_SEC;
    settings->wifi_timeout_sec = DEFAULT_WIFI_TIMEOUT_SEC;
    settings->deep_sleep_sec = DEFAULT_DEEP_SLEEP_SEC;
    settings->show_datetime = true;
    settings->show_temperature = true;
    settings->show_battery = true;
    settings->timezone_offset = 0;
    settings->auto_brightness = false;
    settings->current_image_index = 0;
    strncpy(settings->ap_ssid, DEFAULT_AP_SSID, sizeof(settings->ap_ssid));
    strncpy(settings->ap_password, DEFAULT_AP_PASS, sizeof(settings->ap_password));
    settings->provisioned = false;
}

esp_err_t storage_load_settings(app_settings_t *settings) {
    if (!settings) return ESP_ERR_INVALID_ARG;
    
    storage_reset_settings(settings);
    
    nvs_handle_t nvs;
    esp_err_t ret = nvs_open(SETTINGS_NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "No saved settings, using defaults");
        return ESP_OK;
    }
    
    size_t len = sizeof(app_settings_t);
    ret = nvs_get_blob(nvs, NVS_KEY_SETTINGS, settings, &len);
    nvs_close(nvs);
    
    if (ret != ESP_OK) {
        storage_reset_settings(settings);
        ESP_LOGW(TAG, "Failed to load settings, using defaults");
    } else {
        ESP_LOGI(TAG, "Settings loaded");
    }
    
    return ESP_OK;
}

esp_err_t storage_save_settings(const app_settings_t *settings) {
    if (!settings) return ESP_ERR_INVALID_ARG;
    
    nvs_handle_t nvs;
    esp_err_t ret = nvs_open(SETTINGS_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Cannot open NVS");
        return ret;
    }
    
    ret = nvs_set_blob(nvs, NVS_KEY_SETTINGS, settings, sizeof(app_settings_t));
    if (ret == ESP_OK) {
        nvs_commit(nvs);
        ESP_LOGI(TAG, "Settings saved");
    }
    
    nvs_close(nvs);
    return ret;
}

uint64_t storage_get_free_space(void) {
    if (!s_sd_mounted) return 0;
    
    uint64_t total, free_space;
    if (esp_vfs_fat_info(SD_MOUNT_POINT, &total, &free_space) != ESP_OK) {
        return 0;
    }
    return free_space;
}

uint64_t storage_get_total_space(void) {
    if (!s_sd_mounted) return 0;
    
    uint64_t total, free_space;
    if (esp_vfs_fat_info(SD_MOUNT_POINT, &total, &free_space) != ESP_OK) {
        return 0;
    }
    return total;
}

esp_err_t storage_format_sd(void) {
    if (!s_sd_mounted) return ESP_ERR_INVALID_STATE;
    
    // This would require unmounting first
    ESP_LOGW(TAG, "Format not implemented");
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t storage_create_images_dir(void) {
    if (!s_sd_mounted) return ESP_ERR_INVALID_STATE;
    
    struct stat st;
    if (stat(IMAGES_DIR, &st) == 0) {
        return ESP_OK;  // Already exists
    }
    
    if (mkdir(IMAGES_DIR, 0755) != 0) {
        ESP_LOGE(TAG, "Cannot create images directory");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Created %s", IMAGES_DIR);
    return ESP_OK;
}
