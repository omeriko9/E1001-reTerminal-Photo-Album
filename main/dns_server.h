#pragma once

#include "esp_err.h"

/**
 * @brief Start the DNS server (Captive Portal)
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t dns_server_start(void);

/**
 * @brief Stop the DNS server
 */
void dns_server_stop(void);
