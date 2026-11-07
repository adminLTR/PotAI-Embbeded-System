#include "http_service.h"

#include "api_logic.h"
#include "connectivity.h"
#include "device_state.h"
#include "esp_http_server.h"
#include "esp_log.h"

static const char *TAG = "HTTP_SERVICE";
static httpd_handle_t s_server = NULL;

static esp_err_t send_json(httpd_req_t *req, const char *json, int status_code)
{
    httpd_resp_set_type(req, "application/json");

    switch (status_code) {
    case 404:
        httpd_resp_set_status(req, "404 Not Found");
        break;
    case 500:
        httpd_resp_set_status(req, "500 Internal Server Error");
        break;
    case 503:
        httpd_resp_set_status(req, "503 Service Unavailable");
        break;
    default:
        /* 200 OK e outros casos-base usam o status padrao do servidor. */
        break;
    }

    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t health_handler(httpd_req_t *req)
{
    char json[64];
    api_status_t st = api_build_health_response(json, sizeof(json));

    if (st != API_STATUS_OK) {
        return send_json(req, "{\"error\":\"buffer\"}", 500);
    }
    return send_json(req, json, 200);
}

static esp_err_t status_handler(httpd_req_t *req)
{
    char json[320];
    device_snapshot_t snap;
    device_state_copy(&snap);

    api_status_t st = api_build_status_response(
        &snap,
        connectivity_get_state(),
        json,
        sizeof(json));

    if (st != API_STATUS_OK) {
        return send_json(req, "{\"error\":\"status_unavailable\"}", 500);
    }
    return send_json(req, json, 200);
}

static esp_err_t telemetry_handler(httpd_req_t *req)
{
    char json[320];
    device_snapshot_t snap;
    device_state_copy(&snap);

    api_status_t st = api_build_telemetry_response(&snap, json, sizeof(json));

    if (st == API_STATUS_OK) {
        return send_json(req, json, 200);
    }
    if (st == API_STATUS_UNAVAILABLE) {
        /* Nao existe ainda uma representacao corrente de telemetria. */
        return send_json(req, json, 404);
    }
    return send_json(req, "{\"error\":\"buffer\"}", 500);
}

static esp_err_t not_found_handler(httpd_req_t *req, httpd_err_code_t err)
{
    (void)err;
    return send_json(req, "{\"error\":\"not_found\"}", 404);
}

static esp_err_t stop_after_registration_error(esp_err_t error, const char *what)
{
    ESP_LOGE(TAG, "%s falhou: %s", what, esp_err_to_name(error));

    if (s_server != NULL) {
        httpd_handle_t tmp = s_server;
        s_server = NULL;
        esp_err_t stop_ret = httpd_stop(tmp);
        if (stop_ret != ESP_OK) {
            ESP_LOGW(TAG, "httpd_stop apos falha de registro: %s", esp_err_to_name(stop_ret));
        }
    }

    return error;
}

esp_err_t http_service_start(void)
{
    if (s_server != NULL) {
        return ESP_OK;
    }

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = CONFIG_A10_HTTP_PORT;
    cfg.core_id = 0;

    esp_err_t ret = httpd_start(&s_server, &cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start falhou: %s", esp_err_to_name(ret));
        s_server = NULL;
        return ret;
    }

    const httpd_uri_t health_uri = {
        .uri = "/health",
        .method = HTTP_GET,
        .handler = health_handler,
        .user_ctx = NULL,
    };
    const httpd_uri_t status_uri = {
        .uri = "/api/v1/status",
        .method = HTTP_GET,
        .handler = status_handler,
        .user_ctx = NULL,
    };
    const httpd_uri_t telemetry_uri = {
        .uri = "/api/v1/telemetry/latest",
        .method = HTTP_GET,
        .handler = telemetry_handler,
        .user_ctx = NULL,
    };

    ret = httpd_register_uri_handler(s_server, &health_uri);
    if (ret != ESP_OK) {
        return stop_after_registration_error(ret, "registro de /health");
    }

    ret = httpd_register_uri_handler(s_server, &status_uri);
    if (ret != ESP_OK) {
        return stop_after_registration_error(ret, "registro de /api/v1/status");
    }

    ret = httpd_register_uri_handler(s_server, &telemetry_uri);
    if (ret != ESP_OK) {
        return stop_after_registration_error(ret, "registro de /api/v1/telemetry/latest");
    }

    ret = httpd_register_err_handler(
        s_server,
        HTTPD_404_NOT_FOUND,
        not_found_handler);
    if (ret != ESP_OK) {
        return stop_after_registration_error(ret, "registro do handler 404");
    }

    ESP_LOGI(TAG, "HTTP server iniciado na porta %d", CONFIG_A10_HTTP_PORT);
    return ESP_OK;
}

esp_err_t http_service_stop(void)
{
    if (s_server == NULL) {
        return ESP_OK;
    }

    httpd_handle_t tmp = s_server;
    s_server = NULL;
    return httpd_stop(tmp);
}

bool http_service_is_started(void)
{
    return s_server != NULL;
}
