#pragma once
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"
#include "telemetry.h"
#include "health.h"
#include "mqtt_adapter.h"
#include "cloud_command.h"
typedef enum { MQTT_SERVICE_OFFLINE=0, MQTT_SERVICE_CONNECTING, MQTT_SERVICE_ONLINE } mqtt_service_state_t;
typedef esp_err_t (*mqtt_service_command_cb_t)(const cloud_command_t *command, void *ctx);
typedef struct {
    const char *thing_name;
    int telemetry_qos;
    int event_qos;
    int health_qos;
    int command_qos;
    mqtt_service_command_cb_t command_cb;
    void *command_ctx;
} mqtt_service_config_t;
esp_err_t mqtt_service_init(const mqtt_service_config_t *config);
esp_err_t mqtt_service_start_aws(const char *endpoint);
mqtt_service_state_t mqtt_service_get_state(void);
bool mqtt_service_is_started(void);
bool mqtt_service_is_command_subscribed(void);
bool mqtt_service_endpoint_is_valid(const char *endpoint);
const char *mqtt_service_state_to_string(mqtt_service_state_t state);
void mqtt_service_handle_adapter_event(const mqtt_adapter_event_data_t *event, void *user_ctx);
esp_err_t mqtt_service_build_topic(const char *thing_name, const char *kind, char *buffer, size_t buffer_len);
esp_err_t mqtt_service_publish_telemetry(const telemetry_data_t *data);
esp_err_t mqtt_service_publish_event(const char *event_name);
esp_err_t mqtt_service_publish_health(const health_data_t *data);
