idf_component_register(
    SRCS 
        "main.c"
        "wifi_manager.c"
        "dns_server.c"
        "epaper_driver.c"
        "storage_manager.c"
        "web_server.c"
        "power_manager.c"
        "image_processor.c"
        "carousel.c"
        "display_overlay.c"
        "sht40.c"
    INCLUDE_DIRS 
        "."
    REQUIRES 
        driver
        esp_wifi
        esp_event
        esp_http_server
        esp_netif
        nvs_flash
        fatfs
        sdmmc
        esp_timer
        esp_adc
        esp_pm
        spi_flash
        esp_psram
        json
        spiffs
)

# Embed web files
spiffs_create_partition_image(storage ../spiffs FLASH_IN_PROJECT)
