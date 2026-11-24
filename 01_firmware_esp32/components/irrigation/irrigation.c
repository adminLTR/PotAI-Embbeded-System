#include "irrigation.h"

#include <stdbool.h>
#include "buzzer.h"
#include "device_state.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "ml_model.h"
#include "pump.h"

static const char *TAG = "IRRIGATION";

static irrigation_config_t s_cfg;
static bool s_started;
static TaskHandle_t s_task;
static QueueHandle_t s_manual_queue;

/* Igual que la API: un pedido manual trae entre 10 y 500 ml. */
#define MANUAL_ML_MIN 10u
#define MANUAL_ML_MAX 500u
/* Modo real y modo test: el modelo se consulta cada minuto. */
#define CHECK_PERIOD_MS 60000u

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

static TickType_t decision_period(void)
{
    return pdMS_TO_TICKS(CHECK_PERIOD_MS);
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

    float simulated = 0.0f;
    if (device_state_test_bump_soil(45.0f, now_ms(), &simulated)) {
        ESP_LOGI(TAG, "[TEST] riego simulado: suelo ahora %.0f%%", (double)simulated);
    }
    return delivered;
}

static void run_manual(uint16_t ml, uint32_t *count, uint64_t *last_end_ms, bool *had_irrigation)
{
    device_snapshot_t snap;
    device_state_copy(&snap);
    const uint32_t duration_ms = irrigation_duration_ms((float)ml, s_cfg.flow_ml_per_min, s_cfg.max_on_ms);
    if (duration_ms == 0) {
        ESP_LOGW(TAG, "[IA] riego manual %u ml sin duracion", (unsigned)ml);
        return;
    }

    const float soil = snap.soil_valid ? snap.soil_moisture_pct : 0.0f;
    const float temp = snap.sample_valid ? snap.temperature_c : 0.0f;
    ESP_LOGI(TAG, "[IA] riego manual: %u ml (ignora IA, suelo y cooldown)", (unsigned)ml);
    (*count)++;
    water_plant((float)ml, duration_ms, *count, soil, temp);
    *last_end_ms = now_ms();
    *had_irrigation = true;
}

static TickType_t ticks_until(TickType_t deadline)
{
    const int32_t left = (int32_t)(deadline - xTaskGetTickCount());
    return (left > 0) ? (TickType_t)left : 0;
}

static void irrigation_task(void *arg)
{
    (void)arg;
    uint32_t count = 0;
    uint64_t last_end_ms = 0;
    bool had_irrigation = false;
    TickType_t next_auto = xTaskGetTickCount() + decision_period();

    while (1) {
        const TickType_t wait = s_cfg.automatic ? ticks_until(next_auto) : portMAX_DELAY;
        ulTaskNotifyTake(pdTRUE, wait);
        const TickType_t period = decision_period();

        uint16_t manual_ml = 0;
        const bool did_manual = xQueueReceive(s_manual_queue, &manual_ml, 0) == pdTRUE;
        if (did_manual) {
            run_manual(manual_ml, &count, &last_end_ms, &had_irrigation);
        }

        if (!s_cfg.automatic) {
            continue;
        }
        if (did_manual) {
            next_auto = xTaskGetTickCount() + period;
            continue;
        }
        if ((int32_t)(xTaskGetTickCount() - next_auto) < 0) {
            continue;
        }
        /* Los rechazos rapidos esperan un periodo entero desde ahora. */
        next_auto = xTaskGetTickCount() + period;

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
        next_auto = xTaskGetTickCount() + period;
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
    if (config->automatic && !ml_model_is_ready()) {
        ESP_LOGE(TAG, "modelo ML nao esta pronto: irrigacao automatica desabilitada");
        return ESP_ERR_INVALID_STATE;
    }
    s_cfg = *config;
    s_manual_queue = xQueueCreate(1, sizeof(uint16_t));
    if (s_manual_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreatePinnedToCore(irrigation_task, "irrigation_task", 4096, NULL, 4, &s_task, 1) != pdPASS) {
        vQueueDelete(s_manual_queue);
        s_manual_queue = NULL;
        s_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    s_started = true;
    ESP_LOGI(TAG, "ativo: planta=%u automatico=%d periodo=%lu ms vazao=%lu ml/min max_on=%lu ms",
             s_cfg.plant_id, s_cfg.automatic ? 1 : 0, (unsigned long)CHECK_PERIOD_MS,
             (unsigned long)s_cfg.flow_ml_per_min, (unsigned long)s_cfg.max_on_ms);
    return ESP_OK;
}

esp_err_t irrigation_request_manual(uint16_t ml)
{
    if (!s_started || s_task == NULL || s_manual_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (ml < MANUAL_ML_MIN || ml > MANUAL_ML_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    /* Un solo pedido pendiente: el mas nuevo reemplaza al anterior. */
    if (xQueueOverwrite(s_manual_queue, &ml) != pdPASS) {
        return ESP_ERR_INVALID_STATE;
    }
    xTaskNotifyGive(s_task);
    return ESP_OK;
}
