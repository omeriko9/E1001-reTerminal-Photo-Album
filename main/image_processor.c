/*
 * Image Processor Implementation
 */

#include "image_processor.h"
#include "board_config.h"
#include "storage_manager.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <math.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "tjpgd.h"

// #define STBI_NO_STDIO  <-- REMOVED THIS LINE
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_MALLOC(sz) heap_caps_malloc((sz), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define STBI_REALLOC(p, sz) heap_caps_realloc((p), (sz), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define STBI_FREE(p) free(p)
#define STBI_ASSERT(x) assert(x)
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

static const char *TAG = "img_proc";

// BMP header structures
#pragma pack(push, 1)
typedef struct
{
    uint16_t type;
    uint32_t size;
    uint16_t reserved1;
    uint16_t reserved2;
    uint32_t offset;
} bmp_file_header_t;

typedef struct
{
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

void img_get_default_opts(img_process_opts_t *opts)
{
    opts->target_width = EPAPER_WIDTH;
    opts->target_height = EPAPER_HEIGHT;
    opts->format = IMG_FORMAT_1BPP;
    opts->dither = DITHER_ATKINSON;
    opts->threshold = 128;
    opts->invert = false;
    opts->fit_mode = true;
}

const char *img_detect_format(const uint8_t *data, size_t size)
{
    if (size < 4)
        return "unknown";

    // BMP: "BM"
    if (data[0] == 'B' && data[1] == 'M')
    {
        return "bmp";
    }

    // JPEG: FF D8 FF
    if (data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF)
    {
        return "jpg";
    }

    // PNG: 89 50 4E 47
    if (data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G')
    {
        return "png";
    }

    // Check if it's raw e-ink data
    if (size == EPAPER_BUFFER_SIZE)
    {
        return "raw";
    }

    return "unknown";
}

esp_err_t img_decode_bmp(const uint8_t *input, size_t input_size,
                         uint8_t **output, uint16_t *width, uint16_t *height)
{
    if (!input || input_size < sizeof(bmp_file_header_t) + sizeof(bmp_info_header_t))
    {
        return ESP_ERR_INVALID_ARG;
    }

    const bmp_file_header_t *file_hdr = (const bmp_file_header_t *)input;
    const bmp_info_header_t *info_hdr = (const bmp_info_header_t *)(input + sizeof(bmp_file_header_t));

    if (file_hdr->type != 0x4D42)
    { // "BM"
        ESP_LOGE(TAG, "Not a BMP file");
        return ESP_ERR_INVALID_ARG;
    }

    int w = abs(info_hdr->width);
    int h = abs(info_hdr->height);
    bool bottom_up = info_hdr->height > 0;
    int bpp = info_hdr->bit_count;

    ESP_LOGI(TAG, "BMP: %dx%d, %d bpp", w, h, bpp);

    if (bpp != 24 && bpp != 32 && bpp != 8)
    {
        ESP_LOGE(TAG, "Unsupported BMP format: %d bpp", bpp);
        return ESP_ERR_NOT_SUPPORTED;
    }

    // Allocate output RGB buffer
    size_t out_size = w * h * 3;
    *output = heap_caps_malloc(out_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!*output)
    {
        *output = malloc(out_size);
    }
    if (!*output)
    {
        ESP_LOGE(TAG, "Cannot allocate %d bytes for decoded image", out_size);
        return ESP_ERR_NO_MEM;
    }

    const uint8_t *pixel_data = input + file_hdr->offset;
    int row_stride = ((w * bpp + 31) / 32) * 4; // Rows are 4-byte aligned

    for (int y = 0; y < h; y++)
    {
        int src_y = bottom_up ? (h - 1 - y) : y;
        const uint8_t *row = pixel_data + src_y * row_stride;
        uint8_t *dst = *output + y * w * 3;

        for (int x = 0; x < w; x++)
        {
            if (bpp == 24)
            {
                dst[x * 3 + 0] = row[x * 3 + 2]; // R (BMP is BGR)
                dst[x * 3 + 1] = row[x * 3 + 1]; // G
                dst[x * 3 + 2] = row[x * 3 + 0]; // B
            }
            else if (bpp == 32)
            {
                dst[x * 3 + 0] = row[x * 4 + 2];
                dst[x * 3 + 1] = row[x * 4 + 1];
                dst[x * 3 + 2] = row[x * 4 + 0];
            }
            else if (bpp == 8)
            {
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
               uint8_t *output, uint16_t out_w, uint16_t out_h, bool fit)
{
    // Calculate scale
    float scale_x = (float)out_w / in_w;
    float scale_y = (float)out_h / in_h;
    float scale;
    
    if (fit) {
        scale = (scale_x < scale_y) ? scale_x : scale_y; // Use smaller scale to fit
    } else {
        scale = (scale_x > scale_y) ? scale_x : scale_y; // Use larger scale to cover
    }

    // Calculate source window size in input coordinates
    float src_w = out_w / scale;
    float src_h = out_h / scale;

    // Center the source window
    float src_x_offset = (in_w - src_w) / 2.0f;
    float src_y_offset = (in_h - src_h) / 2.0f;

    for (int y = 0; y < out_h; y++)
    {
        for (int x = 0; x < out_w; x++)
        {
            // Map output coordinate to source coordinate
            float src_x = src_x_offset + (x / scale);
            float src_y = src_y_offset + (y / scale);

            int sx = (int)src_x;
            int sy = (int)src_y;

            // Check bounds
            if (sx < 0 || sx >= in_w || sy < 0 || sy >= in_h) {
                // Fill with white (background)
                int dst_idx = (y * out_w + x) * 3;
                output[dst_idx + 0] = 255;
                output[dst_idx + 1] = 255;
                output[dst_idx + 2] = 255;
            } else {
                int src_idx = (sy * in_w + sx) * 3;
                int dst_idx = (y * out_w + x) * 3;

                output[dst_idx + 0] = input[src_idx + 0];
                output[dst_idx + 1] = input[src_idx + 1];
                output[dst_idx + 2] = input[src_idx + 2];
            }
        }
    }
}

// Floyd-Steinberg dithering
static void dither_floyd_steinberg(int16_t *gray, int w, int h)
{
    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            int idx = y * w + x;
            int old_val = gray[idx];
            int new_val = (old_val < 128) ? 0 : 255;
            gray[idx] = new_val;
            int error = old_val - new_val;

            if (x + 1 < w)
                gray[idx + 1] += error * 7 / 16;
            if (y + 1 < h)
            {
                if (x > 0)
                {
                    gray[idx + w - 1] += error * 3 / 16;
                }
                gray[idx + w] += error * 5 / 16;
                if (x + 1 < w)
                    gray[idx + w + 1] += error * 1 / 16;
            }
        }
    }
}

// Atkinson dithering (better for e-ink)
static void dither_atkinson(int16_t *gray, int w, int h)
{
    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            int idx = y * w + x;
            int old_val = gray[idx];
            int new_val = (old_val < 128) ? 0 : 255;
            gray[idx] = new_val;
            int error = (old_val - new_val) / 8;

            if (x + 1 < w)
                gray[idx + 1] += error;
            if (x + 2 < w)
                gray[idx + 2] += error;
            if (y + 1 < h)
            {
                if (x > 0)
                {
                    gray[idx + w - 1] += error;
                }
                gray[idx + w] += error;
                if (x + 1 < w)
                    gray[idx + w + 1] += error;
            }
            if (y + 2 < h)
                gray[idx + w * 2] += error;
        }
    }
}

void img_scale_gray(const uint8_t *input, uint16_t in_w, uint16_t in_h,
                    uint8_t *output, uint16_t out_w, uint16_t out_h, bool fit)
{
    // Calculate scale
    float scale_x = (float)out_w / in_w;
    float scale_y = (float)out_h / in_h;
    float scale;
    
    if (fit) {
        scale = (scale_x < scale_y) ? scale_x : scale_y;
    } else {
        scale = (scale_x > scale_y) ? scale_x : scale_y;
    }

    float src_w = out_w / scale;
    float src_h = out_h / scale;

    float src_x_offset = (in_w - src_w) / 2.0f;
    float src_y_offset = (in_h - src_h) / 2.0f;

    for (int y = 0; y < out_h; y++)
    {
        for (int x = 0; x < out_w; x++)
        {
            float src_x = src_x_offset + (x / scale);
            float src_y = src_y_offset + (y / scale);

            int sx = (int)src_x;
            int sy = (int)src_y;

            int dst_idx = y * out_w + x;

            if (sx < 0 || sx >= in_w || sy < 0 || sy >= in_h) {
                output[dst_idx] = 255; // White background
            } else {
                int src_idx = sy * in_w + sx;
                output[dst_idx] = input[src_idx];
            }
        }
    }
}

void img_gray_to_1bpp(const uint8_t *gray_in, uint16_t width, uint16_t height,
                      uint8_t *output, const img_process_opts_t *opts)
{
    size_t pixels = width * height;
    int16_t *gray = malloc(pixels * sizeof(int16_t));
    if (!gray)
    {
        ESP_LOGE(TAG, "Cannot allocate grayscale buffer");
        return;
    }

    // Copy to int16 buffer for dithering
    for (size_t i = 0; i < pixels; i++)
    {
        gray[i] = gray_in[i];
    }

    // Apply dithering
    switch (opts->dither)
    {
    case DITHER_FLOYD:
        dither_floyd_steinberg(gray, width, height);
        break;
    case DITHER_ATKINSON:
        dither_atkinson(gray, width, height);
        break;
    case DITHER_NONE:
    default:
        for (size_t i = 0; i < pixels; i++)
        {
            gray[i] = (gray[i] < opts->threshold) ? 0 : 255;
        }
        break;
    }

    // Convert to 1-bit packed format
    memset(output, 0, (width * height + 7) / 8);

    for (size_t i = 0; i < pixels; i++)
    {
        int val = (gray[i] > 127) ? 1 : 0;
        if (opts->invert)
            val = 1 - val;

        int byte_idx = i / 8;
        int bit_idx = 7 - (i % 8);

        if (val)
        {
            output[byte_idx] |= (1 << bit_idx);
        }
    }

    free(gray);
}

void img_rgb_to_1bpp(const uint8_t *rgb, uint16_t width, uint16_t height,
                     uint8_t *output, const img_process_opts_t *opts)
{
    // Convert to grayscale with extended range for dithering
    size_t pixels = width * height;
    int16_t *gray = malloc(pixels * sizeof(int16_t));
    if (!gray)
    {
        ESP_LOGE(TAG, "Cannot allocate grayscale buffer");
        return;
    }

    for (size_t i = 0; i < pixels; i++)
    {
        // Luminance formula: 0.299*R + 0.587*G + 0.114*B
        int r = rgb[i * 3 + 0];
        int g = rgb[i * 3 + 1];
        int b = rgb[i * 3 + 2];
        gray[i] = (r * 77 + g * 150 + b * 29) >> 8;
    }

    // Apply dithering
    switch (opts->dither)
    {
    case DITHER_FLOYD:
        dither_floyd_steinberg(gray, width, height);
        break;
    case DITHER_ATKINSON:
        dither_atkinson(gray, width, height);
        break;
    case DITHER_NONE:
    default:
        // Simple threshold
        for (size_t i = 0; i < pixels; i++)
        {
            gray[i] = (gray[i] < opts->threshold) ? 0 : 255;
        }
        break;
    }

    // Convert to 1-bit packed format
    memset(output, 0, (width * height + 7) / 8);

    for (size_t i = 0; i < pixels; i++)
    {
        int val = (gray[i] > 127) ? 1 : 0;
        if (opts->invert)
            val = 1 - val;

        int byte_idx = i / 8;
        int bit_idx = 7 - (i % 8);

        if (val)
        {
            output[byte_idx] |= (1 << bit_idx);
        }
    }

    free(gray);
}

static void free_image_buffer(uint8_t *buf, bool from_stbi)
{
    if (!buf)
        return;
    if (from_stbi)
    {
        stbi_image_free(buf);
    }
    else
    {
        free(buf);
    }
}

esp_err_t img_process(const uint8_t *input, size_t input_size,
                      uint8_t *output, size_t output_size,
                      const img_process_opts_t *opts)
{
    if (!input || !output || !opts)
    {
        return ESP_ERR_INVALID_ARG;
    }

    const char *format = img_detect_format(input, input_size);
    ESP_LOGI(TAG, "Processing %s image (%d bytes)", format, input_size);

    // If already raw e-ink format, just copy
    if (strcmp(format, "raw") == 0 && input_size == output_size)
    {
        memcpy(output, input, input_size);
        return ESP_OK;
    }

    uint8_t *rgb = NULL;
    uint16_t width = 0;
    uint16_t height = 0;
    bool rgb_from_stbi = false;
    esp_err_t ret = ESP_OK;

    if (strcmp(format, "bmp") == 0)
    {
        ret = img_decode_bmp(input, input_size, &rgb, &width, &height);
        if (ret != ESP_OK)
        {
            return ret;
        }
    }
    else if (strcmp(format, "jpg") == 0 || strcmp(format, "png") == 0)
    {
        int w = 0, h = 0, comp = 0;

        // Check dimensions first
        if (!stbi_info_from_memory(input, (int)input_size, &w, &h, &comp))
        {
            ESP_LOGE(TAG, "Failed to parse image info: %s", stbi_failure_reason());
            return ESP_ERR_INVALID_ARG;
        }

        ESP_LOGI(TAG, "Image info: %dx%d, %d comp", w, h, comp);

        if (w > 3200 || h > 3200 || (w * h) > 5000000)
        {
            ESP_LOGE(TAG, "Image too large to process: %dx%d", w, h);
            return ESP_ERR_INVALID_SIZE;
        }

        stbi_uc *decoded = stbi_load_from_memory(input, (int)input_size,
                                                 &w, &h, &comp, 3);
        if (!decoded)
        {
            ESP_LOGE(TAG, "Decode failed for %s: %s", format, stbi_failure_reason());
            return ESP_ERR_NOT_SUPPORTED;
        }

        rgb = decoded;
        width = (uint16_t)w;
        height = (uint16_t)h;
        rgb_from_stbi = true;
    }
    else
    {
        ESP_LOGE(TAG, "Unsupported format: %s", format);
        return ESP_ERR_NOT_SUPPORTED;
    }

    // Scale if needed
    uint8_t *scaled = NULL;
    if (width != opts->target_width || height != opts->target_height)
    {
        size_t scaled_size = opts->target_width * opts->target_height * 3;
        scaled = heap_caps_malloc(scaled_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!scaled)
        {
            scaled = malloc(scaled_size);
        }
        if (!scaled)
        {
            free_image_buffer(rgb, rgb_from_stbi);
            return ESP_ERR_NO_MEM;
        }

        img_scale(rgb, width, height, scaled, opts->target_width, opts->target_height, opts->fit_mode);
        free_image_buffer(rgb, rgb_from_stbi);
        rgb = scaled;
        rgb_from_stbi = false;
        width = opts->target_width;
        height = opts->target_height;
    }

    // Convert to 1-bit
    img_rgb_to_1bpp(rgb, width, height, output, opts);

    free_image_buffer(rgb, rgb_from_stbi);

    ESP_LOGI(TAG, "Image processed successfully");
    return ESP_OK;
}

// Combined context for tjpgd input and output
typedef struct {
    // Input fields
    FILE *fp;
    const uint8_t *data;
    size_t size;
    size_t index;
    
    // Output fields
    uint8_t *output;
    int width;
    int height;
} tjpgd_ctx_t;

// Input callback
static size_t tjpgd_input_func(JDEC *jd, uint8_t *buff, size_t ndata) {
    tjpgd_ctx_t *ctx = (tjpgd_ctx_t *)jd->device;
    if (buff) {
        // Read data
        if (ctx->fp) {
            return fread(buff, 1, ndata, ctx->fp);
        } else {
            size_t to_read = ndata;
            if (ctx->index + to_read > ctx->size) {
                to_read = ctx->size - ctx->index;
            }
            memcpy(buff, ctx->data + ctx->index, to_read);
            ctx->index += to_read;
            return to_read;
        }
    } else {
        // Skip data
        if (ctx->fp) {
            return fseek(ctx->fp, ndata, SEEK_CUR) == 0 ? ndata : 0;
        } else {
            size_t to_skip = ndata;
            if (ctx->index + to_skip > ctx->size) {
                to_skip = ctx->size - ctx->index;
            }
            ctx->index += to_skip;
            return to_skip;
        }
    }
}

// Output callback
static int tjpgd_output_func(JDEC *jd, void *bitmap, JRECT *rect) {
    tjpgd_ctx_t *ctx = (tjpgd_ctx_t *)jd->device;
    uint8_t *src = (uint8_t *)bitmap;
    
    // Copy block to output buffer
    // JD_FORMAT=2 means 1 byte per pixel (grayscale)
    // TJpgDec outputs blocks (usually 8x8 or 16x16)
    // We need to copy row by row
    
    int w = rect->right - rect->left + 1;
    int h = rect->bottom - rect->top + 1;
    
    for (int y = 0; y < h; y++) {
        // Calculate destination index in the full image buffer
        // rect->top + y is the current row in the full image
        // rect->left is the starting column
        int dst_row = rect->top + y;
        
        // Safety check
        if (dst_row >= ctx->height) continue;
        
        int dst_idx = dst_row * ctx->width + rect->left;
        
        // Copy one row of the block
        // Ensure we don't write past the width
        int copy_w = w;
        if (rect->left + copy_w > ctx->width) {
            copy_w = ctx->width - rect->left;
        }
        
        if (copy_w > 0) {
            // src is the block buffer, it has width 'w'
            // We copy 'copy_w' bytes from src to ctx->output
            memcpy(ctx->output + dst_idx, src + y * w, copy_w);
        }
    }
    
    return 1; // Continue
}

esp_err_t img_process_file(const char *filename,
                           uint8_t *output, size_t output_size,
                           const img_process_opts_t *opts)
{
    if (!filename || !output || !opts)
    {
        return ESP_ERR_INVALID_ARG;
    }

    // Check extension
    const char *ext = strrchr(filename, '.');
    if (!ext)
        return ESP_ERR_NOT_SUPPORTED;

    // If raw/bin, we can just read directly
    if (strcasecmp(ext, ".raw") == 0 || strcasecmp(ext, ".bin") == 0)
    {
        FILE *f = fopen(filename, "rb");
        if (!f)
            return ESP_ERR_NOT_FOUND;
        size_t read = fread(output, 1, output_size, f);
        fclose(f);
        return (read == output_size) ? ESP_OK : ESP_ERR_INVALID_SIZE;
    }

    // For images, use STBI to load from file directly (saves memory)
    int w = 0, h = 0, comp = 0;

    // Check dimensions first
    if (!stbi_info(filename, &w, &h, &comp))
    {
        ESP_LOGE(TAG, "Failed to parse image info: %s", stbi_failure_reason());
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "File Image info: %dx%d, %d comp", w, h, comp);

    // Check if it's a JPEG and large
    bool use_tjpgd = false;
    if (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0) {
        size_t required_mem = w * h * 1;
        size_t free_mem = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        if (required_mem + 512000 > free_mem) {
            use_tjpgd = true;
        }
    }

    if (use_tjpgd) {
        ESP_LOGI(TAG, "Using TJpgDec for large JPEG");
        
        // Determine scale factor
        uint8_t scale = 0;
        int sw = w, sh = h;
        while (scale < 3) {
            if ((sw >> 1) < opts->target_width && (sh >> 1) < opts->target_height) {
                break;
            }
            sw >>= 1;
            sh >>= 1;
            scale++;
        }
        
        ESP_LOGI(TAG, "Scaling by 1/%d -> %dx%d", 1 << scale, sw, sh);
        
        // Allocate buffer for scaled image
        size_t buf_size = sw * sh;
        uint8_t *gray = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!gray) {
            ESP_LOGE(TAG, "Failed to allocate %d bytes for scaled image", buf_size);
            return ESP_ERR_NO_MEM;
        }
        
        // Prepare tjpgd
        JDEC jd;
        char *work = malloc(TJPGD_WORKSPACE_SIZE);
        if (!work) {
            free(gray);
            return ESP_ERR_NO_MEM;
        }
        
        tjpgd_ctx_t ctx = {0};
        ctx.fp = fopen(filename, "rb");
        if (!ctx.fp) {
            free(gray);
            free(work);
            return ESP_ERR_NOT_FOUND;
        }
        ctx.output = gray;
        ctx.width = sw;
        ctx.height = sh;
        
        JRESULT res = jd_prepare(&jd, tjpgd_input_func, work, TJPGD_WORKSPACE_SIZE, &ctx);
        if (res == JDR_OK) {
            res = jd_decomp(&jd, tjpgd_output_func, scale);
        }
        
        fclose(ctx.fp);
        free(work);
        
        if (res != JDR_OK) {
            ESP_LOGE(TAG, "TJpgDec failed: %d", res);
            free(gray);
            return ESP_FAIL;
        }
        
        // Now scale to final size
        uint8_t *final_gray = NULL;
        if (sw != opts->target_width || sh != opts->target_height) {
            final_gray = heap_caps_malloc(opts->target_width * opts->target_height, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!final_gray) {
                free(gray);
                return ESP_ERR_NO_MEM;
            }
            img_scale_gray(gray, sw, sh, final_gray, opts->target_width, opts->target_height, opts->fit_mode);
            free(gray);
            gray = final_gray;
        }
        
        // Convert to 1bpp
        img_gray_to_1bpp(gray, opts->target_width, opts->target_height, output, opts);
        free(gray);
        
        ESP_LOGI(TAG, "Large JPEG processed successfully");
        return ESP_OK;
    }

    // Calculate required memory for grayscale (1 byte per pixel)
    size_t required_mem = w * h * 1;
    size_t free_mem = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

    // Leave some headroom (e.g. 500KB)
    if (required_mem + 512000 > free_mem)
    {
        ESP_LOGE(TAG, "Image too large for memory: %dx%d (req: %d KB, free: %d KB)",
                 w, h, required_mem / 1024, free_mem / 1024);
        return ESP_ERR_NO_MEM;
    }

    // Load as GRAYSCALE (req_comp=1) to save 3x memory!
    stbi_uc *decoded = stbi_load(filename, &w, &h, &comp, 1);
    if (!decoded)
    {
        ESP_LOGE(TAG, "Decode failed for %s: %s", filename, stbi_failure_reason());
        return ESP_ERR_NOT_SUPPORTED;
    }

    uint8_t *gray = decoded;
    uint16_t width = (uint16_t)w;
    uint16_t height = (uint16_t)h;
    bool gray_from_stbi = true;

    // Scale if needed
    uint8_t *scaled = NULL;
    if (width != opts->target_width || height != opts->target_height)
    {
        size_t scaled_size = opts->target_width * opts->target_height * 1; // 1 byte per pixel
        scaled = heap_caps_malloc(scaled_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!scaled)
        {
            scaled = malloc(scaled_size);
        }
        if (!scaled)
        {
            free_image_buffer(gray, gray_from_stbi);
            return ESP_ERR_NO_MEM;
        }

        img_scale_gray(gray, width, height, scaled, opts->target_width, opts->target_height, opts->fit_mode);
        free_image_buffer(gray, gray_from_stbi);
        gray = scaled;
        gray_from_stbi = false;
        width = opts->target_width;
        height = opts->target_height;
    }

    // Convert to 1-bit
    img_gray_to_1bpp(gray, width, height, output, opts);

    free_image_buffer(gray, gray_from_stbi);

    ESP_LOGI(TAG, "Image processed successfully from file");
    return ESP_OK;
}

bool img_is_valid_epd_buffer(const uint8_t *data, size_t size)
{
    return data != NULL && size == EPAPER_BUFFER_SIZE;
}

// Helper to save BMP
static esp_err_t img_save_bmp(const char *filename, const uint8_t *data, int w, int h, int comp) {
    FILE *f = fopen(filename, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open %s for writing", filename);
        return ESP_FAIL;
    }

    int row_padded = (w * comp + 3) & (~3);
    int size = 54 + row_padded * h;

    uint8_t header[54] = {
        'B','M',
        size & 0xFF, (size >> 8) & 0xFF, (size >> 16) & 0xFF, (size >> 24) & 0xFF,
        0,0, 0,0,
        54,0,0,0,
        40,0,0,0,
        w & 0xFF, (w >> 8) & 0xFF, (w >> 16) & 0xFF, (w >> 24) & 0xFF,
        h & 0xFF, (h >> 8) & 0xFF, (h >> 16) & 0xFF, (h >> 24) & 0xFF,
        1,0,
        comp * 8, 0,
        0,0,0,0,
        0,0,0,0,
        0,0,0,0,
        0,0,0,0,
        0,0,0,0,
        0,0,0,0
    };

    fwrite(header, 1, 54, f);

    uint8_t *pad = calloc(1, 4);
    for (int y = h - 1; y >= 0; y--) {
        fwrite(data + y * w * comp, 1, w * comp, f);
        fwrite(pad, 1, row_padded - w * comp, f);
    }
    free(pad);
    fclose(f);
    return ESP_OK;
}

esp_err_t img_process_upload(const char *filename) {
    ESP_LOGI(TAG, "Processing upload: %s", filename);
    
    // 1. Generate Thumbnail
    int w, h, comp;
    // Force 3 components (RGB) for thumbnail
    stbi_uc *img = stbi_load(filename, &w, &h, &comp, 3); 
    if (img) {
        int thumb_w = 160;
        int thumb_h = 120;
        uint8_t *thumb = malloc(thumb_w * thumb_h * 3);
        if (thumb) {
            // Simple nearest neighbor resize
            for (int y = 0; y < thumb_h; y++) {
                for (int x = 0; x < thumb_w; x++) {
                    int sx = x * w / thumb_w;
                    int sy = y * h / thumb_h;
                    int src_idx = (sy * w + sx) * 3;
                    int dst_idx = (y * thumb_w + x) * 3;
                    thumb[dst_idx] = img[src_idx];
                    thumb[dst_idx+1] = img[src_idx+1];
                    thumb[dst_idx+2] = img[src_idx+2];
                }
            }
            
            char thumb_path[256];
            snprintf(thumb_path, sizeof(thumb_path), "%s.thumb", filename);
            img_save_bmp(thumb_path, thumb, thumb_w, thumb_h, 3);
            free(thumb);
            ESP_LOGI(TAG, "Thumbnail generated: %s", thumb_path);
        }
        stbi_image_free(img);
    } else {
        ESP_LOGW(TAG, "Failed to load image for thumbnail: %s", filename);
        // If stbi fails (e.g. large JPEG), try tjpgd for thumbnail too
        // But for now, just skip
    }

    // 2. Generate Optimized Image (BIN)
    // Check if .bin already exists (maybe uploaded as .bin)
    char bin_path[256];
    // Replace extension with .bin
    strncpy(bin_path, filename, sizeof(bin_path));
    char *ext = strrchr(bin_path, '.');
    if (ext) strcpy(ext, ".bin");
    else strcat(bin_path, ".bin");

    // If input is already .bin, skip
    if (strcmp(filename, bin_path) == 0) return ESP_OK;

    // Process to .bin
    uint8_t *processed = heap_caps_malloc(EPAPER_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (processed) {
        img_process_opts_t opts;
        img_get_default_opts(&opts);
        
        // Load settings for fit mode
        app_settings_t settings;
        if (storage_load_settings(&settings) == ESP_OK) {
            opts.fit_mode = settings.fit_mode;
        }

        if (img_process_file(filename, processed, EPAPER_BUFFER_SIZE, &opts) == ESP_OK) {
            FILE *f = fopen(bin_path, "wb");
            if (f) {
                fwrite(processed, 1, EPAPER_BUFFER_SIZE, f);
                fclose(f);
                ESP_LOGI(TAG, "Optimized binary generated: %s", bin_path);
            }
        }
        free(processed);
    }

    return ESP_OK;
}
