#include "soil_moisture.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

static const char *TAG = "SOIL";

/* Fora desta faixa o sensor esta, quase certamente, desconectado ou em curto. */
#define SOIL_RAW_MIN_VALID 100
#define SOIL_RAW_MAX_VALID 4090

static adc_oneshot_unit_handle_t s_adc;
static adc_channel_t s_channel;
static soil_moisture_config_t s_cfg;
static bool s_ready;

float soil_moisture_raw_to_pct(int raw, int raw_dry, int raw_wet)
{
    if (raw_dry == raw_wet) {
        return 0.0f;
    }
    float pct = ((float)(raw_dry - raw) * 100.0f) / (float)(raw_dry - raw_wet);
    if (pct < 0.0f) pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;
    return pct;
}

esp_err_t soil_moisture_init(const soil_moisture_config_t *config)
{
    if (config == NULL || config->samples < 1 || config->raw_dry == config->raw_wet) {
        return ESP_ERR_INVALID_ARG;
    }
    s_cfg = *config;

    adc_unit_t unit;
    esp_err_t err = adc_oneshot_io_to_channel(config->adc_gpio, &unit, &s_channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GPIO%d nao e um pino ADC valido", config->adc_gpio);
        return err;
    }
    if (unit != ADC_UNIT_1) {
        ESP_LOGE(TAG, "GPIO%d pertence ao ADC2 (conflita com Wi-Fi). Use GPIO32..39", config->adc_gpio);
        return ESP_ERR_NOT_SUPPORTED;
    }

    adc_oneshot_unit_init_cfg_t unit_cfg = {.unit_id = ADC_UNIT_1};
    err = adc_oneshot_new_unit(&unit_cfg, &s_adc);
    if (err != ESP_OK) {
        return err;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12, /* ~0..3.1 V */
        .bitwidth = ADC_BITWIDTH_12,
    };
    err = adc_oneshot_config_channel(s_adc, s_channel, &chan_cfg);
    if (err != ESP_OK) {
        adc_oneshot_del_unit(s_adc);
        s_adc = NULL;
        return err;
    }

    s_ready = true;
    ESP_LOGI(TAG, "ADC1 canal %d (GPIO%d) dry=%d wet=%d samples=%d",
             (int)s_channel, config->adc_gpio, config->raw_dry, config->raw_wet, config->samples);
    return ESP_OK;
}

esp_err_t soil_moisture_read(float *out_pct, int *out_raw)
{
    if (out_pct == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    int sum = 0;
    for (int i = 0; i < s_cfg.samples; i++) {
        int raw = 0;
        esp_err_t err = adc_oneshot_read(s_adc, s_channel, &raw);
        if (err != ESP_OK) {
            return err;
        }
        sum += raw;
    }
    const int avg = sum / s_cfg.samples;
    if (out_raw != NULL) {
        *out_raw = avg;
    }
    if (avg < SOIL_RAW_MIN_VALID || avg > SOIL_RAW_MAX_VALID) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    *out_pct = soil_moisture_raw_to_pct(avg, s_cfg.raw_dry, s_cfg.raw_wet);
    return ESP_OK;
}
