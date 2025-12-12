/*
 * Image Processor Implementation
 */

#include "image_processor.h"
#include "board_config.h"

#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <math.h>
#include "esp_log.h"
#include "esp_heap_caps.h"

#define STBI_NO_STDIO
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_MALLOC(sz) heap_caps_malloc((sz), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define STBI_REALLOC(p,sz) heap_caps_realloc((p), (sz), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define STBI_FREE(p) free(p)
#define STBI_ASSERT(x) assert(x)
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

static const char *TAG = "img_proc";

// BMP header structures
#pragma pack(push, 1)
typedef struct {
    uint16_t type;
    uint32_t size;
    uint16_t reserved1;
    uint16_t reserved2;
    uint32_t offset;
} bmp_file_header_t;

typedef struct {
    uint32_t size;
    int32_t width;
    int32_t height;
    uint16_t planes;
    uint16_t bit_count;
    uint32_t compression;
    uint32_t image_size;
    int32_t x_ppm;
    int32_t y_ppm;
    uint32_t colors_used;
    uint32_t colors_important;
} bmp_info_header_t;
#pragma pack(pop)

void img_get_default_opts(img_process_opts_t *opts) {
    opts->target_width = EPAPER_WIDTH;
    opts->target_height = EPAPER_HEIGHT;
    opts->format = IMG_FORMAT_1BPP;
    opts->dither = DITHER_ATKINSON;
    opts->threshold = 128;
    opts->invert = false;
    opts->fit_mode = true;
}

const char *img_detect_format(const uint8_t *data, size_t size) {
    if (size < 4) return "unknown";
    
    // BMP: "BM"
    if (data[0] == 'B' && data[1] == 'M') {
        return "bmp";
    }
    
    // JPEG: FF D8 FF
    if (data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) {
        return "jpg";
    }
    
    // PNG: 89 50 4E 47
    if (data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G') {
        return "png";
    }
    
    // Check if it's raw e-ink data
    if (size == EPAPER_BUFFER_SIZE) {
        return "raw";
    }
    
    return "unknown";
}

esp_err_t img_decode_bmp(const uint8_t *input, size_t input_size,
                         uint8_t **output, uint16_t *width, uint16_t *height) {
    if (!input || input_size < sizeof(bmp_file_header_t) + sizeof(bmp_info_header_t)) {
        return ESP_ERR_INVALID_ARG;
    }
    
    const bmp_file_header_t *file_hdr = (const bmp_file_header_t *)input;
    const bmp_info_header_t *info_hdr = (const bmp_info_header_t *)(input + sizeof(bmp_file_header_t));
    
    if (file_hdr->type != 0x4D42) {  // "BM"
        ESP_LOGE(TAG, "Not a BMP file");
        return ESP_ERR_INVALID_ARG;
    }
    
    int w = abs(info_hdr->width);
    int h = abs(info_hdr->height);
    bool bottom_up = info_hdr->height > 0;
    int bpp = info_hdr->bit_count;
    
    ESP_LOGI(TAG, "BMP: %dx%d, %d bpp", w, h, bpp);
    
    if (bpp != 24 && bpp != 32 && bpp != 8) {
        ESP_LOGE(TAG, "Unsupported BMP format: %d bpp", bpp);
        return ESP_ERR_NOT_SUPPORTED;
    }
    
    // Allocate output RGB buffer
    size_t out_size = w * h * 3;
    *output = heap_caps_malloc(out_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!*output) {
        *output = malloc(out_size);
    }
    if (!*output) {
        ESP_LOGE(TAG, "Cannot allocate %d bytes for decoded image", out_size);
        return ESP_ERR_NO_MEM;
    }
    
    const uint8_t *pixel_data = input + file_hdr->offset;
    int row_stride = ((w * bpp + 31) / 32) * 4;  // Rows are 4-byte aligned
    
    for (int y = 0; y < h; y++) {
        int src_y = bottom_up ? (h - 1 - y) : y;
        const uint8_t *row = pixel_data + src_y * row_stride;
        uint8_t *dst = *output + y * w * 3;
        
        for (int x = 0; x < w; x++) {
            if (bpp == 24) {
                dst[x * 3 + 0] = row[x * 3 + 2];  // R (BMP is BGR)
                dst[x * 3 + 1] = row[x * 3 + 1];  // G
                dst[x * 3 + 2] = row[x * 3 + 0];  // B
            } else if (bpp == 32) {
                dst[x * 3 + 0] = row[x * 4 + 2];
                dst[x * 3 + 1] = row[x * 4 + 1];
                dst[x * 3 + 2] = row[x * 4 + 0];
            } else if (bpp == 8) {
                // Grayscale or palette - treat as grayscale
                uint8_t gray = row[x];
                dst[x * 3 + 0] = gray;
                dst[x * 3 + 1] = gray;
                dst[x * 3 + 2] = gray;
            }
        }
    }
    
    *width = w;
    *height = h;
    return ESP_OK;
}

void img_scale(const uint8_t *input, uint16_t in_w, uint16_t in_h,
               uint8_t *output, uint16_t out_w, uint16_t out_h) {
    // Simple bilinear scaling
    float x_ratio = (float)in_w / out_w;
    float y_ratio = (float)in_h / out_h;
    
    for (int y = 0; y < out_h; y++) {
        for (int x = 0; x < out_w; x++) {
            int src_x = (int)(x * x_ratio);
            int src_y = (int)(y * y_ratio);
            
            if (src_x >= in_w) src_x = in_w - 1;
            if (src_y >= in_h) src_y = in_h - 1;
            
            int src_idx = (src_y * in_w + src_x) * 3;
            int dst_idx = (y * out_w + x) * 3;
            
            output[dst_idx + 0] = input[src_idx + 0];
            output[dst_idx + 1] = input[src_idx + 1];
            output[dst_idx + 2] = input[src_idx + 2];
        }
    }
}

// Floyd-Steinberg dithering
static void dither_floyd_steinberg(int16_t *gray, int w, int h) {
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int idx = y * w + x;
            int old_val = gray[idx];
            int new_val = (old_val < 128) ? 0 : 255;
            gray[idx] = new_val;
            int error = old_val - new_val;
            
            if (x + 1 < w)     gray[idx + 1]     += error * 7 / 16;
            if (y + 1 < h) {
                if (x > 0) {
                    gray[idx + w - 1] += error * 3 / 16;
                }
                gray[idx + w]     += error * 5 / 16;
                if (x + 1 < w) gray[idx + w + 1] += error * 1 / 16;
            }
        }
    }
}

