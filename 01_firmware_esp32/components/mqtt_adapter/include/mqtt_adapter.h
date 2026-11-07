#pragma once
#include <stddef.h>
#include "esp_err.h"
typedef enum {
    MQTT_ADAPTER_EVENT_BEFORE_CONNECT = 0,
    MQTT_ADAPTER_EVENT_CONNECTED,
    MQTT_ADAPTER_EVENT_DISCONNECTED,
    MQTT_ADAPTER_EVENT_ERROR,
    MQTT_ADAPTER_EVENT_PUBLISHED,
    MQTT_ADAPTER_EVENT_SUBSCRIBED,
    MQTT_ADAPTER_EVENT_DATA
} mqtt_adapter_event_t;
typedef struct {
    mqtt_adapter_event_t type;
    int msg_id;
    const char *topic;
    size_t topic_len;
    const char *payload;
    size_t payload_len;
    int qos;
} mqtt_adapter_event_data_t;
typedef void (*mqtt_adapter_event_cb_t)(const mqtt_adapter_event_data_t *event, void *user_ctx);
typedef struct { const char *uri; const char *client_id; } mqtt_adapter_config_t;
esp_err_t mqtt_adapter_start(const mqtt_adapter_config_t *config, mqtt_adapter_event_cb_t cb, void *user_ctx);
esp_err_t mqtt_adapter_stop(void);
esp_err_t mqtt_adapter_publish(const char *topic, const char *payload, int qos);
esp_err_t mqtt_adapter_subscribe(const char *topic_filter, int qos, int *msg_id_out);
