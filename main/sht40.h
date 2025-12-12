#pragma once

#include "esp_err.h"

// Initialize SHT40 sensor
esp_err_t sht40_init(void);

// Read temperature and humidity
// Returns ESP_OK on success
esp_err_t sht40_read_temp_humid(float *temp, float *humid);