// Atkinson dithering (better for e-ink)
static void dither_atkinson(int16_t *gray, int w, int h) {
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int idx = y * w + x;
            int old_val = gray[idx];
            int new_val = (old_val < 128) ? 0 : 255;
            gray[idx] = new_val;
            int error = (old_val - new_val) / 8;
            
            if (x + 1 < w)         gray[idx + 1]         += error;
            if (x + 2 < w)         gray[idx + 2]         += error;
            if (y + 1 < h) {
                if (x > 0) {
                    gray[idx + w - 1]     += error;
                }
                gray[idx + w]         += error;
                if (x + 1 < w)     gray[idx + w + 1]     += error;
            }
            if (y + 2 < h)         gray[idx + w * 2]     += error;
        }
    }
}

void img_rgb_to_1bpp(const uint8_t *rgb, uint16_t width, uint16_t height,
                     uint8_t *output, const img_process_opts_t *opts) {
    // Convert to grayscale with extended range for dithering
    size_t pixels = width * height;
    int16_t *gray = malloc(pixels * sizeof(int16_t));
    if (!gray) {
        ESP_LOGE(TAG, "Cannot allocate grayscale buffer");
        return;
    }
    
    for (size_t i = 0; i < pixels; i++) {
        // Luminance formula: 0.299*R + 0.587*G + 0.114*B
        int r = rgb[i * 3 + 0];
        int g = rgb[i * 3 + 1];
        int b = rgb[i * 3 + 2];
        gray[i] = (r * 77 + g * 150 + b * 29) >> 8;
    }
    
    // Apply dithering
    switch (opts->dither) {
        case DITHER_FLOYD:
            dither_floyd_steinberg(gray, width, height);
            break;
        case DITHER_ATKINSON:
            dither_atkinson(gray, width, height);
            break;
        case DITHER_NONE:
        default:
            // Simple threshold
            for (size_t i = 0; i < pixels; i++) {
                gray[i] = (gray[i] < opts->threshold) ? 0 : 255;
            }
            break;
    }
    
    // Convert to 1-bit packed format
    memset(output, 0, (width * height + 7) / 8);
    
    for (size_t i = 0; i < pixels; i++) {
        int val = (gray[i] > 127) ? 1 : 0;
        if (opts->invert) val = 1 - val;
        
        int byte_idx = i / 8;
        int bit_idx = 7 - (i % 8);
        
        if (val) {
            output[byte_idx] |= (1 << bit_idx);
        }
    }
    
    free(gray);
}

