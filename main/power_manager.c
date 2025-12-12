/*
 * Power Manager Implementation
 */

#include "power_manager.h"
#include "board_config.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_sleep.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_timer.h"
#include "rom/rtc.h"
#include "soc/rtc.h"

static const char *TAG = "power";

// State
static adc_oneshot_unit_handle_t s_adc_handle = NULL;
static adc_cali_handle_t s_adc_cali = NULL;
static power_event_cb_t s_callback = NULL;
static void *s_callback_ctx = NULL;
static wake_reason_t s_wake_reason = WAKE_REASON_UNKNOWN;
static bool s_initialized = false;

// Buzzer LEDC config
#define BUZZER_LEDC_TIMER       LEDC_TIMER_0
#define BUZZER_LEDC_MODE        LEDC_LOW_SPEED_MODE
#define BUZZER_LEDC_CHANNEL     LEDC_CHANNEL_0

static void determine_wake_reason(void) {
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    
    switch (cause) {
        case ESP_SLEEP_WAKEUP_TIMER:
            s_wake_reason = WAKE_REASON_TIMER;
            ESP_LOGI(TAG, "Wake: Timer");
            break;
            
        case ESP_SLEEP_WAKEUP_EXT1: {
            uint64_t wakeup_pins = esp_sleep_get_ext1_wakeup_status();
            if (wakeup_pins & (1ULL << PIN_BUTTON_K0)) {
                s_wake_reason = WAKE_REASON_BUTTON_K0;
                ESP_LOGI(TAG, "Wake: Button K0 (WiFi)");
            } else if (wakeup_pins & (1ULL << PIN_BUTTON_K1)) {
                s_wake_reason = WAKE_REASON_BUTTON_K1;
                ESP_LOGI(TAG, "Wake: Button K1 (Next)");
            } else if (wakeup_pins & (1ULL << PIN_BUTTON_K2)) {
                s_wake_reason = WAKE_REASON_BUTTON_K2;
                ESP_LOGI(TAG, "Wake: Button K2 (Prev)");
            }
            break;
        }
        
        case ESP_SLEEP_WAKEUP_UNDEFINED:
        default:
            // Check reset reason
            switch (rtc_get_reset_reason(0)) {
                case POWERON_RESET:
                case RTCWDT_RTC_RESET:
                    s_wake_reason = WAKE_REASON_RESET;
                    ESP_LOGI(TAG, "Wake: Power on / Reset");
                    break;
                default:
                    s_wake_reason = WAKE_REASON_UNKNOWN;
                    ESP_LOGI(TAG, "Wake: Unknown");
            }
            break;
    }
}

esp_err_t power_init(void) {
    if (s_initialized) {
        return ESP_OK;
    }
    
    // Determine wake reason first
    determine_wake_reason();
    
    // Configure battery monitoring
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIN_VBAT_EN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(PIN_VBAT_EN, 0);  // Disabled by default
    
    // Configure peripheral power pins
    io_conf.pin_bit_mask = (1ULL << PIN_SD_EN) | (1ULL << PIN_PDM_EN);
    gpio_config(&io_conf);
    
    // Configure ADC for battery voltage
    adc_oneshot_unit_init_cfg_t adc_cfg = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&adc_cfg, &s_adc_handle));
    
    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = VBAT_ADC_ATTEN,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc_handle, ADC_CHANNEL_0, &chan_cfg));
    
    // Calibration
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = ADC_UNIT_1,
        .atten = VBAT_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_12,
    };
    adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_adc_cali);
    
    // Configure buzzer
    ledc_timer_config_t ledc_timer = {
        .speed_mode = BUZZER_LEDC_MODE,
        .timer_num = BUZZER_LEDC_TIMER,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .freq_hz = 2000,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ledc_timer_config(&ledc_timer);
    
    ledc_channel_config_t ledc_channel = {
        .speed_mode = BUZZER_LEDC_MODE,
        .channel = BUZZER_LEDC_CHANNEL,
        .timer_sel = BUZZER_LEDC_TIMER,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = PIN_BUZZER,
        .duty = 0,
        .hpoint = 0
    };
    ledc_channel_config(&ledc_channel);
    
    // Configure wake GPIO
    power_configure_wake_gpio();
    
    s_initialized = true;
    ESP_LOGI(TAG, "Power manager initialized");
    
    return ESP_OK;
}

