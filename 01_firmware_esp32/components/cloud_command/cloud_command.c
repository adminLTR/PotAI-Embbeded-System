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
    cJSON *test_on = cJSON_GetObjectItemCaseSensitive(root, "test_on");
    if (cJSON_IsBool(test_on)) {
        const int nkeys = cJSON_GetArraySize(root);
        if (!cJSON_IsTrue(test_on) && nkeys == 1) {
            out->type = CLOUD_COMMAND_TEST;
            out->data.test_sensors.on = false;
            cJSON_Delete(root);
            return ESP_OK;
        }
        cJSON *temp = cJSON_GetObjectItemCaseSensitive(root, "temperature_c");
        cJSON *hum = cJSON_GetObjectItemCaseSensitive(root, "humidity_pct");
        cJSON *soil = cJSON_GetObjectItemCaseSensitive(root, "soil_moisture_pct");
        if (cJSON_IsTrue(test_on) && nkeys == 4 && cJSON_IsNumber(temp) && cJSON_IsNumber(hum) && cJSON_IsNumber(soil)) {
            const double t = temp->valuedouble;
            const double h = hum->valuedouble;
            const double s = soil->valuedouble;
            if (t >= -10.0 && t <= 60.0 && h >= 0.0 && h <= 100.0 && s >= 0.0 && s <= 100.0) {
                out->type = CLOUD_COMMAND_TEST;
                out->data.test_sensors.on = true;
                out->data.test_sensors.temperature_c = (float)t;
                out->data.test_sensors.humidity_pct = (float)h;
                out->data.test_sensors.soil_moisture_pct = (float)s;
                cJSON_Delete(root);
                return ESP_OK;
            }
        }
        cJSON_Delete(root);
        ESP_LOGW(TAG, "[COMMAND] rejected: test_on true needs temperature_c, humidity_pct, soil_moisture_pct");
        return ESP_ERR_INVALID_ARG;
    }

    if (cJSON_GetArraySize(root) != 1) {
        cJSON_Delete(root);
        ESP_LOGW(TAG, "[COMMAND] rejected: expected led, irrigate_ml or test_on");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *led = cJSON_GetObjectItemCaseSensitive(root, "led");
    if (cJSON_IsBool(led)) {
        out->type = CLOUD_COMMAND_LED_SET;
        out->data.led_set.on = cJSON_IsTrue(led);
        cJSON_Delete(root);
        return ESP_OK;
    }

    cJSON *ml = cJSON_GetObjectItemCaseSensitive(root, "irrigate_ml");
    if (cJSON_IsNumber(ml)) {
        const double value = ml->valuedouble;
        const int whole = (int)value;
        /* Mismo rango que la API: 10..500 ml, entero. La duracion la corta max_on_ms. */
        if (value >= 10.0 && value <= 500.0 && value == (double)whole) {
            out->type = CLOUD_COMMAND_IRRIGATE;
            out->data.irrigate.ml = (uint16_t)whole;
            cJSON_Delete(root);
            return ESP_OK;
        }
    }

    cJSON_Delete(root);
    ESP_LOGW(TAG, "[COMMAND] rejected: expected {\"led\":bool} or {\"irrigate_ml\":10..500}");
    return ESP_ERR_INVALID_ARG;
}
