#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "aws_iot_credentials.h"
#include "cloud_command.h"
#include "connectivity.h"
#include "device_state.h"
#include "dht11.h"
#include "buzzer.h"
#include "display_ui.h"
#include "irrigation.h"
#include "ml_model.h"
#include "pump.h"
#include "soil_moisture.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "event_logic.h"
#include "health.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/portmacro.h"
#include "http_service.h"
#include "mqtt_service.h"
#include "telemetry.h"
#include "time_sync.h"

static const char *TAG = "A10_APP";
typedef enum { PUBLISH_ITEM_TELEMETRY=0, PUBLISH_ITEM_EVENT } publish_item_type_t;
typedef struct { publish_item_type_t type; telemetry_data_t telemetry; char event_name[32]; } publish_item_t;
static QueueHandle_t s_publish_queue=NULL;
static QueueHandle_t s_command_queue=NULL;
static QueueHandle_t s_irrigation_queue=NULL; /* riegos concluidos pendientes de enviar a AWS */
static bool s_mqtt_enabled=false;
static TaskHandle_t s_event_task_handle=NULL;
static volatile uint32_t s_raw_isr_count;
static uint32_t s_telemetry_seq;
static system_state_t s_local_state;
static portMUX_TYPE s_health_lock=portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_sensor_error_count;
static uint32_t s_publish_drop_count;
static uint32_t s_command_drop_count;
static bool s_last_sensor_ok;
static bool s_sensor_fault_active;
static uint64_t s_last_sensor_ok_ms;
static esp_reset_reason_t s_boot_reset_reason;

static bool gpio_is_reserved_for_flash(int gpio){return gpio>=6&&gpio<=11;}
static uint64_t now_ms(void){return (uint64_t)(esp_timer_get_time()/1000ULL);}

static const char *reset_reason_name(esp_reset_reason_t reason)
{
    switch(reason){
    case ESP_RST_POWERON:return "POWERON";
    case ESP_RST_EXT:return "EXT";
    case ESP_RST_SW:return "SW";
    case ESP_RST_PANIC:return "PANIC";
    case ESP_RST_INT_WDT:return "INT_WDT";
    case ESP_RST_TASK_WDT:return "TASK_WDT";
    case ESP_RST_WDT:return "WDT";
    case ESP_RST_DEEPSLEEP:return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:return "BROWNOUT";
    default:return "OTHER";
    }
}

static void health_record_sensor_result(bool ok,uint64_t t_ms)
{
    portENTER_CRITICAL(&s_health_lock);
    s_last_sensor_ok=ok;
    if(ok)s_last_sensor_ok_ms=t_ms;else s_sensor_error_count++;
    portEXIT_CRITICAL(&s_health_lock);
}

static void health_record_publish_drop(void)
{
    portENTER_CRITICAL(&s_health_lock);s_publish_drop_count++;portEXIT_CRITICAL(&s_health_lock);
}

static void health_record_command_drop(void)
{
    portENTER_CRITICAL(&s_health_lock);s_command_drop_count++;portEXIT_CRITICAL(&s_health_lock);
}

static void build_health_snapshot(health_data_t *out)
{
    if(out==NULL)return;
    uint32_t sensor_errors,publish_drops,command_drops;bool sensor_ok;uint64_t last_sensor_ok;
    portENTER_CRITICAL(&s_health_lock);
    sensor_errors=s_sensor_error_count;publish_drops=s_publish_drop_count;command_drops=s_command_drop_count;
    sensor_ok=s_last_sensor_ok;last_sensor_ok=s_last_sensor_ok_ms;
    portEXIT_CRITICAL(&s_health_lock);
    *out=(health_data_t){
        .device_id=CONFIG_A10_THING_NAME,
        .firmware_version="a10-v07",
        .uptime_ms=now_ms(),
        .connectivity=connectivity_state_name(connectivity_get_state()),
        .time_synced=time_sync_is_ready(),
        .mqtt_connected=mqtt_service_get_state()==MQTT_SERVICE_ONLINE,
        .command_subscribed=mqtt_service_is_command_subscribed(),
        .sensor_ok=sensor_ok,
        .last_sensor_ok_ms=last_sensor_ok,
        .sensor_error_count=sensor_errors,
        .publish_drop_count=publish_drops,
        .command_drop_count=command_drops,
        .free_heap_bytes=(uint32_t)esp_get_free_heap_size(),
        .reset_reason=reset_reason_name(s_boot_reset_reason),
    };
}

