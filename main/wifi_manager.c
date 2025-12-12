/*
 * WiFi Manager Implementation
 */

#include "wifi_manager.h"
#include "board_config.h"
#include "dns_server.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "lwip/inet.h"

static const char *TAG = "wifi_mgr";

// Event bits
#define WIFI_CONNECTED_BIT      BIT0
#define WIFI_FAIL_BIT           BIT1
#define WIFI_SCAN_DONE_BIT      BIT2

// State
static EventGroupHandle_t s_wifi_event_group = NULL;
static esp_netif_t *s_sta_netif = NULL;
static esp_netif_t *s_ap_netif = NULL;
static wifi_mgr_info_t s_info = {0};
static wifi_mgr_callback_t s_callback = NULL;
static void *s_callback_ctx = NULL;
static int s_retry_count = 0;
static bool s_initialized = false;

#define MAX_RETRY 5

// NVS keys
#define NVS_KEY_SSID "wifi_ssid"
#define NVS_KEY_PASS "wifi_pass"

static void notify_callback(wifi_mgr_status_t status) {
    s_info.status = status;
    if (s_callback) {
        s_callback(status, s_callback_ctx);
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "STA started, connecting...");
                esp_wifi_connect();
                notify_callback(WIFI_MGR_STATUS_CONNECTING);
                break;
                
            case WIFI_EVENT_STA_DISCONNECTED: {
                wifi_event_sta_disconnected_t *evt = event_data;
                ESP_LOGW(TAG, "Disconnected, reason: %d", evt->reason);
                
                if (s_retry_count < MAX_RETRY) {
                    s_retry_count++;
                    ESP_LOGI(TAG, "Retry %d/%d", s_retry_count, MAX_RETRY);
                    esp_wifi_connect();
                } else {
                    xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
                    notify_callback(WIFI_MGR_STATUS_DISCONNECTED);
                }
                break;
            }
            
            case WIFI_EVENT_AP_START:
                ESP_LOGI(TAG, "AP started");
                notify_callback(WIFI_MGR_STATUS_AP_ACTIVE);
                break;
                
            case WIFI_EVENT_AP_STACONNECTED: {
                wifi_event_ap_staconnected_t *evt = (wifi_event_ap_staconnected_t *)event_data;
                ESP_LOGI(TAG, "Station connected: %02X:%02X:%02X:%02X:%02X:%02X", 
                         evt->mac[0], evt->mac[1], evt->mac[2], evt->mac[3], evt->mac[4], evt->mac[5]);
                notify_callback(WIFI_MGR_STATUS_AP_STATION_CONNECTED);
                break;
            }
            
            case WIFI_EVENT_AP_STADISCONNECTED: {
                wifi_event_ap_stadisconnected_t *evt = (wifi_event_ap_stadisconnected_t *)event_data;
                ESP_LOGI(TAG, "Station disconnected: %02X:%02X:%02X:%02X:%02X:%02X",
                         evt->mac[0], evt->mac[1], evt->mac[2], evt->mac[3], evt->mac[4], evt->mac[5]);
                notify_callback(WIFI_MGR_STATUS_AP_STATION_DISCONNECTED);
                break;
            }
            
            case WIFI_EVENT_SCAN_DONE:
                xEventGroupSetBits(s_wifi_event_group, WIFI_SCAN_DONE_BIT);
                break;
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t *event = event_data;
            snprintf(s_info.ip_addr, sizeof(s_info.ip_addr), IPSTR, 
                     IP2STR(&event->ip_info.ip));
            ESP_LOGI(TAG, "Got IP: %s", s_info.ip_addr);
            s_retry_count = 0;
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
            notify_callback(WIFI_MGR_STATUS_CONNECTED);
        }
    }
}

esp_err_t wifi_mgr_init(void) {
    if (s_initialized) {
        return ESP_OK;
    }
    
    s_wifi_event_group = xEventGroupCreate();
    if (!s_wifi_event_group) {
        return ESP_ERR_NO_MEM;
    }
    
    ESP_ERROR_CHECK(esp_netif_init());
    
    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif = esp_netif_create_default_wifi_ap();
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));
    
    // Set AP IP info
    snprintf(s_info.ap_ip_addr, sizeof(s_info.ap_ip_addr), "192.168.4.1");
    
    s_initialized = true;
    s_info.status = WIFI_MGR_STATUS_OFF;
    s_info.mode = WIFI_MGR_MODE_OFF;
    
    ESP_LOGI(TAG, "Initialized");
    return ESP_OK;
}

