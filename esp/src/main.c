#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_http_server.h"

#define WIFI_SSID "SSID"
#define WIFI_PASS "PASS"
#define SERVER_PORT 5000
#define SEND_BUF_SIZE 16384

static const char *TAG = "ws_blast";
static volatile bool wifi_connected = false;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_connected = false;
        ESP_LOGI(TAG, "Disconnected, reconnecting...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        wifi_connected = true;
    }
}

static void wifi_init(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connecting to WiFi...");
    while (!wifi_connected) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "WebSocket handshake from client");
        return ESP_OK;
    }

    // Receive the trigger message from client
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_BINARY;
    uint8_t recv_buf[128];
    ws_pkt.payload = recv_buf;
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, sizeof(recv_buf));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to receive ws frame: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Received trigger, starting blast...");

    // Now blast data back
    char *buf = malloc(SEND_BUF_SIZE);
    if (!buf) {
        ESP_LOGE(TAG, "Failed to allocate send buffer");
        return ESP_ERR_NO_MEM;
    }
    memset(buf, 'X', SEND_BUF_SIZE);

    httpd_ws_frame_t send_pkt;
    memset(&send_pkt, 0, sizeof(httpd_ws_frame_t));
    send_pkt.final = true;
    send_pkt.fragmented = false;
    send_pkt.type = HTTPD_WS_TYPE_BINARY;
    send_pkt.payload = (uint8_t *)buf;
    send_pkt.len = SEND_BUF_SIZE;

    int fd = httpd_req_to_sockfd(req);
    while (1) {
        ret = httpd_ws_send_frame_async(req->handle, fd, &send_pkt);
        if (ret != ESP_OK) {
            ESP_LOGI(TAG, "Client disconnected (send error: %s)", esp_err_to_name(ret));
            break;
        }
    }

    free(buf);
    return ESP_OK;
}

static const httpd_uri_t ws_uri = {
    .uri = "/ws",
    .method = HTTP_GET,
    .handler = ws_handler,
    .is_websocket = true,
};

static httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = SERVER_PORT;
    config.send_wait_timeout = 30;
    config.recv_wait_timeout = 30;

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &ws_uri);
        ESP_LOGI(TAG, "WebSocket server started on port %d", SERVER_PORT);
    }
    return server;
}

void app_main(void)
{
    wifi_init();

    // Print our IP address
    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"), &ip_info);
    ESP_LOGI(TAG, "My IP: " IPSTR, IP2STR(&ip_info.ip));

    start_webserver();

    // Keep main task alive
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