typedef struct { int gpio; bool needs_output; const char *name; } pin_use_t;

static void validate_gpio_configuration(void)
{
    /* GPIO34..39 existem na ESP32 mas sao somente entrada: so servem para sensores. */
    const pin_use_t pins[]={
        {CONFIG_A10_DHT11_GPIO,true,"DHT11"},{CONFIG_A10_DOOR_GPIO,false,"DOOR"},{CONFIG_A10_LED_GPIO,true,"LED"},
        {CONFIG_A10_SOIL_ADC_GPIO,false,"SOIL_ADC"},{CONFIG_A10_PUMP_RELAY_GPIO,true,"PUMP_RELAY"},{CONFIG_A10_BUZZER_GPIO,true,"BUZZER"},
#if CONFIG_A10_LCD_ENABLED
        {CONFIG_A10_LCD_I2C_SDA_GPIO,true,"LCD_SDA"},{CONFIG_A10_LCD_I2C_SCL_GPIO,true,"LCD_SCL"},{CONFIG_A10_UI_BUTTON_GPIO,false,"UI_BUTTON"},
#endif
    };
    const int n=(int)(sizeof(pins)/sizeof(pins[0]));
    for(int i=0;i<n;i++)if(pins[i].gpio<0||pins[i].gpio>39||gpio_is_reserved_for_flash(pins[i].gpio)||(pins[i].needs_output&&pins[i].gpio>33)){ESP_LOGE(TAG,"GPIO invalido para %s: %d",pins[i].name,pins[i].gpio);ESP_ERROR_CHECK(ESP_ERR_INVALID_ARG);}
    for(int i=0;i<n;i++)for(int j=i+1;j<n;j++)if(pins[i].gpio==pins[j].gpio){ESP_LOGE(TAG,"GPIOs devem ser distintos: %s e %s usam GPIO%d",pins[i].name,pins[j].name,pins[i].gpio);ESP_ERROR_CHECK(ESP_ERR_INVALID_ARG);}
}

static void enqueue_publish_item(const publish_item_t *item)
{
    if(s_publish_queue==NULL||item==NULL)return;
    if(xQueueSend(s_publish_queue,item,0)!=pdTRUE){health_record_publish_drop();ESP_LOGW(TAG,"publish_queue full: drop_new policy applied");}
}

static esp_err_t enqueue_cloud_command(const cloud_command_t *command, void *ctx)
{
    (void)ctx;if(command==NULL||s_command_queue==NULL)return ESP_ERR_INVALID_STATE;
    if(xQueueSend(s_command_queue,command,0)!=pdTRUE){health_record_command_drop();ESP_LOGW(TAG,"command_queue full: drop_new policy applied");return ESP_ERR_NO_MEM;}
    return ESP_OK;
}

static void gpio_event_isr(void *arg)
{
    (void)arg;s_raw_isr_count++;if(s_event_task_handle==NULL)return;BaseType_t hp=pdFALSE;vTaskNotifyGiveFromISR(s_event_task_handle,&hp);if(hp==pdTRUE)portYIELD_FROM_ISR();
}
static bool read_door_open_level(void){return gpio_get_level((gpio_num_t)CONFIG_A10_DOOR_GPIO)==0;}

static void configure_local_gpio(void)
{
    gpio_config_t led={.pin_bit_mask=1ULL<<CONFIG_A10_LED_GPIO,.mode=GPIO_MODE_OUTPUT,.pull_up_en=GPIO_PULLUP_DISABLE,.pull_down_en=GPIO_PULLDOWN_DISABLE,.intr_type=GPIO_INTR_DISABLE};
    ESP_ERROR_CHECK(gpio_config(&led));ESP_ERROR_CHECK(gpio_set_level((gpio_num_t)CONFIG_A10_LED_GPIO,0));
    gpio_config_t door={.pin_bit_mask=1ULL<<CONFIG_A10_DOOR_GPIO,.mode=GPIO_MODE_INPUT,.pull_up_en=GPIO_PULLUP_ENABLE,.pull_down_en=GPIO_PULLDOWN_DISABLE,.intr_type=GPIO_INTR_ANYEDGE};
    ESP_ERROR_CHECK(gpio_config(&door));
}
static void install_local_gpio_isr(void){ESP_ERROR_CHECK(gpio_install_isr_service(0));ESP_ERROR_CHECK(gpio_isr_handler_add((gpio_num_t)CONFIG_A10_DOOR_GPIO,gpio_event_isr,NULL));}

