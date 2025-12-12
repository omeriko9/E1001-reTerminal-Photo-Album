#include "dns_server.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "esp_log.h"
#include <errno.h>

static const char *TAG = "dns_server";
static TaskHandle_t s_dns_task_handle = NULL;
static int s_dns_socket = -1;
static volatile bool s_running = false;

static void dns_server_task(void *pvParameters) {
    char rx_buffer[512];
    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(53);

    s_dns_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (s_dns_socket < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }

    int err = bind(s_dns_socket, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err < 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        close(s_dns_socket);
        s_dns_socket = -1;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "DNS Server started");

    while (s_running) {
        struct sockaddr_in source_addr;
        socklen_t socklen = sizeof(source_addr);
        
        // Set a timeout for recvfrom so we can check s_running periodically
        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        setsockopt(s_dns_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        int len = recvfrom(s_dns_socket, rx_buffer, sizeof(rx_buffer), 0, (struct sockaddr *)&source_addr, &socklen);

        if (len < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
            vTaskDelay(pdMS_TO_TICKS(1000)); // Delay to avoid tight loop on error
            continue;
        }

        if (len > 12) { // Minimum DNS header size
            // DNS Header is 12 bytes
            // Transaction ID: 2 bytes
            // Flags: 2 bytes
            // Questions: 2 bytes
            // Answer RRs: 2 bytes
            // Authority RRs: 2 bytes
            // Additional RRs: 2 bytes

            // We just want to spoof the response.
            // Keep Transaction ID (bytes 0-1)
            // Set Flags (bytes 2-3) to Standard Query Response, No Error
            // 0x8180: Response, Recursion Desired, Recursion Available, No Error
            rx_buffer[2] = 0x81;
            rx_buffer[3] = 0x80;

            // Questions (bytes 4-5) - keep as is (usually 1)
            // Answer RRs (bytes 6-7) - set to 1
            rx_buffer[6] = 0x00;
            rx_buffer[7] = 0x01;
            
            // Authority RRs (bytes 8-9) - set to 0
            rx_buffer[8] = 0x00;
            rx_buffer[9] = 0x00;

            // Additional RRs (bytes 10-11) - set to 0
            rx_buffer[10] = 0x00;
            rx_buffer[11] = 0x00;

            // The question section is variable length.
            // It ends with a 0 byte (end of domain name) + 4 bytes (Type and Class).
            // We need to find the end of the question to append the answer.
            
            int q_len = 0;
            char *q_ptr = rx_buffer + 12;
            while (q_ptr < rx_buffer + len && *q_ptr != 0) {
                q_len++;
                q_ptr++;
            }
            
            if (q_ptr >= rx_buffer + len) {
                continue;
            }

            // Skip null byte and Type/Class (4 bytes)
            q_ptr += 5; 
            
            if (q_ptr > rx_buffer + len) {
                continue;
            }
            
            int answer_offset = q_ptr - rx_buffer;
            
            if (answer_offset < sizeof(rx_buffer) - 16) { // Ensure space for answer
                // Construct Answer
                // Name: Pointer to the name in the question (0xC00C)
                rx_buffer[answer_offset++] = 0xC0;
                rx_buffer[answer_offset++] = 0x0C;
                
                // Type: A (Host Address) = 1
                rx_buffer[answer_offset++] = 0x00;
                rx_buffer[answer_offset++] = 0x01;
                
                // Class: IN (Internet) = 1
                rx_buffer[answer_offset++] = 0x00;
                rx_buffer[answer_offset++] = 0x01;
                
                // TTL: 60 seconds
                rx_buffer[answer_offset++] = 0x00;
                rx_buffer[answer_offset++] = 0x00;
                rx_buffer[answer_offset++] = 0x00;
                rx_buffer[answer_offset++] = 0x3C;
                
                // Data Length: 4 bytes (IPv4)
                rx_buffer[answer_offset++] = 0x00;
                rx_buffer[answer_offset++] = 0x04;
                
                // IP Address: 192.168.4.1
                rx_buffer[answer_offset++] = 192;
                rx_buffer[answer_offset++] = 168;
                rx_buffer[answer_offset++] = 4;
                rx_buffer[answer_offset++] = 1;
                
                sendto(s_dns_socket, rx_buffer, answer_offset, 0, (struct sockaddr *)&source_addr, sizeof(source_addr));
            }
        }
    }

    if (s_dns_socket != -1) {
        close(s_dns_socket);
        s_dns_socket = -1;
    }
    s_dns_task_handle = NULL;
    vTaskDelete(NULL);
}

esp_err_t dns_server_start(void) {
    if (s_running) return ESP_OK;
    s_running = true;
    // Increased stack size for safety
    xTaskCreate(dns_server_task, "dns_server", 6144, NULL, 5, &s_dns_task_handle);
    return ESP_OK;
}

void dns_server_stop(void) {
    s_running = false;
    // Task will exit on next loop
}
