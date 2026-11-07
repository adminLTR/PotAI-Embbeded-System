#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WIFI_ADAPTER_EVENT_ASSOCIATED = 0,
    WIFI_ADAPTER_EVENT_IP_READY = 1,
    WIFI_ADAPTER_EVENT_DISCONNECTED = 2,
} wifi_adapter_event_t;

typedef void (*wifi_adapter_event_cb_t)(
    wifi_adapter_event_t event,
    void *ctx);

void wifi_adapter_set_callback(
    wifi_adapter_event_cb_t cb,
    void *ctx);

/*
 * Prepara a infraestrutura Wi-Fi STA: NVS, esp_netif, ESP Event Loop,
 * interface Wi-Fi STA, driver, handlers, modo STA e configuracao SSID/senha.
 * Nao inicia a STA nem solicita associacao ao AP.
 */
esp_err_t wifi_adapter_init(void);

/*
 * Inicia o driver Wi-Fi depois de wifi_adapter_init(). No percurso real
 * do ESP-IDF, WIFI_EVENT_STA_START e entregue posteriormente pelo Event Loop;
 * o adapter solicita esp_wifi_connect() a partir desse evento.
 */
esp_err_t wifi_adapter_start(void);

#ifdef __cplusplus
}
#endif
