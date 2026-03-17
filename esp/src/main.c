#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "mqtt_client.h"

#define WIFI_SSID "SSID"
#define WIFI_PASS "PASS"

// Mode: 0 = local Mosquitto plain, 1 = AWS IoT Core mTLS, 2 = local Mosquitto TLS
#define MODE            1

#if MODE == 1
#define MQTT_BROKER_URI "mqtts://??.amazonaws.com:8883"
#elif MODE == 2
#define MQTT_BROKER_URI "mqtts://??:8883"
#else
#define MQTT_BROKER_URI "mqtt://??:1883"
#endif

#define MQTT_TOPIC      "esp32/data"
#define MQTT_QOS        0       // 0 = fire-and-forget, 1 = at-least-once
#define SEND_BUF_SIZE   4096
#define NUM_CLIENTS     2

#if MODE == 1
#include "cert_data.h"
#include "key_data.h"
#include "client_cert_data.h"
#elif MODE == 2
#include "local_ca_data.h"
#endif

static const char *TAG = "mqtt_blast";
static volatile bool wifi_connected = false;

typedef struct {
    int id;
    esp_mqtt_client_handle_t client;
    volatile bool connected;
    volatile uint32_t bytes_sent;
    volatile uint32_t msgs_sent;
    volatile uint32_t send_errors;
} mqtt_ctx_t;

static mqtt_ctx_t clients[NUM_CLIENTS];

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_connected = false;
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

    while (!wifi_connected) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                                int32_t event_id, void *event_data)
{
    mqtt_ctx_t *ctx = (mqtt_ctx_t *)handler_args;
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "Client %d: MQTT connected", ctx->id);
        ctx->connected = true;
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "Client %d: MQTT disconnected", ctx->id);
        ctx->connected = false;
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "Client %d: MQTT error type: %d", ctx->id, event->error_handle->error_type);
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_ESP_TLS) {
            ESP_LOGE(TAG, "Client %d: TLS error: 0x%x", ctx->id, event->error_handle->esp_tls_last_esp_err);
        }
        break;

    default:
        break;
    }
}

static void mqtt_blast_task(void *pvParameters)
{
    mqtt_ctx_t *ctx = (mqtt_ctx_t *)pvParameters;

    char *buf = heap_caps_malloc(SEND_BUF_SIZE, MALLOC_CAP_DEFAULT);
    if (!buf) {
        ESP_LOGE(TAG, "Client %d: Failed to allocate send buffer", ctx->id);
        vTaskDelete(NULL);
        return;
    }
    memset(buf, 'X', SEND_BUF_SIZE);

    while (!ctx->connected) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    ESP_LOGI(TAG, "Client %d: Starting blast, %d byte payloads, QoS %d", ctx->id, SEND_BUF_SIZE, MQTT_QOS);

    while (1) {
        if (!ctx->connected) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        int ret = esp_mqtt_client_publish(ctx->client, MQTT_TOPIC, buf, SEND_BUF_SIZE, MQTT_QOS, 0);
        if (ret >= 0) {
            ctx->bytes_sent += SEND_BUF_SIZE;
            ctx->msgs_sent++;
        } else {
            ctx->send_errors++;
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        taskYIELD();
    }
}

static void stats_task(void *pvParameters)
{
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        uint32_t total_bytes = 0, total_msgs = 0, total_errors = 0;

        for (int i = 0; i < NUM_CLIENTS; i++) {
            uint32_t b = clients[i].bytes_sent;
            uint32_t m = clients[i].msgs_sent;
            uint32_t e = clients[i].send_errors;
            clients[i].bytes_sent = 0;
            clients[i].msgs_sent = 0;
            clients[i].send_errors = 0;
            total_bytes += b;
            total_msgs += m;
            total_errors += e;
        }

        float mbps = (total_bytes * 8.0f) / 1000000.0f;

        printf("[MQTT %d clients] %.2f Mbps | %lu msgs/s | %lu bytes/s | %lu errors | heap: %lu\n",
               NUM_CLIENTS,
               mbps,
               (unsigned long)total_msgs,
               (unsigned long)total_bytes,
               (unsigned long)total_errors,
               (unsigned long)esp_get_free_heap_size());
    }
}

static void mqtt_start_client(mqtt_ctx_t *ctx, int id)
{
    ctx->id = id;
    ctx->connected = false;
    ctx->bytes_sent = 0;
    ctx->msgs_sent = 0;
    ctx->send_errors = 0;

    char client_id[32];
    snprintf(client_id, sizeof(client_id), "esp32-blast-%d", id);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker = {
            .address.uri = MQTT_BROKER_URI,
#if MODE == 1
            .verification = {
                .certificate = (const char *)AmazonRootCA1_pem,
                .certificate_len = AmazonRootCA1_pem_len,
            },
#elif MODE == 2
            .verification = {
                .certificate = (const char *)local_ca_pem,
                .certificate_len = local_ca_pem_len,
            },
#endif
        },
        .credentials = {
            .client_id = client_id,
#if MODE == 1
            .authentication = {
                .certificate = (const char *)device_cert_pem,
                .certificate_len = device_cert_pem_len,
                .key = (const char *)device_private_key,
                .key_len = device_private_key_len,
            },
#endif
        },
        .network = {
            .disable_auto_reconnect = false,
        },
        .buffer = {
            .size = SEND_BUF_SIZE + 256,
            .out_size = SEND_BUF_SIZE + 256,
        },
        .outbox = {
            .limit = MQTT_QOS == 1 ? 16 * 1024 : 0,
        },
    };

    ctx->client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(ctx->client, ESP_EVENT_ANY_ID, mqtt_event_handler, ctx);
    esp_mqtt_client_start(ctx->client);

    ESP_LOGI(TAG, "Client %d (%s) started", id, client_id);
}

void app_main(void)
{
    esp_log_set_vprintf(vprintf);
    wifi_init();

    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"), &ip_info);
    ESP_LOGI(TAG, "My IP: " IPSTR, IP2STR(&ip_info.ip));

    // Start all MQTT clients
    for (int i = 0; i < NUM_CLIENTS; i++) {
        mqtt_start_client(&clients[i], i);
        // Stagger connections slightly to avoid TLS handshake pile-up
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    // Create blast tasks - distribute across cores
    for (int i = 0; i < NUM_CLIENTS; i++) {
        char name[16];
        snprintf(name, sizeof(name), "blast_%d", i);
        int core = (i % 2 == 0) ? 1 : 0;
        xTaskCreatePinnedToCore(mqtt_blast_task, name, 4096, &clients[i], 5, NULL, core);
    }

    xTaskCreatePinnedToCore(stats_task, "stats", 3072, NULL, 4, NULL, 0);

    vTaskDelete(NULL);
}
