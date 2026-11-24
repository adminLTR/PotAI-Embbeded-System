#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum {
    CLOUD_COMMAND_LED_SET = 0,
    CLOUD_COMMAND_IRRIGATE = 1,
    CLOUD_COMMAND_TEST = 2,
} cloud_command_type_t;
typedef struct {
    cloud_command_type_t type;
    union {
        struct { bool on; } led_set;
        struct { uint16_t ml; } irrigate;
        struct {
            bool on;
            float temperature_c;
            float humidity_pct;
            float soil_moisture_pct;
        } test_sensors;
    } data;
} cloud_command_t;
esp_err_t cloud_command_decode(const char *expected_topic,
                               const char *topic, size_t topic_len,
                               const char *payload, size_t payload_len,
                               cloud_command_t *out);
#ifdef __cplusplus
}
#endif
