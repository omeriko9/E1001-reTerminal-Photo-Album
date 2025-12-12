/*
 * E1001 reTerminal Photo Frame - Main Application
 * 
 * Features:
 * - WiFi provisioning via AP mode
 * - Web UI for image upload/management and settings
 * - Image carousel with configurable interval
 * - Deep sleep between image changes for battery savings
 * - Button controls: K0=WiFi, K1=Next, K2=Prev
 * - Date/time, battery, temperature overlays
 * 
 * Copyright 2024
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"

#include "board_config.h"
#include "wifi_manager.h"
#include "epaper_driver.h"
#include "storage_manager.h"
#include "web_server.h"
#include "power_manager.h"
#include "carousel.h"
#include "display_overlay.h"

static const char *TAG = "main";

// WiFi auto-off timer
static TimerHandle_t s_wifi_timer = NULL;
static uint32_t s_wifi_timeout_sec = DEFAULT_WIFI_TIMEOUT_SEC;
static volatile bool s_wifi_shutdown_requested = false;

// Button debounce
#define BUTTON_DEBOUNCE_MS 50
static uint32_t s_last_button_time[3] = {0};

static void wifi_timeout_callback(TimerHandle_t timer) {
    // Do not log here to save stack space in Timer Service task
    s_wifi_shutdown_requested = true;
}

static void reset_wifi_timer(void) {
    if (s_wifi_timer && s_wifi_timeout_sec > 0) {
        xTimerChangePeriod(s_wifi_timer, pdMS_TO_TICKS(s_wifi_timeout_sec * 1000), 0);
        xTimerReset(s_wifi_timer, 0);
        s_wifi_shutdown_requested = false;
    }
}

static void stop_wifi_timer(void) {
    if (s_wifi_timer) {
        xTimerStop(s_wifi_timer, 0);
    }
}

static void wifi_callback(wifi_mgr_status_t status, void *ctx) {
    switch (status) {
        case WIFI_MGR_STATUS_AP_ACTIVE:
            ESP_LOGI(TAG, "AP mode active - starting web server");
            webserver_start();
            reset_wifi_timer();
            break;
            
        case WIFI_MGR_STATUS_CONNECTED:
            ESP_LOGI(TAG, "Connected to WiFi - starting web server and time sync");
            webserver_start();
            overlay_sync_time();
            reset_wifi_timer();
            break;
            
        case WIFI_MGR_STATUS_OFF:
            webserver_stop();
            break;
            
        case WIFI_MGR_STATUS_AP_STATION_CONNECTED:
            ESP_LOGI(TAG, "Client connected to AP - stopping auto-off timer");
            stop_wifi_timer();
            break;
            
        case WIFI_MGR_STATUS_AP_STATION_DISCONNECTED:
            ESP_LOGI(TAG, "Client disconnected from AP - restarting auto-off timer");
            reset_wifi_timer();
            break;
            
        default:
            break;
    }
}

static void settings_callback(const app_settings_t *settings, void *ctx) {
    ESP_LOGI(TAG, "Settings updated");
    s_wifi_timeout_sec = settings->wifi_timeout_sec;
    carousel_update_settings(settings);
}

static void image_callback(const char *filename, bool added, void *ctx) {
    ESP_LOGI(TAG, "Image %s: %s", added ? "added" : "removed", filename);
    carousel_refresh();
}

static void IRAM_ATTR button_isr_handler(void *arg) {
    int button = (int)arg;
    uint32_t now = xTaskGetTickCountFromISR();
    
    if ((now - s_last_button_time[button]) > pdMS_TO_TICKS(BUTTON_DEBOUNCE_MS)) {
        s_last_button_time[button] = now;
        
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        // We'll handle in main task to avoid ISR issues
        // For now just handle via wake reason on deep sleep wake
    }
}

static void setup_buttons(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = WAKEUP_BUTTON_MASK,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    gpio_config(&io_conf);
    
    // Install ISR service
    gpio_install_isr_service(0);
    gpio_isr_handler_add(PIN_BUTTON_K0, button_isr_handler, (void *)0);
    gpio_isr_handler_add(PIN_BUTTON_K1, button_isr_handler, (void *)1);
    gpio_isr_handler_add(PIN_BUTTON_K2, button_isr_handler, (void *)2);
}

static void display_startup_screen(void) {
    uint8_t *fb = epd_get_framebuffer();
    if (!fb) return;
    
    // Clear to white
    memset(fb, 0xFF, EPAPER_BUFFER_SIZE);
    
    // Draw startup message
    const char *title = "E1001 Photo Frame";
    const char *version = "v1.0.0";
    
    int title_w = epd_get_text_width(title, 3);
    int version_w = epd_get_text_width(version, 2);
    
    epd_draw_text((EPAPER_WIDTH - title_w) / 2, EPAPER_HEIGHT / 2 - 40, title, 3, 0);
    epd_draw_text((EPAPER_WIDTH - version_w) / 2, EPAPER_HEIGHT / 2 + 20, version, 2, 0);
    
    // Show status
    char status[64];
    snprintf(status, sizeof(status), "Battery: %d%%", power_get_battery_percent());
    epd_draw_text(20, EPAPER_HEIGHT - 60, status, 2, 0);
    
    if (storage_sd_mounted()) {
        int count = storage_get_image_count();
        snprintf(status, sizeof(status), "SD Card: %d images", count);
    } else {
        snprintf(status, sizeof(status), "SD Card: Not found");
    }
    epd_draw_text(20, EPAPER_HEIGHT - 30, status, 2, 0);
    
    epd_display(fb, EPD_UPDATE_FULL);
}

static void button_poll_task(void *arg) {
    bool last_state[3] = {true, true, true};  // Active low, so true = not pressed
    
    while (1) {
        bool states[3] = {
            gpio_get_level(PIN_BUTTON_K0),
            gpio_get_level(PIN_BUTTON_K1),
            gpio_get_level(PIN_BUTTON_K2)
        };
        
        for (int i = 0; i < 3; i++) {
            // Detect falling edge (button press)
            if (last_state[i] && !states[i]) {
                ESP_LOGI(TAG, "Button K%d pressed", i);
                carousel_handle_button(i);
                
                // Reset WiFi timer on any button press
                reset_wifi_timer();
            }
            last_state[i] = states[i];
        }
        
        vTaskDelay(pdMS_TO_TICKS(50));  // Poll every 50ms
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "=== E1001 Photo Frame Starting ===");
    
    // Initialize power manager first (to get wake reason)
    ESP_ERROR_CHECK(power_init());
    
    wake_reason_t wake_reason = power_get_wake_reason();
    ESP_LOGI(TAG, "Wake reason: %d", wake_reason);
    
    // Create default event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // Initialize SPI Bus (shared by SD card and e-Paper)
    spi_bus_config_t buscfg = {
        .mosi_io_num = PIN_SPI_MOSI,
        .miso_io_num = PIN_SPI_MISO,
        .sclk_io_num = PIN_SPI_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = SPI_MAX_TRANSFER_SIZE,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI_HOST_USED, &buscfg, SPI_DMA_CHAN));
    
    // Initialize storage (NVS and SD card)
    ESP_ERROR_CHECK(storage_init());
    
    // Load settings
    app_settings_t settings;
    storage_load_settings(&settings);
    s_wifi_timeout_sec = settings.wifi_timeout_sec;
    
    // Initialize e-Paper display
    ESP_ERROR_CHECK(epd_init());
    
    // Initialize WiFi manager
    ESP_ERROR_CHECK(wifi_mgr_init());
    wifi_mgr_register_callback(wifi_callback, NULL);
    
    // Initialize carousel
    ESP_ERROR_CHECK(carousel_init());
    
    // Create WiFi timeout timer
    s_wifi_timer = xTimerCreate("wifi_timer", pdMS_TO_TICKS(s_wifi_timeout_sec * 1000),
                                pdFALSE, NULL, wifi_timeout_callback);
    
    // Set up web server callbacks
    webserver_set_settings_callback(settings_callback, NULL);
    webserver_set_image_callback(image_callback, NULL);
    
    // Handle wake reason
    switch (wake_reason) {
        case WAKE_REASON_BUTTON_K0:
            // WiFi button pressed - start WiFi
            ESP_LOGI(TAG, "WiFi button wake - starting WiFi");
            power_buzzer_beep(1000, 200);
            
            if (wifi_mgr_has_credentials()) {
                if (wifi_mgr_start_sta() != ESP_OK) {
                    wifi_mgr_start_ap(settings.ap_ssid, settings.ap_password);
                }
            } else {
                wifi_mgr_start_ap(settings.ap_ssid, settings.ap_password);
            }
            break;
            
        case WAKE_REASON_BUTTON_K1:
            // Next image button
            ESP_LOGI(TAG, "Next button wake");
            power_buzzer_beep(2000, 100);
            carousel_next();
            break;
            
        case WAKE_REASON_BUTTON_K2:
            // Previous image button
            ESP_LOGI(TAG, "Prev button wake");
            power_buzzer_beep(2000, 100);
            carousel_prev();
            break;
            
        case WAKE_REASON_TIMER:
            // Timer wake - just display next image and go back to sleep
            ESP_LOGI(TAG, "Timer wake - carousel update");
            break;
            
        case WAKE_REASON_RESET:
        case WAKE_REASON_UNKNOWN:
        default:
            // Fresh boot - show startup screen and start AP if not provisioned
            ESP_LOGI(TAG, "Fresh boot");
            power_buzzer_beep(500, 100);
            vTaskDelay(pdMS_TO_TICKS(100));
            power_buzzer_beep(1000, 100);
            
            display_startup_screen();
            vTaskDelay(pdMS_TO_TICKS(2000));
            
            if (wifi_mgr_has_credentials()) {
                ESP_LOGI(TAG, "Found credentials, attempting to connect");
                if (wifi_mgr_start_sta() != ESP_OK) {
                    ESP_LOGW(TAG, "Connection failed, falling back to AP");
                    wifi_mgr_start_ap(settings.ap_ssid, settings.ap_password);
                }
            } else {
                // First boot - start AP for provisioning
                ESP_LOGI(TAG, "No credentials, starting AP for provisioning");
                wifi_mgr_start_ap(settings.ap_ssid, settings.ap_password);
            }
            break;
    }
    
    // Set up button handling
    setup_buttons();
    
    // Start button polling task
    xTaskCreate(button_poll_task, "buttons", 2048, NULL, 10, NULL);
    
    // Start carousel
    carousel_start();
    
    ESP_LOGI(TAG, "=== Initialization Complete ===");
    
    // Print status
    wifi_mgr_info_t wifi_info;
    wifi_mgr_get_info(&wifi_info);
    
    if (wifi_info.mode == WIFI_MGR_MODE_AP) {
        ESP_LOGI(TAG, "Connect to WiFi: %s", wifi_info.ap_ssid);
        ESP_LOGI(TAG, "Then open: http://%s", wifi_info.ap_ip_addr);
    } else if (wifi_info.status == WIFI_MGR_STATUS_CONNECTED) {
        ESP_LOGI(TAG, "Web UI: http://%s", wifi_info.ip_addr);
    }
    
    // Main loop - just monitor system health
    while (1) {
        // Check for WiFi shutdown request
        if (s_wifi_shutdown_requested) {
            ESP_LOGI(TAG, "Processing WiFi shutdown request");
            wifi_mgr_stop();
            webserver_stop();
            s_wifi_shutdown_requested = false;
        }

        // Check battery level
        if (power_is_battery_critical()) {
            ESP_LOGW(TAG, "Battery critical! Entering deep sleep.");
            
            // Display low battery warning
            uint8_t *fb = epd_get_framebuffer();
            if (fb) {
                memset(fb, 0xFF, EPAPER_BUFFER_SIZE);
                epd_draw_text(300, 220, "LOW BATTERY", 3, 0);
                epd_draw_text(250, 280, "Please recharge", 2, 0);
                epd_display(fb, EPD_UPDATE_FULL);
            }
            
            epd_sleep();
            power_enter_deep_sleep(0);  // Sleep until button press
        }
        
        // Periodic status log
        static int counter = 0;
        if (++counter >= 60) {
            counter = 0;
            ESP_LOGI(TAG, "Status: Battery=%d%%, Images=%d, WiFi=%s",
                     power_get_battery_percent(),
                     storage_get_image_count(),
                     wifi_mgr_is_active() ? "ON" : "OFF");
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
