/*
 * Power Manager - Deep sleep, battery monitoring, power saving
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// Wake reasons
typedef enum {
    WAKE_REASON_UNKNOWN,
    WAKE_REASON_TIMER,
    WAKE_REASON_BUTTON_K0,      // WiFi button
    WAKE_REASON_BUTTON_K1,      // Next image
    WAKE_REASON_BUTTON_K2,      // Previous image
    WAKE_REASON_RESET
} wake_reason_t;

// Power events callback
typedef void (*power_event_cb_t)(wake_reason_t reason, void *ctx);

/**
 * @brief Initialize power manager
 * @return ESP_OK on success
 */
esp_err_t power_init(void);

/**
 * @brief Get battery voltage in millivolts
 */
uint32_t power_get_battery_mv(void);

/**
 * @brief Get battery percentage (0-100)
 */
uint8_t power_get_battery_percent(void);

/**
 * @brief Check if battery is low (<20%)
 */
bool power_is_battery_low(void);

/**
 * @brief Check if battery is critical (<10%)
 */
bool power_is_battery_critical(void);

/**
 * @brief Enter deep sleep for specified duration
 * @param sleep_seconds Duration in seconds (0 = indefinite until button)
 */
void power_enter_deep_sleep(uint32_t sleep_seconds);

/**
 * @brief Get the reason for waking from sleep
 */
wake_reason_t power_get_wake_reason(void);

/**
 * @brief Configure GPIO wake sources
 */
void power_configure_wake_gpio(void);

/**
 * @brief Register callback for power events
 */
void power_register_callback(power_event_cb_t cb, void *ctx);

/**
 * @brief Enable/disable peripheral power (for power saving)
 */
void power_set_sd_power(bool enable);
void power_set_mic_power(bool enable);

/**
 * @brief Get uptime in seconds since boot
 */
uint32_t power_get_uptime_sec(void);

/**
 * @brief Check if wake was from deep sleep
 */
bool power_was_deep_sleep(void);

/**
 * @brief Play buzzer tone
 * @param frequency Tone frequency in Hz
 * @param duration Duration in ms
 */
void power_buzzer_beep(uint32_t frequency, uint32_t duration);

/**
 * @brief Play a pleasant chord
 */
void power_buzzer_chord(void);
