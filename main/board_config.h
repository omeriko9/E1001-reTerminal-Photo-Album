/*
 * E1001 reTerminal Board Configuration
 * GPIO and Hardware Definitions for ESP32-S3
 */

#pragma once

#include "driver/gpio.h"

// ============================================================================
// WiFi Configuration
// ============================================================================
#define DEFAULT_WIFI_TIMEOUT_SEC 300    // Auto-off after 5 minutes
#define DEFAULT_AP_SSID         "E1001_Frame"

// ============================================================================
// User Buttons (Active LOW with pull-up)
// ============================================================================
#define PIN_BUTTON_K0           GPIO_NUM_3
#define PIN_BUTTON_K1           GPIO_NUM_4
#define PIN_BUTTON_K2           GPIO_NUM_5

#define BUTTON_WIFI_TOGGLE      PIN_BUTTON_K0   // K0: Toggle WiFi on/off
#define BUTTON_NEXT_IMAGE       PIN_BUTTON_K1   // K1: Next image
#define BUTTON_PREV_IMAGE       PIN_BUTTON_K2   // K2: Previous image

// ============================================================================
// Status LED (Active HIGH)
// ============================================================================
#define PIN_LED_STATUS          GPIO_NUM_6

// ============================================================================
// SPI Bus (SD Card & e-Paper shared)
// ============================================================================
#define PIN_SPI_CLK             GPIO_NUM_7
#define PIN_SPI_MISO            GPIO_NUM_8
#define PIN_SPI_MOSI            GPIO_NUM_9

// ============================================================================
// e-Paper Display (800x480)
// ============================================================================
#define PIN_EPAPER_CS           GPIO_NUM_10
#define PIN_EPAPER_DC           GPIO_NUM_11
#define PIN_EPAPER_RST          GPIO_NUM_12
#define PIN_EPAPER_BUSY         GPIO_NUM_13

#define EPAPER_WIDTH            800
#define EPAPER_HEIGHT           480
#define EPAPER_BUFFER_SIZE      (EPAPER_WIDTH * EPAPER_HEIGHT / 8)

// ============================================================================
// SD Card
// ============================================================================
#define PIN_SD_CS               GPIO_NUM_14
#define PIN_SD_DET              GPIO_NUM_15     // Card detect (LOW = inserted)
#define PIN_SD_EN               GPIO_NUM_16     // Power enable (HIGH = on)

// ============================================================================
// UART1 (External)
// ============================================================================
#define PIN_UART1_TX            GPIO_NUM_17
#define PIN_UART1_RX            GPIO_NUM_18

// ============================================================================
// I2C Bus 0 (Sensors, RTC, etc.)
// ============================================================================
#define PIN_I2C0_SDA            GPIO_NUM_19
#define PIN_I2C0_SCL            GPIO_NUM_20
#define I2C0_FREQ_HZ            400000

// ============================================================================
// Battery Monitoring
// ============================================================================
#define PIN_VBAT_EN             GPIO_NUM_21     // Enable battery voltage divider
#define PIN_VBAT_ADC            GPIO_NUM_1      // ADC1_CH0
#define PIN_ADC_EXTRA           GPIO_NUM_2      // ADC1_CH1 (extra analog input)

// Battery voltage divider calibration (adjust based on actual resistors)
#define VBAT_DIVIDER_RATIO      2.0f            // Assuming 1:1 divider
#define VBAT_ADC_ATTEN          ADC_ATTEN_DB_12 // 0-3.3V range
#define VBAT_FULL_MV            4200
#define VBAT_EMPTY_MV           3300

// ============================================================================
// PDM Microphone
// ============================================================================
#define PIN_PDM_EN              GPIO_NUM_38     // Enable mic power
#define PIN_PDM_DATA            GPIO_NUM_41
#define PIN_PDM_CLK             GPIO_NUM_42

// ============================================================================
// I2C Bus 1 (Touch Controller)
// ============================================================================
#define PIN_I2C1_SDA            GPIO_NUM_39
#define PIN_I2C1_SCL            GPIO_NUM_40
#define I2C1_FREQ_HZ            400000

// ============================================================================
// Touch Controller
// ============================================================================
#define PIN_TOUCH_RST           GPIO_NUM_48
#define PIN_TOUCH_INT           GPIO_NUM_47

// ============================================================================
// Buzzer
// ============================================================================
#define PIN_BUZZER              GPIO_NUM_45

// ============================================================================
// Misc
// ============================================================================
#define PIN_GPIO46              GPIO_NUM_46     // Strapping / header GPIO

// ============================================================================
// SPI Configuration
// ============================================================================
#define SPI_HOST_USED           SPI2_HOST
#define SPI_DMA_CHAN            SPI_DMA_CH_AUTO
#define SPI_MAX_TRANSFER_SIZE   (EPAPER_WIDTH * 16)

// ============================================================================
// Deep Sleep Configuration
// ============================================================================
#define WAKEUP_BUTTON_MASK      ((1ULL << PIN_BUTTON_K0) | \
                                 (1ULL << PIN_BUTTON_K1) | \
                                 (1ULL << PIN_BUTTON_K2))

// ============================================================================
// Default Settings
// ============================================================================
#define DEFAULT_CAROUSEL_INTERVAL_SEC   300     // 5 minutes
#define DEFAULT_WIFI_TIMEOUT_SEC        300     // 5 minutes (increased for stability)
#define DEFAULT_DEEP_SLEEP_SEC          3600    // 1 hour max sleep
#define DEFAULT_AP_SSID                 "E1001-Setup"
#define DEFAULT_AP_PASS                 "12345678"

// ============================================================================
// Storage Paths
// ============================================================================
#define SD_MOUNT_POINT          "/sdcard"
#define IMAGES_DIR              "/sdcard/images"
#define SETTINGS_NVS_NAMESPACE  "e1001_cfg"
