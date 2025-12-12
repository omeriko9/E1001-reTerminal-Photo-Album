# E1001 ESP32-S3 E-Paper Photo Frame

<img width="806" height="895" alt="image" src="https://github.com/user-attachments/assets/b2844cf3-1e10-4560-afbc-d59a9087e090" />


A complete firmware project for the reTerminal E1001 (ESP32-S3 based device) featuring WiFi provisioning, web-based image management, and automatic carousel display on an 800x480 e-ink display.

## Features

- **WiFi Provisioning**: Easy setup via access point mode with web interface
- **E-ink Display**: High-quality 800x480 UC8179-compatible display with carousel functionality
- **Web Interface**: Modern responsive UI for uploading, managing, and configuring images
- **Image Processing**: Support for BMP, PNG, JPG formats with automatic dithering for e-ink
- **Power Management**: Deep sleep between images, battery monitoring, and buzzer feedback
- **Display Overlays**: Optional date/time, temperature, and battery status overlays
- **Button Controls**: Physical buttons for WiFi toggle, next/previous image navigation
- **Storage**: SD card support for image storage, NVS for settings
- **DNS Server**: Captive portal for seamless provisioning

## Hardware Requirements

- **reTerminal E1001** with ESP32-S3, 16MB flash, 8MB PSRAM
- **800x480 E-ink Display** (UC8179 compatible)
- **SD Card** (FAT32 formatted, for storing images)
- **Battery** (optional, ADC monitoring on GPIO1)
- **Buttons**: K0 (GPIO3), K1 (GPIO4), K2 (GPIO5) for controls
- **Buzzer**: GPIO45 (optional, for audio feedback)
- **USB Cable**: For programming and power

## Software Requirements

- **ESP-IDF v5.5**: Install from [official documentation](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/get-started/index.html)
- **Python 3.8+**: Required for build tools
- **Git**: For cloning the repository

## Installation

### 1. Setup ESP-IDF

```bash
# Clone ESP-IDF v5.5
git clone -b v5.5 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh
. ./export.sh
```

### 2. Clone This Project

```bash
cd ~/projects
git clone https://github.com/omeriko9/E1001.git
cd E1001
```

### 3. Configure the Project

```bash
idf.py set-target esp32s3
idf.py menuconfig  # Optional: customize settings
```

### 4. Build the Project

```bash
idf.py build
```

### 5. Flash to Device

Connect your reTerminal E1001 via USB, then:

```bash
idf.py -p <PORT> flash
```

Replace `<PORT>` with your device's serial port (e.g., `COM14` on Windows).

### 6. Monitor (Optional)

```bash
idf.py -p <PORT> monitor
```

## Usage

### Initial Setup

1. Power on the device - it will enter AP mode and create a WiFi network named "E1001-Setup"
2. Connect your phone or computer to this network (password: 12345678)
3. A captive portal will automatically open, or navigate to `http://192.168.4.1`
4. Configure your WiFi credentials through the web interface
5. Upload images via the web UI

### Normal Operation

- The device displays images in a rotating carousel
- Press **K0** to toggle WiFi on/off (times out after 1 minute)
- Press **K1** (Next) or **K2** (Previous) to manually navigate images
- The device enters deep sleep between images to conserve battery

### Web Interface

Access the web interface at the device's IP address when connected to your WiFi:

- **Dashboard**: System status, battery level, storage info
- **Images**: Upload new images, view gallery, delete images
- **Settings**: Configure carousel interval, WiFi timeout, deep sleep duration
- **WiFi**: Scan and connect to different networks

Supported image formats: BMP, PNG, JPG. Images are automatically processed and dithered for optimal e-ink display quality.

## Configuration

Default settings can be modified via the web interface or by editing `sdkconfig.defaults`:

- **Carousel Interval**: 30 seconds between images
- **WiFi Timeout**: 60 seconds
- **Deep Sleep Duration**: 30 seconds
- **AP Network**: "E1001-Setup" / "12345678"

## Troubleshooting

### Build Issues
- Ensure ESP-IDF v5.5 is correctly installed and exported
- Check Python version (3.8+ required)
- Run `idf.py fullclean` and rebuild if issues persist

### WiFi Issues
- If AP mode doesn't appear, check device power and try a different channel
- For STA connection problems, verify credentials and signal strength
- Use the K0 button to toggle WiFi modes

### Display Issues
- Verify SPI connections and power supply to the e-ink display
- Ensure SD card is properly formatted (FAT32)
- E-ink displays are slow by nature - refresh times are normal

### Storage Issues
- Check SD card detection (SD_EN pin) and format
- If NVS is corrupted, erase flash: `idf.py erase-flash`
- Remove write protection from SD card

### Power Issues
- Verify wake pin configuration for deep sleep
- Calibrate ADC for accurate battery readings
- Check buzzer connections if no audio feedback

## Project Structure

```
E1001/
├── main/
│   ├── board_config.h          # Hardware pin definitions
│   ├── wifi_manager.c/.h       # WiFi management
│   ├── epaper_driver.c/.h      # E-ink display driver
│   ├── storage_manager.c/.h    # SD card and NVS storage
│   ├── image_processor.c/.h    # Image decoding and dithering
│   ├── web_server.c/.h        # HTTP server and web UI
│   ├── power_manager.c/.h      # Deep sleep and battery
│   ├── display_overlay.c/.h    # Status overlays
│   ├── carousel.c/.h           # Image rotation logic
│   ├── dns_server.c/.h        # Captive portal DNS
│   └── main.c                  # Application entry point
├── spiffs/                     # Web assets (HTML, CSS, JS)
├── CMakeLists.txt              # Build configuration
├── sdkconfig.defaults          # Default SDK settings
└── partitions.csv              # Flash partition table
```

## License

This project is provided as-is for educational and development purposes.