static void sensor_task(void *arg)
{
    (void)arg;vTaskDelay(pdMS_TO_TICKS(4000));TickType_t last=xTaskGetTickCount();
#if CONFIG_A10_FORCE_SENSOR_UNAVAILABLE_READS > 0
    uint32_t forced_unavailable_count=0;
#endif
    while(1){
        bool valid=false;float temperature_c=0.0f,humidity_pct=0.0f;uint64_t sample_time=now_ms();
#if CONFIG_A10_FORCE_SENSOR_UNAVAILABLE_READS > 0
        if(forced_unavailable_count<(uint32_t)CONFIG_A10_FORCE_SENSOR_UNAVAILABLE_READS){forced_unavailable_count++;ESP_LOGW(TAG,"[EXT][DHT11] indisponibilidade forcada attempt=%lu/%d",(unsigned long)forced_unavailable_count,CONFIG_A10_FORCE_SENSOR_UNAVAILABLE_READS);}else
#endif
        {
            dht11_reading_t r={0};esp_err_t ret=dht11_read((gpio_num_t)CONFIG_A10_DHT11_GPIO,&r);valid=ret==ESP_OK;
            if(valid){temperature_c=(float)r.temperature_c;humidity_pct=(float)r.humidity_pct;ESP_LOGI(TAG,"[LOCAL][DHT11] temperature_c=%d humidity_pct=%d",r.temperature_c,r.humidity_pct);}else ESP_LOGW(TAG,"[LOCAL][DHT11] leitura falhou: %s",esp_err_to_name(ret));
        }
        device_state_update_telemetry(temperature_c,humidity_pct,valid,sample_time);
        {float soil_pct=0.0f;int soil_raw=0;esp_err_t sret=soil_moisture_read(&soil_pct,&soil_raw);bool soil_ok=sret==ESP_OK;
            if(soil_ok)ESP_LOGI(TAG,"[LOCAL][SOIL] raw=%d soil_moisture_pct=%.1f",soil_raw,(double)soil_pct);else ESP_LOGW(TAG,"[LOCAL][SOIL] leitura falhou: %s raw=%d",esp_err_to_name(sret),soil_raw);
            device_state_update_soil(soil_pct,soil_raw,soil_ok,sample_time);}
        health_record_sensor_result(valid,sample_time);
        if(!valid&&!s_sensor_fault_active){s_sensor_fault_active=true;publish_item_t ev={.type=PUBLISH_ITEM_EVENT};strlcpy(ev.event_name,"sensor_unavailable",sizeof(ev.event_name));enqueue_publish_item(&ev);}
        else if(valid&&s_sensor_fault_active){s_sensor_fault_active=false;publish_item_t ev={.type=PUBLISH_ITEM_EVENT};strlcpy(ev.event_name,"sensor_recovered",sizeof(ev.event_name));enqueue_publish_item(&ev);}
        if(valid){device_snapshot_t snap;device_state_copy(&snap);publish_item_t item={.type=PUBLISH_ITEM_TELEMETRY,.telemetry={.device_id=CONFIG_A10_THING_NAME,.seq=++s_telemetry_seq,.temperature_c=snap.temperature_c,.humidity_pct=snap.humidity_pct,.valid=snap.sample_valid,.sample_uptime_ms=snap.sample_uptime_ms,
            .soil_moisture_pct=snap.soil_moisture_pct,.soil_valid=snap.soil_valid,.plant_id=CONFIG_A10_PLANT_ID,.ml_predicted=snap.last_ml_predicted,.pump_on=snap.pump_on,.irrigation_count=snap.irrigation_count}};enqueue_publish_item(&item);}
        vTaskDelayUntil(&last,pdMS_TO_TICKS(CONFIG_A10_SENSOR_PERIOD_MS));
    }
}

