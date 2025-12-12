/*
 * Image Carousel Implementation
 */

#include "carousel.h"
#include "board_config.h"
#include "epaper_driver.h"
#include "storage_manager.h"
#include "image_processor.h"
#include "display_overlay.h"
#include "power_manager.h"
#include "wifi_manager.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "carousel";

// State
static carousel_state_t s_state = CAROUSEL_STATE_IDLE;
static int s_current_index = 0;
static app_settings_t s_settings;
static TaskHandle_t s_task_handle = NULL;
static SemaphoreHandle_t s_mutex = NULL;
static bool s_running = false;
static bool s_refresh_pending = false;
static int s_show_index = -1;  // -1 = auto, >= 0 = specific index

esp_err_t carousel_init(void) {
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        return ESP_ERR_NO_MEM;
    }
    
    // Load settings
    storage_load_settings(&s_settings);
    
    ESP_LOGI(TAG, "Carousel initialized (interval: %lu sec)", s_settings.carousel_interval_sec);
    return ESP_OK;
}

static void display_image(int index) {
    image_info_t info;
    
    if (storage_get_image_by_index(index, &info) != ESP_OK) {
        ESP_LOGW(TAG, "No image at index %d", index);
        return;
    }
    
    ESP_LOGI(TAG, "Displaying image %d: %s", index, info.filename);
    
    // Load image from SD card
    uint8_t *raw_data = NULL;
    size_t raw_size = 0;
    
    if (storage_load_image(info.filename, &raw_data, &raw_size) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load image");
        return;
    }
    
    // Get/create framebuffer
    uint8_t *fb = epd_get_framebuffer();
    if (!fb) {
        free(raw_data);
        return;
    }
    
    // Process image if needed
    const char *format = img_detect_format(raw_data, raw_size);
    
    if (strcmp(format, "raw") == 0 || strcmp(format, "bin") == 0) {
        // Already in e-ink format
        if (raw_size == EPAPER_BUFFER_SIZE) {
            memcpy(fb, raw_data, EPAPER_BUFFER_SIZE);
        } else {
            ESP_LOGW(TAG, "Raw image size mismatch: %d vs %d", raw_size, EPAPER_BUFFER_SIZE);
            memset(fb, 0xFF, EPAPER_BUFFER_SIZE);
        }
    } else {
        // Convert image
        img_process_opts_t opts;
        img_get_default_opts(&opts);
        
        if (img_process(raw_data, raw_size, fb, EPAPER_BUFFER_SIZE, &opts) != ESP_OK) {
            ESP_LOGE(TAG, "Image processing failed");
            memset(fb, 0xFF, EPAPER_BUFFER_SIZE);
        }
    }
    
    free(raw_data);
    
    // Draw overlays
    overlay_config_t overlay_cfg;
    overlay_get_default_config(&overlay_cfg);
    overlay_cfg.show_datetime = s_settings.show_datetime;
    overlay_cfg.show_temperature = s_settings.show_temperature;
    overlay_cfg.show_battery = s_settings.show_battery;
    overlay_cfg.timezone_offset = s_settings.timezone_offset;
    
    wifi_mgr_info_t wifi_info;
    wifi_mgr_get_info(&wifi_info);
    
    overlay_draw(fb, &overlay_cfg, 
                 power_get_battery_percent(),
                 -999,  // No temperature sensor on board
                 wifi_info.status == WIFI_MGR_STATUS_CONNECTED);
    
    // Display on e-paper
    s_state = CAROUSEL_STATE_DISPLAYING;
    epd_display(fb, EPD_UPDATE_FULL);
    
    // Update stored index
    s_current_index = index;
    s_settings.current_image_index = index;
    storage_save_settings(&s_settings);
}

