#include "connectivity.h"

#include "esp_log.h"
#include "wifi_adapter.h"

static const char *TAG = "CONNECTIVITY";

static connectivity_fsm_t s_fsm;

static void apply(connectivity_event_t event)
{
    connectivity_state_t before = connectivity_fsm_state(&s_fsm);
    connectivity_state_t after = connectivity_fsm_handle(&s_fsm, event);

    if (before != after) {
        ESP_LOGI(
            TAG,
            "%s -> %s por %s",
            connectivity_state_name(before),
            connectivity_state_name(after),
            connectivity_event_name(event));
    } else {
        ESP_LOGI(
            TAG,
            "estado permanece %s apos %s",
            connectivity_state_name(after),
            connectivity_event_name(event));
    }
}

static void adapter_event_callback(wifi_adapter_event_t event, void *ctx)
{
    (void)ctx;

    switch (event) {
    case WIFI_ADAPTER_EVENT_ASSOCIATED:
        apply(CONNECTIVITY_EVENT_ASSOCIATED);
        break;

    case WIFI_ADAPTER_EVENT_IP_READY:
        apply(CONNECTIVITY_EVENT_IP_READY);
        break;

    case WIFI_ADAPTER_EVENT_DISCONNECTED:
        apply(CONNECTIVITY_EVENT_DISCONNECTED);
        break;

    default:
        break;
    }
}

void connectivity_init(void)
{
    connectivity_fsm_init(&s_fsm);
    wifi_adapter_set_callback(adapter_event_callback, 0);
}

esp_err_t connectivity_start(void)
{
    /*
     * Primeiro prepara-se a infraestrutura do adapter. Essa fase nao deve
     * produzir ASSOCIATED nem IP_READY: ela inicializa NVS, esp_netif, Event
     * Loop, driver Wi-Fi, modo STA, handlers e configuracao SSID/senha.
     */
    esp_err_t ret = wifi_adapter_init();

    if (ret != ESP_OK) {
        ESP_LOGW(
            TAG,
            "wifi_adapter_init() falhou: %s; permanecendo em %s",
            esp_err_to_name(ret),
            connectivity_state_name(connectivity_fsm_state(&s_fsm)));
        return ret;
    }

    /*
     * A FSM entra em CONNECTING antes de iniciar a STA. esp_wifi_start() pode
     * causar a entrega de WIFI_EVENT_STA_START; a partir desse evento o adapter
     * chama esp_wifi_connect(), e outros eventos Wi-Fi/IP podem chegar de forma
     * assíncrona. Aplicar START antes de wifi_adapter_start() evita perder um
     * ASSOCIATED entregue rapidamente.
     */
    apply(CONNECTIVITY_EVENT_START);

    ret = wifi_adapter_start();

    if (ret != ESP_OK) {
        apply(CONNECTIVITY_EVENT_DISCONNECTED);
        ESP_LOGW(
            TAG,
            "wifi_adapter_start() falhou: %s; retorno a %s",
            esp_err_to_name(ret),
            connectivity_state_name(connectivity_fsm_state(&s_fsm)));
    }

    return ret;
}

connectivity_state_t connectivity_get_state(void)
{
    return connectivity_fsm_state(&s_fsm);
}
