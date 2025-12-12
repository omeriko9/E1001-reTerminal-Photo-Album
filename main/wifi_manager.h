/*
 * WiFi Manager - AP Provisioning and STA Connection
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

// WiFi modes
typedef enum {
    WIFI_MGR_MODE_OFF,
    WIFI_MGR_MODE_AP,       // Access Point for provisioning
    WIFI_MGR_MODE_STA,      // Station connected to user's network
    WIFI_MGR_MODE_APSTA     // Both modes
} wifi_mgr_mode_t;

// WiFi status
typedef enum {
    WIFI_MGR_STATUS_OFF,
    WIFI_MGR_STATUS_AP_ACTIVE,
    WIFI_MGR_STATUS_CONNECTING,
    WIFI_MGR_STATUS_CONNECTED,
    WIFI_MGR_STATUS_DISCONNECTED,
    WIFI_MGR_STATUS_FAILED,
    WIFI_MGR_STATUS_AP_STATION_CONNECTED,
    WIFI_MGR_STATUS_AP_STATION_DISCONNECTED
} wifi_mgr_status_t;

// Stored WiFi credentials
typedef struct {
    char ssid[33];
    char password[65];
    bool valid;
} wifi_credentials_t;

// WiFi info structure
typedef struct {
    wifi_mgr_status_t status;
    wifi_mgr_mode_t mode;
    char ap_ssid[33];
    char sta_ssid[33];
    char ip_addr[16];
    char ap_ip_addr[16];
    int8_t rssi;
} wifi_mgr_info_t;

// Callback for WiFi events
typedef void (*wifi_mgr_callback_t)(wifi_mgr_status_t status, void *ctx);

/**
 * @brief Initialize WiFi manager
 * @return ESP_OK on success
 */
esp_err_t wifi_mgr_init(void);

/**
 * @brief Deinitialize WiFi manager
 */
void wifi_mgr_deinit(void);

/**
 * @brief Start Access Point mode for provisioning
 * @param ssid AP SSID (NULL for default)
 * @param password AP password (NULL for default)
 * @return ESP_OK on success
 */
esp_err_t wifi_mgr_start_ap(const char *ssid, const char *password);

/**
 * @brief Start Station mode and connect to saved network
 * @return ESP_OK on success
 */
esp_err_t wifi_mgr_start_sta(void);

/**
 * @brief Connect to a specific network
 * @param ssid Network SSID
 * @param password Network password
 * @param save Save credentials for future use
 * @return ESP_OK on success
 */
esp_err_t wifi_mgr_connect(const char *ssid, const char *password, bool save);

/**
 * @brief Disconnect and stop WiFi
 */
void wifi_mgr_stop(void);

/**
 * @brief Get current WiFi status and info
 * @param info Output info structure
 * @return ESP_OK on success
 */
esp_err_t wifi_mgr_get_info(wifi_mgr_info_t *info);

/**
 * @brief Check if credentials are stored
 * @return true if valid credentials exist
 */
bool wifi_mgr_has_credentials(void);

/**
 * @brief Get stored credentials
 * @param creds Output credentials
 * @return ESP_OK if credentials exist
 */
esp_err_t wifi_mgr_get_credentials(wifi_credentials_t *creds);

/**
 * @brief Clear stored credentials
 * @return ESP_OK on success
 */
esp_err_t wifi_mgr_clear_credentials(void);

/**
 * @brief Register callback for WiFi events
 * @param callback Callback function
 * @param ctx User context
 */
void wifi_mgr_register_callback(wifi_mgr_callback_t callback, void *ctx);

/**
 * @brief Scan for available networks
 * @param results Output array of SSIDs
 * @param max_results Maximum results to return
 * @return Number of networks found
 */
int wifi_mgr_scan(char results[][33], int max_results);

/**
 * @brief Check if WiFi is currently active
 */
bool wifi_mgr_is_active(void);

/**
 * @brief Toggle WiFi on/off (for button handler)
 */
void wifi_mgr_toggle(void);
