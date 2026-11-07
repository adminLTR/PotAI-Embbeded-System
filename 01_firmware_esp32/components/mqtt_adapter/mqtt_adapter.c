#include "mqtt_adapter.h"
#include "aws_iot_credentials.h"
#include "mqtt_client.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_event.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

static const char *TAG = "mqtt_adapter";
static esp_mqtt_client_handle_t s_client;
static mqtt_adapter_event_cb_t s_cb;
static void *s_user_ctx;

static void emit(mqtt_adapter_event_t type, esp_mqtt_event_handle_t event)
{
    if (s_cb == NULL) return;
    mqtt_adapter_event_data_t out = {
        .type = type,
        .msg_id = event ? event->msg_id : -1,
        .qos = event ? event->qos : 0,
    };
    if (event && type == MQTT_ADAPTER_EVENT_DATA) {
        out.topic = event->topic;
        out.topic_len = event->topic_len > 0 ? (size_t)event->topic_len : 0;
        out.payload = event->data;
        out.payload_len = event->data_len > 0 ? (size_t)event->data_len : 0;
    }
    s_cb(&out, s_user_ctx);
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)handler_args; (void)base;
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_BEFORE_CONNECT:
        ESP_LOGI(TAG, "MQTT_EVENT_BEFORE_CONNECT");
        emit(MQTT_ADAPTER_EVENT_BEFORE_CONNECT, event); break;
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        emit(MQTT_ADAPTER_EVENT_CONNECTED, event); break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT_EVENT_DISCONNECTED");
        emit(MQTT_ADAPTER_EVENT_DISCONNECTED, event); break;
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED msg_id=%d", event->msg_id);
        emit(MQTT_ADAPTER_EVENT_SUBSCRIBED, event); break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED msg_id=%d", event->msg_id);
        emit(MQTT_ADAPTER_EVENT_PUBLISHED, event); break;
    case MQTT_EVENT_DATA:
        if (event->current_data_offset != 0 || event->data_len != event->total_data_len) {
            ESP_LOGW(TAG, "fragmented MQTT payload rejected offset=%d data_len=%d total=%d",
                     event->current_data_offset, event->data_len, event->total_data_len);
            return;
        }
        emit(MQTT_ADAPTER_EVENT_DATA, event); break;
    case MQTT_EVENT_ERROR:
        if (event->error_handle != NULL) {
            ESP_LOGE(TAG, "MQTT_EVENT_ERROR type=%d tls_flags=0x%x sock_errno=%d",
                     event->error_handle->error_type,
                     event->error_handle->esp_tls_cert_verify_flags,
                     event->error_handle->esp_transport_sock_errno);
        } else {
            ESP_LOGE(TAG, "MQTT_EVENT_ERROR");
        }
        emit(MQTT_ADAPTER_EVENT_ERROR, event); break;
    default:
        break;
    }
}

esp_err_t mqtt_adapter_start(const mqtt_adapter_config_t *config, mqtt_adapter_event_cb_t cb, void *user_ctx)
{
    if (config == NULL || config->uri == NULL || config->uri[0] == '\0' ||
        config->client_id == NULL || config->client_id[0] == '\0') return ESP_ERR_INVALID_ARG;
    if (s_client != NULL) return ESP_OK;
    if (!aws_iot_credentials_are_provisioned()) {
        ESP_LOGE(TAG, "AWS IoT device credentials are not provisioned; MQTT/TLS start refused");
        return ESP_ERR_INVALID_STATE;
    }
    s_cb = cb; s_user_ctx = user_ctx;
    esp_mqtt_client_config_t cfg = {
        .broker = {
            .address.uri = config->uri,
            .verification.certificate = aws_iot_root_ca_pem(),
        },
        .credentials = {
            .client_id = config->client_id,
            .authentication = {
                .certificate = aws_iot_device_certificate_pem(),
                .key = aws_iot_private_key_pem(),
            },
        },
        .network.disable_auto_reconnect = false,
    };
    s_client = esp_mqtt_client_init(&cfg);
    if (s_client == NULL) return ESP_FAIL;
    esp_err_t err = esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    if (err != ESP_OK) { esp_mqtt_client_destroy(s_client); s_client = NULL; return err; }
    err = esp_mqtt_client_start(s_client);
    if (err != ESP_OK) { esp_mqtt_client_destroy(s_client); s_client = NULL; }
    return err;
}

esp_err_t mqtt_adapter_stop(void)
{
    if (s_client == NULL) return ESP_OK;
    esp_err_t err = esp_mqtt_client_stop(s_client);
    esp_mqtt_client_destroy(s_client);
    s_client = NULL;
    return err;
}

esp_err_t mqtt_adapter_publish(const char *topic, const char *payload, int qos)
{
    if (s_client == NULL || topic == NULL || payload == NULL) return ESP_ERR_INVALID_STATE;
    int msg_id = esp_mqtt_client_publish(s_client, topic, payload, 0, qos, 0);
    if (msg_id < 0) return ESP_FAIL;
    ESP_LOGI(TAG, "publish accepted topic=%s msg_id=%d qos=%d", topic, msg_id, qos);
    return ESP_OK;
}

esp_err_t mqtt_adapter_subscribe(const char *topic_filter, int qos, int *msg_id_out)
{
    if (s_client == NULL || topic_filter == NULL || topic_filter[0] == '\0') return ESP_ERR_INVALID_STATE;
    int msg_id = esp_mqtt_client_subscribe(s_client, topic_filter, qos);
    if (msg_id < 0) return ESP_FAIL;
    if (msg_id_out) *msg_id_out = msg_id;
    ESP_LOGI(TAG, "subscribe requested topic=%s msg_id=%d qos=%d", topic_filter, msg_id, qos);
    return ESP_OK;
}
