#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum { CLOUD_COMMAND_LED_SET = 0 } cloud_command_type_t;
typedef struct {
    cloud_command_type_t type;
    union { struct { bool on; } led_set; } data;
} cloud_command_t;
esp_err_t cloud_command_decode(const char *expected_topic,
                               const char *topic, size_t topic_len,
                               const char *payload, size_t payload_len,
                               cloud_command_t *out);
#ifdef __cplusplus
}
#endif
