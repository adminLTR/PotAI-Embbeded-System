#include "cloud_command.h"
#include <string.h>
#include "cJSON.h"
#include "esp_log.h"
#include "sdkconfig.h"
#ifndef CONFIG_A10_COMMAND_MAX_PAYLOAD
#define CONFIG_A10_COMMAND_MAX_PAYLOAD 128
#endif
static const char *TAG = "CLOUD_COMMAND";

esp_err_t cloud_command_decode(const char *expected_topic,
                               const char *topic, size_t topic_len,
                               const char *payload, size_t payload_len,
                               cloud_command_t *out)
{
    if (expected_topic == NULL || topic == NULL || payload == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t expected_len = strlen(expected_topic);
    if (topic_len != expected_len || memcmp(topic, expected_topic, expected_len) != 0) {
        return ESP_ERR_NOT_FOUND;
    }
    if (payload_len == 0 || payload_len > CONFIG_A10_COMMAND_MAX_PAYLOAD) {
        ESP_LOGW(TAG, "[COMMAND] rejected: invalid payload length=%u", (unsigned)payload_len);
        return ESP_ERR_INVALID_SIZE;
    }
    char json_buf[CONFIG_A10_COMMAND_MAX_PAYLOAD + 1];
    memcpy(json_buf, payload, payload_len);
    json_buf[payload_len] = '\0';
    const char *end = NULL;
    cJSON *root = cJSON_ParseWithOpts(json_buf, &end, true);
    if (root == NULL || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        ESP_LOGW(TAG, "[COMMAND] rejected: malformed JSON");
        return ESP_ERR_INVALID_ARG;
    }
    cJSON *led = cJSON_GetObjectItemCaseSensitive(root, "led");
    if (!cJSON_IsBool(led) || cJSON_GetArraySize(root) != 1) {
        cJSON_Delete(root);
        ESP_LOGW(TAG, "[COMMAND] rejected: expected exactly one boolean field 'led'");
        return ESP_ERR_INVALID_ARG;
    }
    out->type = CLOUD_COMMAND_LED_SET;
    out->data.led_set.on = cJSON_IsTrue(led);
    cJSON_Delete(root);
    return ESP_OK;
}