static void event_task(void *arg)
{
    (void)arg;while(1){uint32_t notify_count=ulTaskNotifyTake(pdTRUE,portMAX_DELAY);while(1){uint32_t extra=ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(CONFIG_A10_DOOR_DEBOUNCE_MS));if(extra==0)break;notify_count+=extra;}
        bool open=read_door_open_level();uint32_t raw=s_raw_isr_count;uint64_t t=now_ms();result_t res=handle_door_level(&s_local_state,open,t);
        device_snapshot_t before;device_state_copy(&before);bool led_on=before.led_on;
        if(res==RESULT_OPEN_ACCEPTED||res==RESULT_CLOSE_ACCEPTED){led_on=s_local_state.door_state==DOOR_OPEN;ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_set_level((gpio_num_t)CONFIG_A10_LED_GPIO,led_on?1:0));}
        device_state_update_local(open,led_on,s_local_state.open_event_count,raw,t);
        ESP_LOGI(TAG,"[LOCAL][EVENT] notify=%lu raw_isr=%lu debounce_ms=%d level_open=%d result=%s state=%s open_event_count=%lu",(unsigned long)notify_count,(unsigned long)raw,CONFIG_A10_DOOR_DEBOUNCE_MS,open?1:0,result_name(res),door_state_name(s_local_state.door_state),(unsigned long)s_local_state.open_event_count);
        if(res==RESULT_OPEN_ACCEPTED||res==RESULT_CLOSE_ACCEPTED){publish_item_t item={.type=PUBLISH_ITEM_EVENT};strlcpy(item.event_name,res==RESULT_OPEN_ACCEPTED?"door_open":"door_closed",sizeof(item.event_name));enqueue_publish_item(&item);}
    }
}

static void command_task(void *arg)
{
    (void)arg;cloud_command_t command;while(1){if(xQueueReceive(s_command_queue,&command,portMAX_DELAY)!=pdTRUE)continue;if(command.type==CLOUD_COMMAND_LED_SET){bool on=command.data.led_set.on;ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_set_level((gpio_num_t)CONFIG_A10_LED_GPIO,on?1:0));device_state_update_led(on,now_ms());ESP_LOGI(TAG,"[CLOUD][COMMAND] led=%s",on?"ON":"OFF");}}
}

static void http_manager_task(void *arg)
{
    (void)arg;bool announced=false;while(1){connectivity_state_t state=connectivity_get_state();if(state==CONNECTIVITY_ONLINE&&!http_service_is_started()){esp_err_t ret=http_service_start();ESP_LOGI(TAG,"[HTTP] http_service_start=%s",esp_err_to_name(ret));announced=false;}else if(state!=CONNECTIVITY_ONLINE&&http_service_is_started()){ESP_LOGW(TAG,"[HTTP] conectividade saiu de ONLINE; parando servico HTTP");http_service_stop();}else if(!announced&&strlen(CONFIG_A10_WIFI_SSID)==0){ESP_LOGW(TAG,"[HTTP] SSID vazio: API HTTP nao sera iniciada; baseline local ativo");announced=true;}vTaskDelay(pdMS_TO_TICKS(500));}
}

