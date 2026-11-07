#include "wifi_adapter.h"

#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

static const char *TAG = "WIFI_ADAPTER";

static wifi_adapter_event_cb_t s_cb;
static void *s_ctx;
static bool s_initialized;
static bool s_started;

static void emit(wifi_adapter_event_t event)
{
    if (s_cb != NULL) {
        s_cb(event, s_ctx);
    }
}

void wifi_adapter_set_callback(
    wifi_adapter_event_cb_t cb,
    void *ctx)
{
    s_cb = cb;
    s_ctx = ctx;
}

static void wifi_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data)
{
    (void)arg;
    (void)event_data;

    if (event_base != WIFI_EVENT) {
        return;
    }

    if (event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(
            TAG,
            "WIFI_EVENT_STA_START: driver STA iniciado; solicitando associacao");

        esp_err_t ret = esp_wifi_connect();
        if (ret != ESP_OK) {
            ESP_LOGW(
                TAG,
                "esp_wifi_connect() falhou apos STA_START: %s",
                esp_err_to_name(ret));
            emit(WIFI_ADAPTER_EVENT_DISCONNECTED);
        }
        return;
    }

    if (event_id == WIFI_EVENT_STA_CONNECTED) {
        ESP_LOGI(
            TAG,
            "WIFI_EVENT_STA_CONNECTED: ASSOCIATED; enlace Wi-Fi estabelecido");
        emit(WIFI_ADAPTER_EVENT_ASSOCIATED);
        return;
    }

    if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *disc =
            (const wifi_event_sta_disconnected_t *)event_data;

        int reason = disc ? disc->reason : -1;
        ESP_LOGW(
            TAG,
            "WIFI_EVENT_STA_DISCONNECTED: DISCONNECTED reason=%d",
            reason);
        emit(WIFI_ADAPTER_EVENT_DISCONNECTED);
        return;
    }
}

static void ip_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data)
{
    (void)arg;

    if (event_base != IP_EVENT || event_id != IP_EVENT_STA_GOT_IP) {
        return;
    }

    const ip_event_got_ip_t *event =
        (const ip_event_got_ip_t *)event_data;

    ESP_LOGI(
        TAG,
        "IP_EVENT_STA_GOT_IP: IP_READY ip=" IPSTR,
        IP2STR(&event->ip_info.ip));
    emit(WIFI_ADAPTER_EVENT_IP_READY);
}

esp_err_t wifi_adapter_init(void)
{
    if (s_initialized) {
        ESP_LOGI(TAG, "wifi_adapter_init(): infraestrutura Wi-Fi ja inicializada");
        return ESP_OK;
    }

    if (strlen(CONFIG_A10_WIFI_SSID) == 0) {
        ESP_LOGW(TAG, "SSID vazio: conectividade Wi-Fi nao sera iniciada");
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        return ret;
    }
    ESP_LOGI(TAG, "nvs_flash_init(): NVS pronta para o subsistema Wi-Fi");

    ret = esp_netif_init();
    if (ret != ESP_OK) {
        return ret;
    }
    ESP_LOGI(TAG, "esp_netif_init(): infraestrutura de interfaces de rede pronta");

    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }
    ESP_LOGI(TAG, "esp_event_loop_create_default(): Event Loop padrao disponivel");

    esp_netif_t *netif = esp_netif_create_default_wifi_sta();
    if (netif == NULL) {
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "esp_netif_create_default_wifi_sta(): interface STA criada");

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        return ret;
    }
    ESP_LOGI(TAG, "esp_wifi_init(): driver Wi-Fi inicializado");

    ret = esp_event_handler_instance_register(
        WIFI_EVENT,
        ESP_EVENT_ANY_ID,
        wifi_event_handler,
        NULL,
        NULL);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = esp_event_handler_instance_register(
        IP_EVENT,
        IP_EVENT_STA_GOT_IP,
        ip_event_handler,
        NULL,
        NULL);
    if (ret != ESP_OK) {
        return ret;
    }
    ESP_LOGI(TAG, "handlers separados registrados para WIFI_EVENT e IP_EVENT_STA_GOT_IP");

    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        return ret;
    }
    ESP_LOGI(TAG, "esp_wifi_set_mode(WIFI_MODE_STA): ESP32 configurado como STA");

    wifi_config_t wifi_config = {0};
    strlcpy(
        (char *)wifi_config.sta.ssid,
        CONFIG_A10_WIFI_SSID,
        sizeof(wifi_config.sta.ssid));
    strlcpy(
        (char *)wifi_config.sta.password,
        CONFIG_A10_WIFI_PASSWORD,
        sizeof(wifi_config.sta.password));

    ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (ret != ESP_OK) {
        return ret;
    }
    ESP_LOGI(TAG, "esp_wifi_set_config(): SSID configurado via menuconfig");

    s_initialized = true;
    return ESP_OK;
}

esp_err_t wifi_adapter_start(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "wifi_adapter_start(): adapter nao inicializado");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_started) {
        ESP_LOGI(TAG, "wifi_adapter_start(): driver Wi-Fi ja iniciado");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "esp_wifi_start(): iniciando driver Wi-Fi STA");
    esp_err_t ret = esp_wifi_start();
    if (ret != ESP_OK) {
        return ret;
    }

    s_started = true;
    return ESP_OK;
}
