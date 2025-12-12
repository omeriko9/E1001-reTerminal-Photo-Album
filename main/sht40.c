#include "sht40.h"
#include "board_config.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "sht40";

#define SHT40_ADDR          0x44
#define SHT40_CMD_MEASURE   0xFD
#define I2C_MASTER_TIMEOUT_MS 1000

esp_err_t sht40_init(void) {
    // I2C should be initialized in main.c
    // We can just check if the device is present
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (SHT40_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "SHT40 found at 0x%02x", SHT40_ADDR);
    } else {
        ESP_LOGW(TAG, "SHT40 not found at 0x%02x", SHT40_ADDR);
    }
    
    return ret;
}

esp_err_t sht40_read_temp_humid(float *temp, float *humid) {
    uint8_t data[6];
    
    // Send measurement command
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (SHT40_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, SHT40_CMD_MEASURE, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send measure command");
        return ret;
    }
    
    // Wait for measurement (datasheet says max 10ms for high precision)
    vTaskDelay(pdMS_TO_TICKS(20));
    
    // Read data
    cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (SHT40_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read(cmd, data, 6, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read data");
        return ret;
    }
    
    // Convert data
    uint16_t t_raw = (data[0] << 8) | data[1];
    uint16_t h_raw = (data[3] << 8) | data[4];
    
    if (temp) {
        *temp = -45.0f + 175.0f * ((float)t_raw / 65535.0f);
    }
    
    if (humid) {
        *humid = -6.0f + 125.0f * ((float)h_raw / 65535.0f);
        if (*humid < 0) *humid = 0;
        if (*humid > 100) *humid = 100;
    }
    
    return ESP_OK;
}
