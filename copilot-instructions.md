# ESP-IDF v5.5 Project for reTerminal E1001

## Overview

This is a complete ESP-IDF v5.5 firmware project for the reTerminal E1001 (ESP32-S3 based device) featuring:

- **WiFi Provisioning**: AP mode setup with web interface, automatic STA connection
- **E-ink Display**: 800x480 UC8179-compatible display with carousel functionality
- **Image Management**: WebUI for uploading/deleting images, BMP/PNG/JPG support with dithering
- **Power Management**: Deep sleep between images, battery monitoring, buzzer feedback
- **Display Overlays**: Date/time, temperature, battery status overlays
- **Button Controls**: K0 (WiFi toggle), K1 (Next), K2 (Previous)
- **Storage**: SD card for images, NVS for settings

## Hardware Requirements

- **reTerminal E1001** with ESP32-S3, 16MB flash, 8MB PSRAM
- **800x480 E-ink Display** (UC8179 compatible)
- **SD Card** (FAT32 formatted)
- **Battery** (ADC monitoring on GPIO1)
- **Buttons**: K0 (GPIO3), K1 (GPIO4), K2 (GPIO5)
- **Buzzer**: GPIO45 (LEDC PWM)
- **USB Serial**: COM14 (or your port)

## Software Requirements

- **ESP-IDF v5.5**: Install from https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/get-started/index.html
- **Python 3.8+**: For build tools
- **Git**: For cloning repositories

## Project Structure

```
E1001/
├── CMakeLists.txt              # Project configuration
├── sdkconfig.defaults          # Default SDK configuration
├── partitions.csv              # Custom partition table
├── main/
│   ├── board_config.h          # GPIO pin definitions
│   ├── wifi_manager.h/.c       # WiFi AP/STA management
│   ├── epaper_driver.h/.c      # 800x480 e-ink display driver
│   ├── storage_manager.h/.c    # SD card + NVS storage
│   ├── image_processor.h/.c    # BMP decode + dithering
│   ├── web_server.h/.c        # HTTP server + embedded WebUI
│   ├── power_manager.h/.c      # Deep sleep + battery + buzzer
│   ├── display_overlay.h/.c    # Date/time/battery overlays
│   ├── carousel.h/.c           # Image rotation logic
│   └── main.c                  # Application entry point
└── build/                      # Generated build files
```

## Component Descriptions

### Board Configuration (`board_config.h`)
Contains all GPIO pin mappings for the E1001:
- **Buttons**: K0 (GPIO3), K1 (GPIO4), K2 (GPIO5)
- **E-paper**: SPI pins (GPIO7-13), control pins
- **SD Card**: SPI pins (GPIO14-16), detect/enable
- **Power**: Battery ADC (GPIO1), buzzer (GPIO45)
- **I2C**: GPIO17-18 for sensors
- **LED**: GPIO6 status indicator

### WiFi Manager (`wifi_manager.h/.c`)
- **AP Mode**: Creates "E1001-Setup" network for provisioning
- **STA Mode**: Connects to configured WiFi network
- **Credentials**: Stored in NVS flash
- **Scan**: Lists available networks
- **Timeout**: K0 button toggles WiFi (auto-off after 1 minute)

### E-paper Driver (`epaper_driver.h/.c`)
- **Resolution**: 800x480 pixels
- **SPI Interface**: 10MHz clock, shared bus with SD card
- **Commands**: UC8179 command set implementation
- **Drawing**: Primitives for lines, rectangles, text, bitmaps
- **PSRAM**: Framebuffer stored in external PSRAM

### Storage Manager (`storage_manager.h/.c`)
- **SD Card**: FAT filesystem via SPI
- **NVS**: Settings storage in flash
- **Images**: CRUD operations for BMP/PNG/JPG files
- **Settings**: Carousel interval, WiFi timeout, deep sleep duration

### Image Processor (`image_processor.h/.c`)
- **Formats**: BMP, PNG, JPG decoding
- **Dithering**: Floyd-Steinberg and Atkinson algorithms
- **Output**: 1-bit per pixel for e-ink display
- **Optimization**: RGB to grayscale conversion

### Web Server (`web_server.h/.c`)
- **HTTP Server**: ESP-IDF httpd component
- **WebUI**: Embedded HTML/CSS/JS interface
- **Endpoints**:
  - `GET /`: Main interface
  - `GET /api/status`: System status (JSON)
  - `POST /api/upload`: Image upload
  - `DELETE /api/images/{filename}`: Delete image
  - `GET /api/settings`: Get settings
  - `POST /api/settings`: Update settings
  - `GET /api/wifi/scan`: WiFi scan results
  - `POST /api/wifi/connect`: Connect to WiFi
- **Embedded Assets**: Full WebUI in C strings

### Power Manager (`power_manager.h/.c`)
- **Deep Sleep**: EXT1 wake on buttons, timer wake for carousel
- **Battery**: ADC reading with voltage calculation
- **Buzzer**: PWM tones for feedback
- **Wake Reasons**: Button press, timer, or power-on

### Display Overlay (`display_overlay.h/.c`)
- **Date/Time**: SNTP synchronization, timezone support
- **Temperature**: I2C sensor reading (if available)
- **Battery**: Voltage percentage display
- **WiFi Status**: Connection indicator
- **Positioning**: Left corner (time), right corner (temp/battery)

### Carousel (`carousel.h/.c`)
- **Rotation**: Automatic image cycling
- **Interval**: Configurable (default 30 seconds)
- **Controls**: K1/K2 buttons for manual navigation
- **Deep Sleep**: Between image displays to save power