static void mqtt_task(void *arg)
{
    (void)arg;publish_item_t item;TickType_t last_health_tick=xTaskGetTickCount();
    while(1){connectivity_state_t cstate=connectivity_get_state();time_sync_state_t tstate=time_sync_get_state();
        if(cstate==CONNECTIVITY_ONLINE&&(tstate==TIME_SYNC_NOT_SYNCED||tstate==TIME_SYNC_ERROR)){esp_err_t ret=time_sync_start(CONFIG_A10_SNTP_SERVER);if(ret!=ESP_OK)ESP_LOGW(TAG,"[TIME] time_sync_start=%s",esp_err_to_name(ret));}
        if(cstate==CONNECTIVITY_ONLINE&&time_sync_is_ready()&&!mqtt_service_is_started()&&s_mqtt_enabled){esp_err_t ret=mqtt_service_start_aws(CONFIG_A10_AWS_IOT_ENDPOINT);ESP_LOGI(TAG,"[AWS_IOT] mqtt_service_start=%s",esp_err_to_name(ret));}
        if(mqtt_service_get_state()!=MQTT_SERVICE_ONLINE){last_health_tick=xTaskGetTickCount();vTaskDelay(pdMS_TO_TICKS(500));continue;}
        if(CONFIG_A10_HEALTH_ENABLED&&((xTaskGetTickCount()-last_health_tick)>=pdMS_TO_TICKS(CONFIG_A10_HEALTH_PERIOD_MS))){
            health_data_t health;build_health_snapshot(&health);esp_err_t hret=mqtt_service_publish_health(&health);
            if(hret!=ESP_OK)ESP_LOGW(TAG,"health publish failed: %s",esp_err_to_name(hret));
            last_health_tick=xTaskGetTickCount();
        }
        irrigation_report_data_t irrigation_report;
        if(s_irrigation_queue!=NULL&&xQueueReceive(s_irrigation_queue,&irrigation_report,0)==pdTRUE){esp_err_t iret=mqtt_service_publish_irrigation(&irrigation_report);if(iret!=ESP_OK)ESP_LOGW(TAG,"irrigation publish failed: %s",esp_err_to_name(iret));}
        if(xQueueReceive(s_publish_queue,&item,pdMS_TO_TICKS(500))==pdTRUE){esp_err_t ret=item.type==PUBLISH_ITEM_TELEMETRY?mqtt_service_publish_telemetry(&item.telemetry):mqtt_service_publish_event(item.event_name);if(ret!=ESP_OK)ESP_LOGW(TAG,"publish failed after dequeue; item is not requeued: %s",esp_err_to_name(ret));}
    }
}

#ifdef CONFIG_A10_PUMP_RELAY_ACTIVE_LOW
#define A10_PUMP_ACTIVE_LOW true
#else
#define A10_PUMP_ACTIVE_LOW false
#endif
#ifdef CONFIG_A10_BUZZER_ACTIVE_LOW
#define A10_BUZZER_ACTIVE_LOW true
#else
#define A10_BUZZER_ACTIVE_LOW false
#endif
#ifdef CONFIG_A10_LCD_BACKLIGHT
#define A10_LCD_BACKLIGHT true
#else
#define A10_LCD_BACKLIGHT false
#endif

static void irrigation_event_cb(const char *event_name,void *ctx){(void)ctx;publish_item_t ev={.type=PUBLISH_ITEM_EVENT};strlcpy(ev.event_name,event_name,sizeof(ev.event_name));enqueue_publish_item(&ev);}

/* Riego concluido -> fila propia (mas prioritaria que la telemetria). Si se llena, descarta el mas antiguo. */
static void irrigation_record_cb(const irrigation_record_t *r,void *ctx)
{
    (void)ctx;if(r==NULL||s_irrigation_queue==NULL)return;
    irrigation_report_data_t rep={.device_id=CONFIG_A10_THING_NAME,.seq=r->seq,.plant_id=r->plant_id,.soil_moisture_pct=r->soil_moisture_pct,.temperature_c=r->temperature_c,
        .ml_predicted=r->ml_predicted,.ml_delivered=r->ml_delivered,.duration_ms=r->duration_ms,.pump_uptime_ms=r->uptime_ms};
    if(xQueueSend(s_irrigation_queue,&rep,0)!=pdTRUE){irrigation_report_data_t old;xQueueReceive(s_irrigation_queue,&old,0);health_record_publish_drop();xQueueSend(s_irrigation_queue,&rep,0);}
}

/* Atuadores e sensor de solo: inicializados cedo, com a bomba garantidamente desligada. */
static void init_irrigation_hardware(void)
{
    ESP_ERROR_CHECK(pump_init(CONFIG_A10_PUMP_RELAY_GPIO,A10_PUMP_ACTIVE_LOW));
    ESP_ERROR_CHECK_WITHOUT_ABORT(buzzer_init(CONFIG_A10_BUZZER_GPIO,A10_BUZZER_ACTIVE_LOW));
    soil_moisture_config_t soil={.adc_gpio=CONFIG_A10_SOIL_ADC_GPIO,.raw_dry=CONFIG_A10_SOIL_RAW_DRY,.raw_wet=CONFIG_A10_SOIL_RAW_WET,.samples=CONFIG_A10_SOIL_SAMPLES};
    esp_err_t sret=soil_moisture_init(&soil);
    if(sret!=ESP_OK)ESP_LOGE(TAG,"[SOIL] init falhou (%s): sem umidade de solo, irrigacao bloqueada",esp_err_to_name(sret));
}

