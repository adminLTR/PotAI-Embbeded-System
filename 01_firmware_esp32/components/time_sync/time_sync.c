#include "time_sync.h"
#include <time.h>
#include <sys/time.h>
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

static const char *TAG = "TIME_SYNC";
static time_sync_state_t s_state = TIME_SYNC_NOT_SYNCED;
static bool s_initialized = false;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static void set_state(time_sync_state_t state)
{
    portENTER_CRITICAL(&s_lock);
    s_state = state;
    portEXIT_CRITICAL(&s_lock);
}

static void time_sync_notification_cb(struct timeval *tv)
{
    (void)tv;
    set_state(TIME_SYNC_SYNCED);
    time_t now = 0;
    struct tm utc = {0};
    time(&now);
    gmtime_r(&now, &utc);
    char stamp[32] = {0};
    strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%SZ", &utc);
    ESP_LOGI(TAG, "[TIME] SYNCED utc=%s", stamp);
}

esp_err_t time_sync_start(const char *server_name)
{
    bool initialized;
    portENTER_CRITICAL(&s_lock);
    initialized = s_initialized;
    portEXIT_CRITICAL(&s_lock);
    if (initialized) return ESP_OK;
    if (server_name == NULL || server_name[0] == '\0') {
        set_state(TIME_SYNC_ERROR);
        return ESP_ERR_INVALID_ARG;
    }
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(server_name);
    config.sync_cb = time_sync_notification_cb;
    set_state(TIME_SYNC_SYNCING);
    ESP_LOGI(TAG, "[TIME] SYNCING server=%s", server_name);
    esp_err_t err = esp_netif_sntp_init(&config);
    if (err != ESP_OK) {
        set_state(TIME_SYNC_ERROR);
        ESP_LOGE(TAG, "[TIME] init failed: %s", esp_err_to_name(err));
        return err;
    }
    portENTER_CRITICAL(&s_lock);
    s_initialized = true;
    portEXIT_CRITICAL(&s_lock);
    return ESP_OK;
}

time_sync_state_t time_sync_get_state(void)
{
    time_sync_state_t state;
    portENTER_CRITICAL(&s_lock);
    state = s_state;
    portEXIT_CRITICAL(&s_lock);
    return state;
}

bool time_sync_is_ready(void) { return time_sync_get_state() == TIME_SYNC_SYNCED; }
const char *time_sync_state_name(time_sync_state_t state)
{
    switch (state) {
    case TIME_SYNC_NOT_SYNCED: return "NOT_SYNCED";
    case TIME_SYNC_SYNCING: return "SYNCING";
    case TIME_SYNC_SYNCED: return "SYNCED";
    case TIME_SYNC_ERROR: return "ERROR";
    default: return "UNKNOWN";
    }
}
