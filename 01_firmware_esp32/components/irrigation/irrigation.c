#include "irrigation.h"

#include <stdbool.h>
#include "buzzer.h"
#include "device_state.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ml_model.h"
#include "pump.h"

static const char *TAG = "IRRIGATION";

static irrigation_config_t s_cfg;
static bool s_started;

static uint64_t now_ms(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000ULL);
}

static void emit_event(const char *name)
{
    if (s_cfg.event_cb != NULL) {
        s_cfg.event_cb(name, s_cfg.event_ctx);
    }
}

uint32_t irrigation_duration_ms(float ml, uint32_t flow_ml_per_min, uint32_t max_on_ms)
{
    if (ml <= 0.0f || flow_ml_per_min == 0) {
        return 0;
    }
    const float ms_f = ml * 60000.0f / (float)flow_ml_per_min;
    uint32_t ms = (ms_f >= (float)UINT32_MAX) ? UINT32_MAX : (uint32_t)(ms_f + 0.5f);
    if (max_on_ms > 0 && ms > max_on_ms) {
        ms = max_on_ms;
    }
    return ms;
}

/* Executa uma irrigacao completa. Retorna os ml efetivamente entregues. */
static float water_plant(float ml_predicted, uint32_t duration_ms, uint32_t count_after,
                         float soil_before_pct, float temperature_c)
{
    const uint64_t t_start = now_ms();

    if (pump_start(duration_ms) != ESP_OK) {
        ESP_LOGE(TAG, "falha ao ligar a bomba");
        return 0.0f;
    }
    device_state_update_irrigation(true, ml_predicted, 0.0f, count_after - 1, t_start);
    emit_event("pump_on");
    ESP_LOGI(TAG, "[IA] bomba ON: previsto=%.0f ml duracao=%lu ms", (double)ml_predicted, (unsigned long)duration_ms);

    if (s_cfg.beep_count > 0) {
        buzzer_beep(150, 100, s_cfg.beep_count);
    }

    /* O esp_timer da bomba a desliga sozinho; aqui so esperamos (margem de 2 s). */
    const uint64_t deadline = t_start + duration_ms + 2000;
    while (pump_is_on() && now_ms() < deadline) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (pump_is_on()) {
        ESP_LOGE(TAG, "bomba excedeu o tempo previsto: desligando a forca");
        pump_stop();
    }

    const float delivered = (float)(now_ms() - t_start) * (float)s_cfg.flow_ml_per_min / 60000.0f;
    device_state_update_irrigation(false, ml_predicted, delivered, count_after, now_ms());
    emit_event("pump_off");
    ESP_LOGI(TAG, "[IA] bomba OFF: entregue~%.0f ml (riegos=%lu)", (double)delivered, (unsigned long)count_after);

    if (s_cfg.record_cb != NULL) {
        const irrigation_record_t rec = {
            .seq = count_after,
            .plant_id = s_cfg.plant_id,
            .soil_moisture_pct = soil_before_pct,
            .temperature_c = temperature_c,
            .ml_predicted = ml_predicted,
            .ml_delivered = delivered,
            .duration_ms = (uint32_t)(now_ms() - t_start),
            .uptime_ms = now_ms(),
        };
        s_cfg.record_cb(&rec, s_cfg.record_ctx);
    }
    return delivered;
}

static void irrigation_task(void *arg)
{
    (void)arg;
    uint32_t count = 0;
    uint64_t last_end_ms = 0;
    bool had_irrigation = false;
    TickType_t last_wake = xTaskGetTickCount();

    while (1) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(s_cfg.check_period_ms));

        device_snapshot_t snap;
        device_state_copy(&snap);
        const uint64_t t = now_ms();

        if (!snap.sample_valid || !snap.soil_valid ||
            (t - snap.sample_uptime_ms) > s_cfg.max_sample_age_ms ||
            (t - snap.soil_sample_uptime_ms) > s_cfg.max_sample_age_ms) {
            ESP_LOGW(TAG, "[IA] sem leituras validas/recentes (DHT11 ok=%d, solo ok=%d): bomba nao acionada",
                     snap.sample_valid ? 1 : 0, snap.soil_valid ? 1 : 0);
            continue;
        }

        float ml = 0.0f;
        esp_err_t err = ml_model_predict_ml(s_cfg.plant_id, snap.soil_moisture_pct, snap.temperature_c, &ml);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "[IA] inferencia falhou: %s", esp_err_to_name(err));
            continue;
        }
        ESP_LOGI(TAG, "[IA] planta=%u solo=%.1f%% temp=%.1fC -> %.1f ml",
                 s_cfg.plant_id, (double)snap.soil_moisture_pct, (double)snap.temperature_c, (double)ml);
        device_state_update_irrigation(false, ml, snap.last_ml_delivered, count, t);

        if (ml < (float)s_cfg.min_ml) {
            continue;
        }
        if (snap.soil_moisture_pct >= s_cfg.soil_max_pct) {
            ESP_LOGW(TAG, "[IA] solo %.1f%% >= limite %.1f%%: irrigacao bloqueada",
                     (double)snap.soil_moisture_pct, (double)s_cfg.soil_max_pct);
            continue;
        }
        if (had_irrigation && (t - last_end_ms) < (uint64_t)s_cfg.cooldown_s * 1000ULL) {
            ESP_LOGI(TAG, "[IA] em cooldown: faltam %lu s",
                     (unsigned long)((uint64_t)s_cfg.cooldown_s - (t - last_end_ms) / 1000ULL));
            continue;
        }

        const uint32_t duration_ms = irrigation_duration_ms(ml, s_cfg.flow_ml_per_min, s_cfg.max_on_ms);
        if (duration_ms == 0) {
            continue;
        }
        count++;
        water_plant(ml, duration_ms, count, snap.soil_moisture_pct, snap.temperature_c);
        last_end_ms = now_ms();
        had_irrigation = true;
    }
}

esp_err_t irrigation_start(const irrigation_config_t *config)
{
    if (config == NULL || config->check_period_ms == 0 || config->flow_ml_per_min == 0 ||
        config->max_on_ms == 0 || config->plant_id == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_started) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!ml_model_is_ready()) {
        ESP_LOGE(TAG, "modelo ML nao esta pronto: irrigacao automatica desabilitada");
        return ESP_ERR_INVALID_STATE;
    }
    s_cfg = *config;

    if (xTaskCreatePinnedToCore(irrigation_task, "irrigation_task", 4096, NULL, 4, NULL, 1) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    s_started = true;
    ESP_LOGI(TAG, "ativo: planta=%u periodo=%lu ms cooldown=%lu s vazao=%lu ml/min max_on=%lu ms",
             s_cfg.plant_id, (unsigned long)s_cfg.check_period_ms, (unsigned long)s_cfg.cooldown_s,
             (unsigned long)s_cfg.flow_ml_per_min, (unsigned long)s_cfg.max_on_ms);
    return ESP_OK;
}