/* Modelo ML (borda), LCD + botao e task de irrigacao. Falhas aqui nao derrubam o firmware. */
static void start_irrigation_and_ui(void)
{
    esp_err_t mret=ml_model_init();
    if(mret!=ESP_OK)ESP_LOGE(TAG,"[ML] modelo invalido (%s): irrigacao automatica desabilitada. Rode 03_model_ML/export_model_esp32.py",esp_err_to_name(mret));
    ESP_LOGI(TAG,"[POTAI] plant_id=%u (1=ajo .. 8=tomatecherry)",(unsigned)CONFIG_A10_PLANT_ID);
#if CONFIG_A10_LCD_ENABLED
    display_ui_config_t ui={
        .lcd={.sda_gpio=CONFIG_A10_LCD_I2C_SDA_GPIO,.scl_gpio=CONFIG_A10_LCD_I2C_SCL_GPIO,.address=(uint8_t)CONFIG_A10_LCD_I2C_ADDR,.freq_hz=CONFIG_A10_LCD_I2C_FREQ_HZ,.backlight=A10_LCD_BACKLIGHT},
        .button_gpio=CONFIG_A10_UI_BUTTON_GPIO,.button_debounce_ms=CONFIG_A10_UI_BUTTON_DEBOUNCE_MS,.refresh_ms=CONFIG_A10_UI_REFRESH_MS,.plant_id=CONFIG_A10_PLANT_ID};
    esp_err_t uret=display_ui_start(&ui);
    if(uret!=ESP_OK)ESP_LOGW(TAG,"[UI] LCD indisponivel (%s): firmware segue sem tela",esp_err_to_name(uret));
#endif
#if CONFIG_A10_IRRIGATION_ENABLED
    if(mret==ESP_OK){
        irrigation_config_t ir={.plant_id=CONFIG_A10_PLANT_ID,.check_period_ms=CONFIG_A10_IRRIGATION_CHECK_PERIOD_MS,.cooldown_s=CONFIG_A10_IRRIGATION_COOLDOWN_S,.min_ml=CONFIG_A10_IRRIGATION_MIN_ML,
            .flow_ml_per_min=CONFIG_A10_PUMP_FLOW_ML_PER_MIN,.max_on_ms=CONFIG_A10_PUMP_MAX_ON_MS,.soil_max_pct=(float)CONFIG_A10_IRRIGATION_SOIL_MAX_PCT,
            .max_sample_age_ms=3u*CONFIG_A10_SENSOR_PERIOD_MS+1000u,.beep_count=CONFIG_A10_PUMP_BEEP_COUNT,.event_cb=irrigation_event_cb,.event_ctx=NULL,.record_cb=irrigation_record_cb,.record_ctx=NULL};
        esp_err_t iret=irrigation_start(&ir);
        if(iret!=ESP_OK)ESP_LOGE(TAG,"[IRRIGATION] start falhou: %s",esp_err_to_name(iret));
    }
#else
    ESP_LOGW(TAG,"[IRRIGATION] riego automatico deshabilitado por menuconfig: bomba nunca sera acionada");
#endif
}

static void create_task_or_abort(TaskFunction_t task,const char *name,uint32_t stack,UBaseType_t prio,TaskHandle_t *handle){if(xTaskCreate(task,name,stack,NULL,prio,handle)!=pdPASS){ESP_LOGE(TAG,"falha ao criar %s",name);ESP_ERROR_CHECK(ESP_ERR_NO_MEM);}}
static void create_task_pinned_or_abort(TaskFunction_t task,const char *name,uint32_t stack,UBaseType_t prio,TaskHandle_t *handle,BaseType_t core){if(xTaskCreatePinnedToCore(task,name,stack,NULL,prio,handle,core)!=pdPASS){ESP_LOGE(TAG,"falha ao criar %s no core %ld",name,(long)core);ESP_ERROR_CHECK(ESP_ERR_NO_MEM);}}
static void start_connectivity_if_configured(void){connectivity_init();if(strlen(CONFIG_A10_WIFI_SSID)==0){ESP_LOGW(TAG,"[CONNECTIVITY] SSID vazio: baseline sem Wi-Fi");return;}esp_err_t ret=connectivity_start();ESP_LOGI(TAG,"[CONNECTIVITY] connectivity_start=%s",esp_err_to_name(ret));}