static void display_no_images(void) {
    uint8_t *fb = epd_get_framebuffer();
    if (!fb) return;
    
    // Clear to white
    memset(fb, 0xFF, EPAPER_BUFFER_SIZE);
    
    // Draw message
    const char *msg1 = "No Images Found";
    const char *msg2 = "Connect to WiFi to upload images";
    
    int y = EPAPER_HEIGHT / 2 - 30;
    int x1 = (EPAPER_WIDTH - epd_get_text_width(msg1, 3)) / 2;
    int x2 = (EPAPER_WIDTH - epd_get_text_width(msg2, 2)) / 2;
    
    epd_draw_text(x1, y, msg1, 3, 0);
    epd_draw_text(x2, y + 50, msg2, 2, 0);
    
    // Draw WiFi info
    wifi_mgr_info_t wifi_info;
    wifi_mgr_get_info(&wifi_info);
    
    char ip_msg[64];
    if (wifi_info.mode == WIFI_MGR_MODE_AP) {
        snprintf(ip_msg, sizeof(ip_msg), "Connect to: %s", wifi_info.ap_ssid);
        epd_draw_text(20, EPAPER_HEIGHT - 60, ip_msg, 2, 0);
        snprintf(ip_msg, sizeof(ip_msg), "Open: http://%s", wifi_info.ap_ip_addr);
        epd_draw_text(20, EPAPER_HEIGHT - 30, ip_msg, 2, 0);
    } else if (wifi_info.status == WIFI_MGR_STATUS_CONNECTED) {
        snprintf(ip_msg, sizeof(ip_msg), "Web UI: http://%s", wifi_info.ip_addr);
        epd_draw_text(20, EPAPER_HEIGHT - 30, ip_msg, 2, 0);
    }
    
    epd_display(fb, EPD_UPDATE_FULL);
}

void carousel_task(void *arg) {
    ESP_LOGI(TAG, "Carousel task started");
    
    TickType_t last_display = 0;
    
    while (s_running) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        
        int image_count = storage_get_image_count();
        bool need_display = false;
        int target_index = s_current_index;
        
        // Check if specific image requested
        if (s_show_index >= 0) {
            target_index = s_show_index;
            s_show_index = -1;
            need_display = true;
        }
        
        // Check if refresh requested
        if (s_refresh_pending) {
            s_refresh_pending = false;
            need_display = true;
        }
        
        // Check interval timer
        TickType_t now = xTaskGetTickCount();
        TickType_t interval_ticks = pdMS_TO_TICKS(s_settings.carousel_interval_sec * 1000);
        
        if (!need_display && (now - last_display) >= interval_ticks) {
            need_display = true;
            target_index = (s_current_index + 1) % (image_count > 0 ? image_count : 1);
        }
        
        xSemaphoreGive(s_mutex);
        
        if (need_display) {
            if (image_count > 0) {
                display_image(target_index);
            } else {
                display_no_images();
            }
            last_display = xTaskGetTickCount();
            
            // Enter deep sleep if WiFi is off and carousel is running
            if (!wifi_mgr_is_active() && s_settings.carousel_interval_sec > 60) {
                ESP_LOGI(TAG, "Entering deep sleep until next image");
                epd_sleep();
                power_enter_deep_sleep(s_settings.carousel_interval_sec);
                // Won't return here - will wake and restart
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000));  // Check every second
    }
    
    ESP_LOGI(TAG, "Carousel task stopped");
    s_task_handle = NULL;
    vTaskDelete(NULL);
}

void carousel_start(void) {
    if (s_running) return;
    
    s_running = true;
    xTaskCreate(carousel_task, "carousel", 8192, NULL, 5, &s_task_handle);
    ESP_LOGI(TAG, "Carousel started");
}

void carousel_stop(void) {
    s_running = false;
    // Task will self-terminate
    ESP_LOGI(TAG, "Carousel stopping");
}

void carousel_next(void) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    int count = storage_get_image_count();
    if (count > 0) {
        s_show_index = (s_current_index + 1) % count;
    }
    xSemaphoreGive(s_mutex);
}

void carousel_prev(void) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    int count = storage_get_image_count();
    if (count > 0) {
        s_show_index = (s_current_index - 1 + count) % count;
    }
    xSemaphoreGive(s_mutex);
}

void carousel_show_index(int index) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_show_index = index;
    xSemaphoreGive(s_mutex);
}

void carousel_refresh(void) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_refresh_pending = true;
    xSemaphoreGive(s_mutex);
}

void carousel_update_settings(const app_settings_t *settings) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    memcpy(&s_settings, settings, sizeof(app_settings_t));
    s_refresh_pending = true;
    xSemaphoreGive(s_mutex);
}

carousel_state_t carousel_get_state(void) {
    return s_state;
}

int carousel_get_current_index(void) {
    return s_current_index;
}

void carousel_handle_button(int button) {
    switch (button) {
        case 0:  // K0 - WiFi toggle
            wifi_mgr_toggle();
            power_buzzer_beep(1000, 100);
            break;
            
        case 1:  // K1 - Next image
            carousel_next();
            power_buzzer_beep(2000, 50);
            break;
            
        case 2:  // K2 - Previous image
            carousel_prev();
            power_buzzer_beep(2000, 50);
            break;
    }
}