void wifi_mgr_deinit(void) {
    if (!s_initialized) return;
    
    wifi_mgr_stop();
    esp_wifi_deinit();
    
    if (s_sta_netif) {
        esp_netif_destroy(s_sta_netif);
        s_sta_netif = NULL;
    }
    if (s_ap_netif) {
        esp_netif_destroy(s_ap_netif);
        s_ap_netif = NULL;
    }
    
    if (s_wifi_event_group) {
        vEventGroupDelete(s_wifi_event_group);
        s_wifi_event_group = NULL;
    }
    
    s_initialized = false;
}

esp_err_t wifi_mgr_start_ap(const char *ssid, const char *password) {
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    const char *ap_ssid = ssid ? ssid : DEFAULT_AP_SSID;
    // Password ignored for open network
    
    wifi_config_t wifi_config = {
        .ap = {
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN,
            .pmf_cfg = {
                .required = false,
            },
        },
    };
    
    strlcpy((char *)wifi_config.ap.ssid, ap_ssid, sizeof(wifi_config.ap.ssid));
    wifi_config.ap.ssid_len = strlen(ap_ssid);
    wifi_config.ap.password[0] = '\0';
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    strlcpy(s_info.ap_ssid, ap_ssid, sizeof(s_info.ap_ssid));
    s_info.mode = WIFI_MGR_MODE_AP;
    
    // Start DNS Server for Captive Portal
    dns_server_start();
    
    ESP_LOGI(TAG, "AP started: %s (Open)", ap_ssid);
    return ESP_OK;
}

esp_err_t wifi_mgr_start_sta(void) {
    wifi_credentials_t creds;
    
    if (wifi_mgr_get_credentials(&creds) != ESP_OK) {
        ESP_LOGW(TAG, "No stored credentials");
        return ESP_ERR_NOT_FOUND;
    }
    
    return wifi_mgr_connect(creds.ssid, creds.password, false);
}

esp_err_t wifi_mgr_connect(const char *ssid, const char *password, bool save) {
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
        },
    };
    
    strlcpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password));
    
    // Save credentials if requested
    if (save) {
        nvs_handle_t nvs;
        if (nvs_open(SETTINGS_NVS_NAMESPACE, NVS_READWRITE, &nvs) == ESP_OK) {
            nvs_set_str(nvs, NVS_KEY_SSID, ssid);
            nvs_set_str(nvs, NVS_KEY_PASS, password);
            nvs_commit(nvs);
            nvs_close(nvs);
            ESP_LOGI(TAG, "Credentials saved");
        }
    }
    
    s_retry_count = 0;
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    
    // Check current mode and adjust
    wifi_mode_t current_mode;
    esp_wifi_get_mode(&current_mode);
    
    if (current_mode == WIFI_MODE_AP) {
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
        s_info.mode = WIFI_MGR_MODE_APSTA;
    } else {
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        s_info.mode = WIFI_MGR_MODE_STA;
    }
    
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    strlcpy(s_info.sta_ssid, ssid, sizeof(s_info.sta_ssid));
    
    ESP_LOGI(TAG, "Connecting to %s...", ssid);
    
    // Wait for connection
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE, pdFALSE, pdMS_TO_TICKS(30000));
    
    if (bits & WIFI_CONNECTED_BIT) {
        return ESP_OK;
    } else {
        return ESP_FAIL;
    }
}

void wifi_mgr_stop(void) {
    if (!s_initialized) return;
    
    dns_server_stop();
    esp_wifi_stop();
    s_info.status = WIFI_MGR_STATUS_OFF;
    s_info.mode = WIFI_MGR_MODE_OFF;
    s_info.ip_addr[0] = '\0';
    
    notify_callback(WIFI_MGR_STATUS_OFF);
    ESP_LOGI(TAG, "WiFi stopped");
}

