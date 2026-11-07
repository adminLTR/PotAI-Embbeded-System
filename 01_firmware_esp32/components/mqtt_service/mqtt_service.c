#include "mqtt_service.h"
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

static const char *TAG = "mqtt_service";
static mqtt_service_config_t s_config;
static mqtt_service_state_t s_state = MQTT_SERVICE_OFFLINE;
static bool s_initialized = false;
static bool s_started = false;
static bool s_command_subscribed = false;
static int s_subscribe_msg_id = -1;
static char s_command_topic[96];
static portMUX_TYPE s_state_lock = portMUX_INITIALIZER_UNLOCKED;

static bool valid_thing_name(const char *name)
{
    if (name == NULL || name[0] == '\0') return false;
    size_t len = strlen(name); if (len > 32) return false;
    for (size_t i=0;i<len;i++) {
        unsigned char c=(unsigned char)name[i];
        if (!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'||c=='_')) return false;
    }
    return true;
}

bool mqtt_service_endpoint_is_valid(const char *endpoint)
{
    if (endpoint == NULL || endpoint[0] == '\0') return false;
    if (strstr(endpoint, "://") != NULL || strchr(endpoint, '/') != NULL || strchr(endpoint, ':') != NULL) return false;
    for (const unsigned char *p=(const unsigned char*)endpoint; *p; ++p) if (isspace(*p)) return false;
    return strchr(endpoint, '.') != NULL;
}

const char *mqtt_service_state_to_string(mqtt_service_state_t state)
{
    switch(state){case MQTT_SERVICE_OFFLINE:return "MQTT_OFFLINE";case MQTT_SERVICE_CONNECTING:return "MQTT_CONNECTING";case MQTT_SERVICE_ONLINE:return "MQTT_ONLINE";default:return "MQTT_UNKNOWN";}
}
static void set_state(mqtt_service_state_t state)
{
    bool changed; portENTER_CRITICAL(&s_state_lock); changed=s_state!=state; s_state=state; portEXIT_CRITICAL(&s_state_lock);
    if(changed) ESP_LOGI(TAG,"state=%s",mqtt_service_state_to_string(state));
}
static bool is_initialized(void){bool v;portENTER_CRITICAL(&s_state_lock);v=s_initialized;portEXIT_CRITICAL(&s_state_lock);return v;}
bool mqtt_service_is_started(void){bool v;portENTER_CRITICAL(&s_state_lock);v=s_started;portEXIT_CRITICAL(&s_state_lock);return v;}
bool mqtt_service_is_command_subscribed(void){bool v;portENTER_CRITICAL(&s_state_lock);v=s_command_subscribed;portEXIT_CRITICAL(&s_state_lock);return v;}
mqtt_service_state_t mqtt_service_get_state(void){mqtt_service_state_t v;portENTER_CRITICAL(&s_state_lock);v=s_state;portEXIT_CRITICAL(&s_state_lock);return v;}

esp_err_t mqtt_service_build_topic(const char *thing_name,const char *kind,char *buffer,size_t buffer_len)
{
    if(!valid_thing_name(thing_name)||kind==NULL||kind[0]=='\0'||buffer==NULL||buffer_len==0)return ESP_ERR_INVALID_ARG;
    int n=snprintf(buffer,buffer_len,"psi3542/%s/%s",thing_name,kind);
    return (n<0||(size_t)n>=buffer_len)?ESP_ERR_NO_MEM:ESP_OK;
}

esp_err_t mqtt_service_init(const mqtt_service_config_t *config)
{
    if(config==NULL||!valid_thing_name(config->thing_name)||config->command_cb==NULL)return ESP_ERR_INVALID_ARG;
    s_config=*config;
    ESP_RETURN_ON_ERROR(mqtt_service_build_topic(config->thing_name,"command",s_command_topic,sizeof(s_command_topic)),TAG,"command topic");
    portENTER_CRITICAL(&s_state_lock);s_initialized=true;s_started=false;s_command_subscribed=false;s_subscribe_msg_id=-1;portEXIT_CRITICAL(&s_state_lock);
    set_state(MQTT_SERVICE_OFFLINE); return ESP_OK;
}

void mqtt_service_handle_adapter_event(const mqtt_adapter_event_data_t *event, void *user_ctx)
{
    (void)user_ctx; if(!is_initialized()||event==NULL)return;
    switch(event->type){
    case MQTT_ADAPTER_EVENT_BEFORE_CONNECT:set_state(MQTT_SERVICE_CONNECTING);break;
    case MQTT_ADAPTER_EVENT_CONNECTED:{
        set_state(MQTT_SERVICE_ONLINE);
        portENTER_CRITICAL(&s_state_lock);s_command_subscribed=false;s_subscribe_msg_id=-1;portEXIT_CRITICAL(&s_state_lock);
        int msg_id=-1; esp_err_t err=mqtt_adapter_subscribe(s_command_topic,s_config.command_qos,&msg_id);
        if(err==ESP_OK){portENTER_CRITICAL(&s_state_lock);s_subscribe_msg_id=msg_id;portEXIT_CRITICAL(&s_state_lock);ESP_LOGI(TAG,"command subscribe requested topic=%s msg_id=%d",s_command_topic,msg_id);}else ESP_LOGE(TAG,"command subscribe request failed: %s",esp_err_to_name(err));
        break;}
    case MQTT_ADAPTER_EVENT_SUBSCRIBED:{
        int expected;portENTER_CRITICAL(&s_state_lock);expected=s_subscribe_msg_id;if(event->msg_id==expected&&expected>=0)s_command_subscribed=true;portEXIT_CRITICAL(&s_state_lock);
        if(event->msg_id==expected&&expected>=0)ESP_LOGI(TAG,"[MQTT] COMMAND_SUBSCRIBED topic=%s msg_id=%d",s_command_topic,event->msg_id);
        break;}
    case MQTT_ADAPTER_EVENT_DATA:{
        cloud_command_t command;
        esp_err_t err=cloud_command_decode(s_command_topic,event->topic,event->topic_len,event->payload,event->payload_len,&command);
        if(err==ESP_OK){err=s_config.command_cb(&command,s_config.command_ctx);if(err!=ESP_OK)ESP_LOGW(TAG,"cloud command callback rejected: %s",esp_err_to_name(err));}
        else if(err!=ESP_ERR_NOT_FOUND)ESP_LOGW(TAG,"cloud command rejected: %s",esp_err_to_name(err));
        break;}
    case MQTT_ADAPTER_EVENT_DISCONNECTED:
    case MQTT_ADAPTER_EVENT_ERROR:
        portENTER_CRITICAL(&s_state_lock);s_command_subscribed=false;portEXIT_CRITICAL(&s_state_lock);set_state(MQTT_SERVICE_OFFLINE);break;
    case MQTT_ADAPTER_EVENT_PUBLISHED:ESP_LOGI(TAG,"broker acknowledged publish when applicable");break;
    default:break;
    }
}

esp_err_t mqtt_service_start_aws(const char *endpoint)
{
    if(!is_initialized())return ESP_ERR_INVALID_STATE;
    if(!mqtt_service_endpoint_is_valid(endpoint)){ESP_LOGE(TAG,"invalid AWS IoT endpoint: hostname only expected");return ESP_ERR_INVALID_ARG;}
    if(mqtt_service_is_started())return ESP_OK;
    char uri[192];int n=snprintf(uri,sizeof(uri),"mqtts://%s:8883",endpoint);if(n<0||(size_t)n>=sizeof(uri))return ESP_ERR_NO_MEM;
    mqtt_adapter_config_t cfg={.uri=uri,.client_id=s_config.thing_name};
    esp_err_t err=mqtt_adapter_start(&cfg,mqtt_service_handle_adapter_event,NULL);
    if(err==ESP_OK){portENTER_CRITICAL(&s_state_lock);s_started=true;portEXIT_CRITICAL(&s_state_lock);}else set_state(MQTT_SERVICE_OFFLINE);
    return err;
}

esp_err_t mqtt_service_publish_telemetry(const telemetry_data_t *data)
{
    if(!is_initialized()||mqtt_service_get_state()!=MQTT_SERVICE_ONLINE)return ESP_ERR_INVALID_STATE;
    if(data==NULL||data->device_id==NULL||strcmp(data->device_id,s_config.thing_name)!=0)return ESP_ERR_INVALID_ARG;
    char topic[96],payload[224];ESP_RETURN_ON_ERROR(mqtt_service_build_topic(s_config.thing_name,"telemetry",topic,sizeof(topic)),TAG,"topic");ESP_RETURN_ON_ERROR(telemetry_encode_json(data,payload,sizeof(payload)),TAG,"payload");
    ESP_LOGI(TAG,"[MQTT] publish telemetry seq=%lu topic=%s",(unsigned long)data->seq,topic);
    return mqtt_adapter_publish(topic,payload,s_config.telemetry_qos);
}

esp_err_t mqtt_service_publish_event(const char *event_name)
{
    if(!is_initialized()||mqtt_service_get_state()!=MQTT_SERVICE_ONLINE)return ESP_ERR_INVALID_STATE;
    if(event_name==NULL)return ESP_ERR_INVALID_ARG;
    char topic[96],payload[128];ESP_RETURN_ON_ERROR(mqtt_service_build_topic(s_config.thing_name,"event",topic,sizeof(topic)),TAG,"topic");
    int n=snprintf(payload,sizeof(payload),"{\"device_id\":\"%s\",\"event\":\"%s\"}",s_config.thing_name,event_name);if(n<0||(size_t)n>=sizeof(payload))return ESP_ERR_NO_MEM;
    return mqtt_adapter_publish(topic,payload,s_config.event_qos);
}

esp_err_t mqtt_service_publish_health(const health_data_t *data)
{
    if(!is_initialized()||mqtt_service_get_state()!=MQTT_SERVICE_ONLINE)return ESP_ERR_INVALID_STATE;
    if(data==NULL||data->device_id==NULL||strcmp(data->device_id,s_config.thing_name)!=0)return ESP_ERR_INVALID_ARG;
    char topic[96],payload[512];
    ESP_RETURN_ON_ERROR(mqtt_service_build_topic(s_config.thing_name,"health",topic,sizeof(topic)),TAG,"topic");
    ESP_RETURN_ON_ERROR(health_encode_json(data,payload,sizeof(payload)),TAG,"health payload");
    ESP_LOGI(TAG,"[MQTT] publish health topic=%s sensor_errors=%lu publish_drops=%lu",topic,
             (unsigned long)data->sensor_error_count,(unsigned long)data->publish_drop_count);
    return mqtt_adapter_publish(topic,payload,s_config.health_qos);
}