uint32_t power_get_battery_mv(void) {
    if (!s_adc_handle) return 0;
    
    // Enable battery voltage divider
    gpio_set_level(PIN_VBAT_EN, 1);
    vTaskDelay(pdMS_TO_TICKS(10));  // Settling time
    
    int raw = 0;
    int voltage = 0;
    
    // Average multiple readings
    for (int i = 0; i < 8; i++) {
        int r;
        adc_oneshot_read(s_adc_handle, ADC_CHANNEL_0, &r);
        raw += r;
    }
    raw /= 8;
    
    // Disable voltage divider
    gpio_set_level(PIN_VBAT_EN, 0);
    
    // Convert to voltage
    if (s_adc_cali) {
        adc_cali_raw_to_voltage(s_adc_cali, raw, &voltage);
    } else {
        // Fallback calculation
        voltage = (raw * 3300) / 4095;
    }
    
    // Account for voltage divider
    uint32_t battery_mv = (uint32_t)(voltage * VBAT_DIVIDER_RATIO);
    
    ESP_LOGD(TAG, "Battery: Raw=%d, V_ADC=%d mV, V_BAT=%lu mV", raw, voltage, battery_mv);
    
    return battery_mv;
}

uint8_t power_get_battery_percent(void) {
    uint32_t mv = power_get_battery_mv();
    
    // If voltage is impossibly low (< 2.0V), assume USB power (no battery)
    if (mv < 2000) return 100;
    
    if (mv >= VBAT_FULL_MV) return 100;
    if (mv <= VBAT_EMPTY_MV) return 0;
    
    return (uint8_t)((mv - VBAT_EMPTY_MV) * 100 / (VBAT_FULL_MV - VBAT_EMPTY_MV));
}

bool power_is_battery_low(void) {
    return power_get_battery_percent() < 20;
}

bool power_is_battery_critical(void) {
    return power_get_battery_percent() < 10;
}

void power_configure_wake_gpio(void) {
    // Configure buttons as wake sources (active low)
    gpio_config_t io_conf = {
        .pin_bit_mask = WAKEUP_BUTTON_MASK,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    gpio_config(&io_conf);
    
    // Configure EXT1 wake (wake on any button press - LOW level)
    esp_sleep_enable_ext1_wakeup(WAKEUP_BUTTON_MASK, ESP_EXT1_WAKEUP_ANY_LOW);
}

void power_enter_deep_sleep(uint32_t sleep_seconds) {
    ESP_LOGI(TAG, "Entering deep sleep for %lu seconds", sleep_seconds);
    
    // Disable peripherals to save power
    power_set_sd_power(false);
    power_set_mic_power(false);
    gpio_set_level(PIN_VBAT_EN, 0);
    
    // Configure timer wake if duration specified
    if (sleep_seconds > 0) {
        esp_sleep_enable_timer_wakeup(sleep_seconds * 1000000ULL);
    }
    
    // Wake on buttons is already configured
    
    // Enter deep sleep
    esp_deep_sleep_start();
}

wake_reason_t power_get_wake_reason(void) {
    return s_wake_reason;
}

void power_register_callback(power_event_cb_t cb, void *ctx) {
    s_callback = cb;
    s_callback_ctx = ctx;
}

void power_set_sd_power(bool enable) {
    gpio_set_level(PIN_SD_EN, enable ? 1 : 0);
    if (enable) {
        vTaskDelay(pdMS_TO_TICKS(100));  // Power stabilization
    }
}

void power_set_mic_power(bool enable) {
    gpio_set_level(PIN_PDM_EN, enable ? 1 : 0);
}

uint32_t power_get_uptime_sec(void) {
    return (uint32_t)(esp_timer_get_time() / 1000000);
}

bool power_was_deep_sleep(void) {
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    return cause != ESP_SLEEP_WAKEUP_UNDEFINED;
}

void power_buzzer_beep(uint32_t frequency, uint32_t duration) {
    if (frequency == 0) {
        ledc_set_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL, 0);
        ledc_update_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL);
        return;
    }
    
    ledc_set_freq(BUZZER_LEDC_MODE, BUZZER_LEDC_TIMER, frequency);
    ledc_set_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL, 512);  // 50% duty
    ledc_update_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL);
    
    vTaskDelay(pdMS_TO_TICKS(duration));
    
    ledc_set_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL, 0);
    ledc_update_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL);
}