esp_err_t wifi_mgr_get_info(wifi_mgr_info_t *info) {
    if (!info) return ESP_ERR_INVALID_ARG;
    
    // Update RSSI if connected
    if (s_info.status == WIFI_MGR_STATUS_CONNECTED) {
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            s_info.rssi = ap_info.rssi;
        }
    }
    
    memcpy(info, &s_info, sizeof(wifi_mgr_info_t));
    return ESP_OK;
}

bool wifi_mgr_has_credentials(void) {
    nvs_handle_t nvs;
    bool has = false;
    
    if (nvs_open(SETTINGS_NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
        size_t len;
        has = (nvs_get_str(nvs, NVS_KEY_SSID, NULL, &len) == ESP_OK && len > 1);
        nvs_close(nvs);
    }
    
    return has;
}

esp_err_t wifi_mgr_get_credentials(wifi_credentials_t *creds) {
    if (!creds) return ESP_ERR_INVALID_ARG;
    
    nvs_handle_t nvs;
    esp_err_t ret = nvs_open(SETTINGS_NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (ret != ESP_OK) return ret;
    
    size_t len = sizeof(creds->ssid);
    ret = nvs_get_str(nvs, NVS_KEY_SSID, creds->ssid, &len);
    if (ret != ESP_OK) {
        nvs_close(nvs);
        return ret;
    }
    
    len = sizeof(creds->password);
    ret = nvs_get_str(nvs, NVS_KEY_PASS, creds->password, &len);
    nvs_close(nvs);
    
    if (ret == ESP_OK) {
        creds->valid = true;
    }
    
    return ret;
}

esp_err_t wifi_mgr_clear_credentials(void) {
    nvs_handle_t nvs;
    esp_err_t ret = nvs_open(SETTINGS_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (ret != ESP_OK) return ret;
    
    nvs_erase_key(nvs, NVS_KEY_SSID);
    nvs_erase_key(nvs, NVS_KEY_PASS);
    nvs_commit(nvs);
    nvs_close(nvs);
    
    ESP_LOGI(TAG, "Credentials cleared");
    return ESP_OK;
}

void wifi_mgr_register_callback(wifi_mgr_callback_t callback, void *ctx) {
    s_callback = callback;
    s_callback_ctx = ctx;
}

int wifi_mgr_scan(char results[][33], int max_results) {
    if (!s_initialized || max_results <= 0) return 0;
    
    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);
    
    // Need to be in STA or APSTA mode to scan
    if (mode == WIFI_MODE_NULL || mode == WIFI_MODE_AP) {
        esp_wifi_set_mode(WIFI_MODE_APSTA);
        esp_wifi_start();
    }
    
    wifi_scan_config_t scan_config = {
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 100,
        .scan_time.active.max = 300,
    };
    
    xEventGroupClearBits(s_wifi_event_group, WIFI_SCAN_DONE_BIT);
    esp_wifi_scan_start(&scan_config, false);
    
    xEventGroupWaitBits(s_wifi_event_group, WIFI_SCAN_DONE_BIT, 
                        pdTRUE, pdFALSE, pdMS_TO_TICKS(10000));
    
    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    
    if (ap_count == 0) {
        return 0;
    }
    
    int count = (ap_count < max_results) ? ap_count : max_results;
    wifi_ap_record_t *ap_records = malloc(count * sizeof(wifi_ap_record_t));
    if (!ap_records) return 0;
    
    uint16_t get_count = count;
    esp_wifi_scan_get_ap_records(&get_count, ap_records);
    
    for (int i = 0; i < get_count; i++) {
        strlcpy(results[i], (char *)ap_records[i].ssid, 33);
    }
    
    free(ap_records);
    return get_count;
}

bool wifi_mgr_is_active(void) {
    return s_info.mode != WIFI_MGR_MODE_OFF;
}

void wifi_mgr_toggle(void) {
    if (wifi_mgr_is_active()) {
        wifi_mgr_stop();
    } else {
        // Try STA first, fall back to AP
        if (wifi_mgr_has_credentials()) {
            if (wifi_mgr_start_sta() != ESP_OK) {
                wifi_mgr_start_ap(NULL, NULL);
            }
        } else {
            wifi_mgr_start_ap(NULL, NULL);
        }
    }
}
