/*
 * Display Overlay Implementation
 */

#include "display_overlay.h"
#include "board_config.h"

#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "esp_log.h"
#include "esp_sntp.h"

static const char *TAG = "overlay";

void overlay_get_default_config(overlay_config_t *config) {
    config->show_datetime = true;
    config->show_temperature = true;
    config->show_battery = true;
    config->show_wifi = true;
    config->timezone_offset = 0;
    config->font_size = 1;
    config->datetime_color = 0;  // Black on white background
    config->datetime_pos = OVERLAY_POS_BOTTOM_LEFT;
    config->temp_pos = OVERLAY_POS_BOTTOM_RIGHT;
    config->battery_pos = OVERLAY_POS_TOP_RIGHT;
}

static void get_position(overlay_pos_t pos, int text_width, int text_height,
                         int *x, int *y) {
    int margin = 10;
    
    switch (pos) {
        case OVERLAY_POS_TOP_LEFT:
            *x = margin;
            *y = margin;
            break;
        case OVERLAY_POS_TOP_RIGHT:
            *x = EPAPER_WIDTH - text_width - margin;
            *y = margin;
            break;
        case OVERLAY_POS_BOTTOM_LEFT:
            *x = margin;
            *y = EPAPER_HEIGHT - text_height - margin;
            break;
        case OVERLAY_POS_BOTTOM_RIGHT:
        default:
            *x = EPAPER_WIDTH - text_width - margin;
            *y = EPAPER_HEIGHT - text_height - margin;
            break;
    }
}

void overlay_draw_datetime(uint8_t *fb, int x, int y, int font_size, 
                           uint8_t color, int8_t tz_offset) {
    time_t now;
    struct tm timeinfo;
    
    time(&now);
    now += tz_offset * 3600;  // Apply timezone offset
    localtime_r(&now, &timeinfo);
    
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &timeinfo);
    
    // Draw background box for readability
    int text_w = epd_get_text_width_large(buf, font_size);
    int text_h = 24 * font_size;
    int pad = 4;
    
    // Invert color for background
    epd_fill_rect(x - pad, y - pad, text_w + pad * 2, text_h + pad * 2, color ? 0 : 1);
    epd_draw_text_large(x, y, buf, font_size, color);
}

void overlay_draw_battery(uint8_t *fb, int x, int y, int font_size,
                          uint8_t color, uint8_t percent) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d%%", percent);
    
    // Draw battery icon approximation
    int icon_w = 24 * font_size;
    int icon_h = 12 * font_size;
    int pad = 4;
    
    // Background
    epd_fill_rect(x - pad, y - pad, icon_w + epd_get_text_width_large(buf, font_size) + pad * 3, 
                  icon_h + pad * 2, color ? 0 : 1);
    
    // Battery outline
    epd_draw_rect(x, y, 20 * font_size, 10 * font_size, color);
    epd_fill_rect(x + 20 * font_size, y + 3 * font_size, 3 * font_size, 4 * font_size, color);
    
    // Fill level
    int fill_w = (16 * font_size * percent) / 100;
    if (fill_w > 0) {
        epd_fill_rect(x + 2, y + 2, fill_w, 6 * font_size, color);
    }
    
    // Percentage text
    epd_draw_text_large(x + icon_w + pad, y - 2, buf, font_size, color);
}

void overlay_draw_temperature(uint8_t *fb, int x, int y, int font_size,
                              uint8_t color, float celsius) {
    char buf[16];
    
    if (celsius < -100) {
        snprintf(buf, sizeof(buf), "--C");
    } else {
        snprintf(buf, sizeof(buf), "%.1fC", celsius);
    }
    
    int text_w = epd_get_text_width_large(buf, font_size);
    int text_h = 24 * font_size;
    int pad = 4;
    
    epd_fill_rect(x - pad, y - pad, text_w + pad * 2, text_h + pad * 2, color ? 0 : 1);
    epd_draw_text_large(x, y, buf, font_size, color);
}

void overlay_draw_wifi(uint8_t *fb, int x, int y, int font_size,
                       uint8_t color, bool connected) {
    const char *text = connected ? "WiFi" : "----";
    
    int text_w = epd_get_text_width_large(text, font_size);
    int text_h = 24 * font_size;
    int pad = 4;
    
    epd_fill_rect(x - pad, y - pad, text_w + pad * 2, text_h + pad * 2, color ? 0 : 1);
    epd_draw_text_large(x, y, text, font_size, color);
}

void overlay_draw(uint8_t *fb, const overlay_config_t *config,
                  uint8_t battery_percent, float temp_celsius, bool wifi_connected) {
    if (!fb || !config) return;
    
    int text_h = 24 * config->font_size;
    int x, y;
    
    // Draw date/time
    if (config->show_datetime) {
        char buf[32];
        time_t now;
        struct tm timeinfo;
        time(&now);
        now += config->timezone_offset * 3600;
        localtime_r(&now, &timeinfo);
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &timeinfo);
        
        int text_w = epd_get_text_width(buf, config->font_size);
        get_position(config->datetime_pos, text_w + 10, text_h + 10, &x, &y);
        overlay_draw_datetime(fb, x, y, config->font_size, config->datetime_color, 
                              config->timezone_offset);
    }
    
    // Draw temperature
    if (config->show_temperature) {
        char buf[16];
        ESP_LOGI(TAG, "The temperature value is: %.2f", temp_celsius);
        if (temp_celsius < -100) {
            
            snprintf(buf, sizeof(buf), "--C");
        } else {
            snprintf(buf, sizeof(buf), "%.1fC", temp_celsius);
        }
        
        int text_w = epd_get_text_width(buf, config->font_size);
        get_position(config->temp_pos, text_w + 10, text_h + 10, &x, &y);
        overlay_draw_temperature(fb, x, y, config->font_size, config->datetime_color, temp_celsius);
    }
    
    // Draw battery
    if (config->show_battery) {
        int icon_w = 60 * config->font_size;
        get_position(config->battery_pos, icon_w, text_h + 10, &x, &y);
        overlay_draw_battery(fb, x, y, config->font_size, config->datetime_color, battery_percent);
    }
    
    // Draw WiFi status
    if (config->show_wifi) {
        int text_w = epd_get_text_width("WiFi", config->font_size);
        // Position next to battery
        get_position(OVERLAY_POS_TOP_LEFT, text_w + 10, text_h + 10, &x, &y);
        overlay_draw_wifi(fb, x, y, config->font_size, config->datetime_color, wifi_connected);
    }
}

void overlay_set_time(time_t epoch) {
    struct timeval tv = {
        .tv_sec = epoch,
        .tv_usec = 0
    };
    settimeofday(&tv, NULL);
    ESP_LOGI(TAG, "Time set to %ld", epoch);
}

static void sntp_sync_callback(struct timeval *tv) {
    ESP_LOGI(TAG, "SNTP sync complete");
}

esp_err_t overlay_sync_time(void) {
    ESP_LOGI(TAG, "Starting SNTP sync...");
    
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.google.com");
    esp_sntp_setservername(2, "time.cloudflare.com");
    esp_sntp_set_time_sync_notification_cb(sntp_sync_callback);
    esp_sntp_init();
    
    // Wait for sync (with timeout)
    int retry = 0;
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && retry < 30) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        retry++;
    }
    
    if (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED) {
        ESP_LOGW(TAG, "SNTP sync timeout");
        return ESP_ERR_TIMEOUT;
    }
    
    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
    ESP_LOGI(TAG, "Time synced: %s", buf);
    
    return ESP_OK;
}