void app_main(void)
{
    ESP_LOGI(TAG,"PSI3542 Aula 10 v07 DRAFT - AWS IoT Core + MQTT/TLS + health");
    s_boot_reset_reason=esp_reset_reason();validate_gpio_configuration();system_state_init(&s_local_state);device_state_init(CONFIG_A10_THING_NAME);configure_local_gpio();init_irrigation_hardware();
    esp_err_t mqtt_init=mqtt_service_init(&(mqtt_service_config_t){.thing_name=CONFIG_A10_THING_NAME,.telemetry_qos=CONFIG_A10_TELEMETRY_QOS,.event_qos=CONFIG_A10_EVENT_QOS,.health_qos=CONFIG_A10_HEALTH_QOS,.command_qos=CONFIG_A10_COMMAND_QOS,.command_cb=enqueue_cloud_command,.command_ctx=NULL});
    bool endpoint_configured=strlen(CONFIG_A10_AWS_IOT_ENDPOINT)>0;
    bool endpoint_valid=mqtt_service_endpoint_is_valid(CONFIG_A10_AWS_IOT_ENDPOINT);
    bool credentials_ready=aws_iot_credentials_are_provisioned();
    if(mqtt_init==ESP_OK&&endpoint_valid&&credentials_ready){
        s_mqtt_enabled=true;s_publish_queue=xQueueCreate(CONFIG_A10_PUBLISH_QUEUE_LEN,sizeof(publish_item_t));s_command_queue=xQueueCreate(CONFIG_A10_COMMAND_QUEUE_LEN,sizeof(cloud_command_t));s_irrigation_queue=xQueueCreate(8,sizeof(irrigation_report_data_t));
        if(s_publish_queue==NULL||s_command_queue==NULL||s_irrigation_queue==NULL){ESP_LOGE(TAG,"falha ao criar filas MQTT/comando");ESP_ERROR_CHECK(ESP_ERR_NO_MEM);}
        ESP_LOGI(TAG,"[AWS_IOT] thing=%s endpoint=%s credentials=provisioned",CONFIG_A10_THING_NAME,CONFIG_A10_AWS_IOT_ENDPOINT);
    }else if(mqtt_init!=ESP_OK)ESP_LOGW(TAG,"mqtt_service_init falhou; MQTT/AWS desabilitado: %s",esp_err_to_name(mqtt_init));
    else if(!endpoint_configured)ESP_LOGW(TAG,"[AWS_IOT] endpoint vazio: MQTT/AWS desabilitado para este boot");
    else if(!endpoint_valid)ESP_LOGE(TAG,"[AWS_IOT] endpoint invalido: informe somente o hostname ATS; MQTT/AWS desabilitado");
    else if(!credentials_ready)ESP_LOGW(TAG,"[AWS_IOT] credenciais de dispositivo nao provisionadas: build/local/HTTP permitidos; MQTT/AWS desabilitado");

    create_task_pinned_or_abort(event_task,"event_task",4096,10,&s_event_task_handle,0);ESP_LOGI(TAG,"[TASK] event_task core=0 prio=10 debounce_ms=%d",CONFIG_A10_DOOR_DEBOUNCE_MS);install_local_gpio_isr();
    create_task_pinned_or_abort(sensor_task,"sensor_task",4096,5,NULL,1);ESP_LOGI(TAG,"[TASK] sensor_task core=1 prio=5");
    start_irrigation_and_ui();
    create_task_or_abort(http_manager_task,"http_manager_task",4096,4,NULL);
    if(s_mqtt_enabled){create_task_pinned_or_abort(mqtt_task,"mqtt_task",7168,4,NULL,0);create_task_pinned_or_abort(command_task,"command_task",4096,5,NULL,0);ESP_LOGI(TAG,"[TASK] mqtt_task core=0 prio=4");ESP_LOGI(TAG,"[TASK] command_task core=0 prio=5");}
    if(CONFIG_A10_LOCAL_BASELINE_MS>0){ESP_LOGI(TAG,"baseline local antes de Wi-Fi: %d ms",CONFIG_A10_LOCAL_BASELINE_MS);vTaskDelay(pdMS_TO_TICKS(CONFIG_A10_LOCAL_BASELINE_MS));}
    start_connectivity_if_configured();
}