### Main Application (`main.c`)
- **Initialization**: All subsystems startup
- **Wake Handling**: Different behavior based on wake reason
- **Button Polling**: Dedicated task for button input
- **Main Loop**: Orchestrates carousel and sleep cycles

## Build Instructions

1. **Setup ESP-IDF**:
   ```bash
   # Clone ESP-IDF v5.5
   git clone -b v5.5 --recursive https://github.com/espressif/esp-idf.git
   cd esp-idf
   ./install.sh
   . ./export.sh
   ```

2. **Clone Project**:
   ```bash
   cd ~/projects
   git clone <this-repo>
   cd E1001
   ```

3. **Configure**:
   ```bash
   idf.py set-target esp32s3
   idf.py menuconfig  # Optional: customize settings
   ```

4. **Build**:
   ```bash
   idf.py build
   ```

## Flash Instructions

1. **Connect Device**: Ensure E1001 is connected via USB (COM14)

2. **Flash**:
   ```bash
   idf.py -p COM14 flash
   ```

3. **Monitor** (optional):
   ```bash
   idf.py -p COM14 monitor
   ```

## Usage Instructions

### Initial Setup
1. Power on device - enters AP mode ("E1001-Setup")
2. Connect phone/laptop to WiFi network
3. Open browser to `http://192.168.4.1`
4. Configure WiFi credentials
5. Upload images via web interface

### Normal Operation
- Device displays images in carousel mode
- Press K0 to toggle WiFi (1 minute timeout)
- Press K1/K2 to navigate images manually
- Device enters deep sleep between images

### Web Interface
- **Status Page**: System info, battery, WiFi status
- **Images Page**: Upload/delete images, view gallery
- **Settings Page**: Configure carousel interval, timeouts
- **WiFi Page**: Scan and connect to networks

## Configuration Options

### SDK Configuration (`sdkconfig.defaults`)
- ESP32-S3 target with PSRAM (octal 80MHz)
- 16MB flash, custom partitions
- WiFi, HTTP server, FAT filesystem enabled
- ADC, GPIO, SPI, I2C drivers enabled

### Partition Table (`partitions.csv`)
- **nvs**: 0x6000 (24KB) - settings storage
- **phy_init**: 0x1000 (4KB) - PHY calibration
- **factory**: 0x300000 (3MB) - application firmware
- **storage**: 0x100000 (1MB) - SPIFFS for web assets

### Default Settings
- **Carousel Interval**: 30 seconds
- **WiFi Timeout**: 60 seconds
- **Deep Sleep**: 30 seconds between images
- **AP SSID**: "E1001-Setup"
- **AP Password**: "12345678"

## API Reference

### WiFi Manager
```c
esp_err_t wifi_init(void);
esp_err_t wifi_start_ap(void);
esp_err_t wifi_connect_sta(const char *ssid, const char *password);
esp_err_t wifi_scan_networks(wifi_ap_record_t *ap_info, uint16_t *ap_count);
```

### Storage Manager
```c
esp_err_t storage_init(void);
int storage_get_images(image_info_t *images, int max_count);
esp_err_t storage_load_image(const char *filename, uint8_t **buffer, size_t *size);
esp_err_t storage_save_image(const char *filename, const uint8_t *data, size_t size);
esp_err_t storage_delete_image(const char *filename);
```

### Image Processor
```c
void img_rgb_to_1bpp(const uint8_t *rgb, uint16_t width, uint16_t height,
                     uint8_t *output, const img_process_opts_t *opts);
```

### Web Server
```c
esp_err_t webserver_init(void);
esp_err_t webserver_start(void);
void webserver_stop(void);
```

### Power Manager
```c
esp_err_t power_init(void);
void power_enter_deep_sleep(uint32_t seconds);
uint8_t power_get_battery_percentage(void);
void power_play_buzzer_tone(uint32_t frequency, uint32_t duration_ms);
```

### Display Overlay
```c
void overlay_draw_datetime(epaper_handle_t *epaper, time_t timestamp);
void overlay_draw_battery(epaper_handle_t *epaper, uint8_t percentage);
void overlay_draw_temperature(epaper_handle_t *epaper, float temperature);
```

### Carousel
```c
esp_err_t carousel_init(void);
void carousel_start(void);
void carousel_stop(void);
void carousel_next_image(void);
void carousel_prev_image(void);
```

## Troubleshooting

### Build Issues
- **ESP-IDF Version**: Ensure v5.5 is used
- **Python Version**: Use Python 3.8+
- **Dependencies**: Run `pip install -r requirements.txt`

### WiFi Issues
- **AP Not Visible**: Check device power, try different channel
- **STA Connection Fails**: Verify credentials, check signal strength
- **Timeout**: K0 button toggles WiFi mode

### Display Issues
- **No Display**: Check SPI connections, power supply
- **Corrupted Images**: Verify SD card format (FAT32)
- **Slow Refresh**: E-ink displays are inherently slow

### Power Issues
- **No Deep Sleep**: Check wake pin configuration
- **Battery Reading**: Calibrate ADC reference voltage
- **Buzzer Silent**: Check GPIO45 connection

### Storage Issues
- **SD Card Not Detected**: Check SD_EN pin, card format
- **NVS Corruption**: Erase flash with `idf.py erase-flash`
- **File Operations Fail**: Check SD card write protection

## Development Notes

- **PSRAM Usage**: Framebuffer and image buffers use external PSRAM
- **SPI Bus**: Shared between e-paper and SD card (mutex protected)
- **Task Priorities**: WiFi (high), carousel (medium), button polling (low)
- **Memory Management**: Use `heap_caps_malloc` with `MALLOC_CAP_SPIRAM`
- **Error Handling**: All functions return `esp_err_t` codes

## License

This project is provided as-is for educational and development purposes.