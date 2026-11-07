#include "ml_model.h"

#include <math.h>
#include "esp_log.h"

/* Simbolos creados por EMBED_FILES "model/rf_model.bin". */
extern const uint8_t s_model_start[] asm("_binary_rf_model_bin_start");
extern const uint8_t s_model_end[] asm("_binary_rf_model_bin_end");

static const char *TAG = "ML_MODEL";

/* Formato: ver 03_model_ML/export_model_esp32.py */
#define MODEL_HEADER_SIZE 16u
#define MODEL_VERSION 1u

#define KIND_MOISTURE 0u
#define KIND_TEMP 1u
#define KIND_PLANT 2u
#define KIND_LEAF 3u

static bool s_ready;
static uint16_t s_n_trees;
static uint16_t s_n_nodes;
static uint16_t s_leaf_scale;
static uint16_t s_input_scale;
static const uint8_t *s_roots;
static const uint8_t *s_meta;
static const uint8_t *s_payload;

/*
 * Lectura byte a byte (little endian): el simbolo embebido no garantiza
 * alineacion de 2 bytes y la ESP32 (Xtensa) no admite accesos desalineados.
 */
static inline uint16_t rd16(const uint8_t *base, uint32_t index)
{
    return (uint16_t)(base[2u * index] | ((uint16_t)base[2u * index + 1u] << 8));
}

static uint16_t crc16_ccitt(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

esp_err_t ml_model_init(void)
{
    s_ready = false;
    const size_t size = (size_t)(s_model_end - s_model_start);

    if (size < MODEL_HEADER_SIZE || s_model_start[0] != 'A' || s_model_start[1] != '1' ||
        s_model_start[2] != '0' || s_model_start[3] != 'M') {
        ESP_LOGE(TAG, "binario invalido (magic/tamano): %u bytes", (unsigned)size);
        return ESP_ERR_INVALID_STATE;
    }
    if (s_model_start[4] != MODEL_VERSION) {
        ESP_LOGE(TAG, "version de modelo no soportada: %u", s_model_start[4]);
        return ESP_ERR_NOT_SUPPORTED;
    }

    const uint16_t n_trees = (uint16_t)(s_model_start[6] | (s_model_start[7] << 8));
    const uint16_t n_nodes = (uint16_t)(s_model_start[8] | (s_model_start[9] << 8));
    const uint16_t leaf_scale = (uint16_t)(s_model_start[10] | (s_model_start[11] << 8));
    const uint16_t input_scale = (uint16_t)(s_model_start[12] | (s_model_start[13] << 8));
    const uint16_t crc_expected = (uint16_t)(s_model_start[14] | (s_model_start[15] << 8));

    const size_t expected_size = MODEL_HEADER_SIZE + 2u * n_trees + 4u * n_nodes;
    if (n_trees == 0 || n_nodes == 0 || leaf_scale == 0 || input_scale == 0 || size != expected_size) {
        ESP_LOGE(TAG, "cabecera inconsistente: trees=%u nodes=%u size=%u esperado=%u",
                 n_trees, n_nodes, (unsigned)size, (unsigned)expected_size);
        return ESP_ERR_INVALID_SIZE;
    }

    const uint16_t crc = crc16_ccitt(s_model_start + MODEL_HEADER_SIZE, size - MODEL_HEADER_SIZE);
    if (crc != crc_expected) {
        ESP_LOGE(TAG, "CRC16 invalido: calculado=0x%04X esperado=0x%04X", crc, crc_expected);
        return ESP_ERR_INVALID_CRC;
    }

    s_n_trees = n_trees;
    s_n_nodes = n_nodes;
    s_leaf_scale = leaf_scale;
    s_input_scale = input_scale;
    s_roots = s_model_start + MODEL_HEADER_SIZE;
    s_meta = s_roots + 2u * n_trees;
    s_payload = s_meta + 2u * n_nodes;
    s_ready = true;

    ESP_LOGI(TAG, "modelo OK: %u arboles, %u nodos, %u bytes en flash (0 bytes de RAM)",
             n_trees, n_nodes, (unsigned)size);
    return ESP_OK;
}

bool ml_model_is_ready(void)
{
    return s_ready;
}

uint16_t ml_model_tree_count(void)
{
    return s_n_trees;
}

size_t ml_model_size_bytes(void)
{
    return (size_t)(s_model_end - s_model_start);
}

esp_err_t ml_model_predict_ml(uint8_t plant_id, float soil_moisture_pct, float temperature_c, float *out_ml)
{
    if (out_ml == NULL || !isfinite(soil_moisture_pct) || !isfinite(temperature_c)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    if (soil_moisture_pct < 0.0f) soil_moisture_pct = 0.0f;
    if (soil_moisture_pct > 100.0f) soil_moisture_pct = 100.0f;
    if (temperature_c < -40.0f) temperature_c = -40.0f;
    if (temperature_c > 120.0f) temperature_c = 120.0f;

    const int32_t moisture_q = (int32_t)lroundf(soil_moisture_pct * (float)s_input_scale);
    const int32_t temp_q = (int32_t)lroundf(temperature_c * (float)s_input_scale);

    uint32_t total = 0;
    for (uint32_t t = 0; t < s_n_trees; t++) {
        uint32_t node = rd16(s_roots, t);
        bool done = false;

        /* Un arbol valido nunca visita mas nodos de los que existen. */
        for (uint32_t guard = 0; guard < s_n_nodes; guard++) {
            if (node >= s_n_nodes) {
                return ESP_ERR_INVALID_STATE;
            }
            const uint16_t meta = rd16(s_meta, node);
            const uint16_t payload = rd16(s_payload, node);
            const uint32_t kind = meta >> 14;

            if (kind == KIND_LEAF) {
                total += payload;
                done = true;
                break;
            }

            const uint32_t right = meta & 0x3FFFu;
            switch (kind) {
            case KIND_MOISTURE:
                node = (moisture_q <= (int16_t)payload) ? node + 1u : right;
                break;
            case KIND_TEMP:
                node = (temp_q <= (int16_t)payload) ? node + 1u : right;
                break;
            default: /* KIND_PLANT */
                node = (plant_id == payload) ? right : node + 1u;
                break;
            }
        }
        if (!done) {
            return ESP_ERR_INVALID_STATE;
        }
    }

    *out_ml = (float)total / ((float)s_n_trees * (float)s_leaf_scale);
    return ESP_OK;
}