static void free_image_buffer(uint8_t *buf, bool from_stbi) {
    if (!buf) return;
    if (from_stbi) {
        stbi_image_free(buf);
    } else {
        free(buf);
    }
}

esp_err_t img_process(const uint8_t *input, size_t input_size,
                      uint8_t *output, size_t output_size,
                      const img_process_opts_t *opts) {
    if (!input || !output || !opts) {
        return ESP_ERR_INVALID_ARG;
    }
    
    const char *format = img_detect_format(input, input_size);
    ESP_LOGI(TAG, "Processing %s image (%d bytes)", format, input_size);
    
    // If already raw e-ink format, just copy
    if (strcmp(format, "raw") == 0 && input_size == output_size) {
        memcpy(output, input, input_size);
        return ESP_OK;
    }
    
    uint8_t *rgb = NULL;
    uint16_t width = 0;
    uint16_t height = 0;
    bool rgb_from_stbi = false;
    esp_err_t ret = ESP_OK;
    
    if (strcmp(format, "bmp") == 0) {
        ret = img_decode_bmp(input, input_size, &rgb, &width, &height);
        if (ret != ESP_OK) {
            return ret;
        }
    } else if (strcmp(format, "jpg") == 0 || strcmp(format, "png") == 0) {
        int w = 0, h = 0, comp = 0;
        stbi_uc *decoded = stbi_load_from_memory(input, (int)input_size,
                                                 &w, &h, &comp, 3);
        if (!decoded) {
            ESP_LOGE(TAG, "Decode failed for %s: %s", format, stbi_failure_reason());
            return ESP_ERR_NOT_SUPPORTED;
        }
        if (w <= 0 || h <= 0 || w > UINT16_MAX || h > UINT16_MAX) {
            ESP_LOGE(TAG, "Image dimensions unsupported: %dx%d", w, h);
            stbi_image_free(decoded);
            return ESP_ERR_INVALID_SIZE;
        }
        
        rgb = decoded;
        width = (uint16_t)w;
        height = (uint16_t)h;
        rgb_from_stbi = true;
    } else {
        ESP_LOGE(TAG, "Unsupported format: %s (BMP, JPG, PNG, raw supported)", format);
        return ESP_ERR_NOT_SUPPORTED;
    }
    
    // Scale if needed
    uint8_t *scaled = NULL;
    if (width != opts->target_width || height != opts->target_height) {
        size_t scaled_size = opts->target_width * opts->target_height * 3;
        scaled = heap_caps_malloc(scaled_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!scaled) {
            scaled = malloc(scaled_size);
        }
        if (!scaled) {
            free_image_buffer(rgb, rgb_from_stbi);
            return ESP_ERR_NO_MEM;
        }
        
        img_scale(rgb, width, height, scaled, opts->target_width, opts->target_height);
        free_image_buffer(rgb, rgb_from_stbi);
        rgb = scaled;
        rgb_from_stbi = false;  // scaled buffer uses malloc path in this file
        width = opts->target_width;
        height = opts->target_height;
    }
    
    // Convert to 1-bit
    img_rgb_to_1bpp(rgb, width, height, output, opts);
    
    free_image_buffer(rgb, rgb_from_stbi);
    
    ESP_LOGI(TAG, "Image processed successfully");
    return ESP_OK;
}

bool img_is_valid_epd_buffer(const uint8_t *data, size_t size) {
    return data != NULL && size == EPAPER_BUFFER_SIZE;
}
