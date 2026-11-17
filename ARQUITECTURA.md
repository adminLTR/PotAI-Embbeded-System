# PotAI — Especificación de arquitectura

Sistema de riego automático con inferencia en el borde. Un ESP32 mide el ambiente y el suelo, decide cuántos mililitros aplicar con un modelo embebido y publica el resultado. La nube guarda el historial. La app solo lee ese historial por HTTPS.

La app no habla con el ESP32. El puente es MQTT hacia AWS IoT Core (o, en desarrollo, una API local que imita el mismo contrato HTTP).

## 1. Vista del sistema

```text
                         Wi-Fi
  Sensores ──► ESP32 ──────────────► AWS IoT Core
  DHT11        inferencia RF          │
  suelo ADC    bomba + OLED           │ reglas MQTT
  puerta/LED                          ▼
                               Lambda ingest
                                      │
                                      ▼
                                  DynamoDB
                                      ▲
                                      │ Query
                               Lambda API ◄── API Gateway (HTTPS)
                                      ▲
                                      │
                                 App PotAI (Flutter)
                                 Inicio · Historial · Ajustes
```

Hay tres formas de operar la app, con el mismo contrato HTTP:

| Modo | Quién responde | Datos |
|---|---|---|
| Demo | `DemoPotaiApi` dentro de la app | Series sintéticas, sin red |
| API local | `02_cloud_aws/local_server.py` (puerto 8080) | DynamoDB en memoria. No simula IoT Core ni el MQTT del ESP32 |
| AWS | API Gateway + Lambda | Telemetría y riegos reales del dispositivo |

## 2. Principios

1. **La decisión de riego vive en el ESP32.** Si se cae la red, la bomba sigue funcionando con la última lectura válida. La nube no calcula mililitros.
2. **Un solo identificador de dispositivo.** `thingName` de AWS IoT, `client_id` MQTT y `device_id` del JSON son el mismo valor (`CONFIG_A10_THING_NAME`, por defecto `psi3542-g01`). Debe empezar por `psi3542-`.
3. **Un solo mapa de plantas.** El id numérico (1..8) es la columna `localname` del dataset. Ese número viaja en `plant_id` y tiene que coincidir en cuatro sitios: `03_model_ML/train.py`, `menuconfig` (`A10_PLANT_ID`), `02_cloud_aws/lambda_api/plants.py` y `04_app_potai/lib/models.dart`.
4. **Publicar no es ejecutar.** `POST /command` responde HTTP 202 cuando IoT Core acepta el mensaje. Eso no confirma que el LED del ESP32 haya cambiado.
5. **Fallos de hardware de riego no tumban el firmware.** Si el modelo, el ADC de suelo o el OLED fallan al arrancar, el resto (Wi-Fi, MQTT, DHT11, HTTP local) sigue.

## 3. Mapa de carpetas

```text
PROJETO/
├── 01_firmware_esp32/     ESP-IDF. Proyecto psi3542_a10_aws_iot_core
├── 02_cloud_aws/          Lambdas, CloudFormation, servidor local, tests
├── 03_model_ML/           Entrenamiento y exportación del bosque a binario
├── 04_app_potai/          Cliente Flutter (Android, Windows, web, iOS)
└── ARQUITECTURA.md        Este documento
```

## 4. Firmware (`01_firmware_esp32`)

ESP-IDF + FreeRTOS. El punto de entrada es `main/app_main.c`. La configuración (Wi-Fi, pines, planta, periodos, QoS) sale de `idf.py menuconfig`, definida en `main/Kconfig.projbuild`.

### 4.1 Componentes

| Componente | Responsabilidad |
|---|---|
| `connectivity` / `wifi_adapter` | Asociación Wi-Fi y estado `ONLINE` |
| `time_sync` | SNTP (`pool.ntp.org` por defecto). MQTT no arranca sin hora |
| `mqtt_adapter` | Cliente MQTT sobre TLS |
| `mqtt_service` | Tópicos `psi3542/<thing>/<kind>`, publicación y suscripción de comandos |
| `aws_iot_credentials` | Certificado de dispositivo y clave (no van en el repositorio) |
| `dht11` | Temperatura (°C) y humedad ambiente (%) |
| `soil_moisture` | ADC1 de la sonda. Porcentaje lineal entre `raw_dry` y `raw_wet`. Fuera de raw 100..4090 la muestra se marca inválida |
| `device_state` | Instantánea compartida: lecturas, bomba, última predicción, contador de riegos |
| `ml_model` | Random Forest en flash (`model/rf_model.bin`). Inferencia in-place, sin copia a RAM |
| `irrigation` | Bucle de decisión, temporización de la bomba y registro del riego |
| `pump` | Relé. Se apaga solo con `esp_timer`; tiempo máximo duro (`A10_PUMP_MAX_ON_MS`) |
| `buzzer` | Aviso al encender la bomba |
| `oled_ssd1306` / `display_ui` | OLED SSD1306 128×64 por I2C. Tres páginas, el botón las rota |
| `event_logic` | Flanco de puerta con debounce; enciende el LED local |
| `cloud_command` | Comando de nube `{"led": true\|false}` |
| `telemetry` | Codificación JSON de telemetría y de riego |
| `health` | Instantánea de salud (heap, reset, contadores de error) |
| `http_service` | API HTTP en la LAN, solo con Wi-Fi. No es la API de la app |

### 4.2 Tareas

| Tarea | Núcleo | Prioridad | Qué hace |
|---|---|---|---|
| `event_task` | 0 | 10 | Debounce de la puerta y eventos `door_open` / `door_closed` |
| `sensor_task` | 1 | 5 | Cada `A10_SENSOR_PERIOD_MS` (3 s): DHT11 + suelo. Encola telemetría si el DHT11 es válido |
| `irrigation_task` | 1 | 4 | Cada `A10_IRRIGATION_CHECK_PERIOD_MS`: inferencia y, si procede, bomba |
| `command_task` | 0 | 5 | Aplica el LED pedido por la nube |
| `mqtt_task` | 0 | 4 | Arranca SNTP y MQTT; publica salud, riegos, telemetría y eventos |
| `http_manager_task` | cualquiera | 4 | Sube o baja el HTTP local según `CONNECTIVITY_ONLINE` |
| `ui_task` | cualquiera | 3 | Refresco del OLED; el botón avanza de página |

La bomba se inicializa apagada antes de crear tareas. Wi-Fi espera `A10_LOCAL_BASELINE_MS` (8 s) para dejar ver el baseline local.

### 4.3 Cadena de conectividad

Los estados son independientes y se encadenan en `mqtt_task`:

```text
Wi-Fi ONLINE  →  SNTP SYNCED  →  MQTT/TLS (X.509) ONLINE  →  SUBSCRIBE del tópico command
```

Sin SSID, sin endpoint ATS válido o sin credenciales, el MQTT queda apagado en ese boot. Sensores, riego y HTTP local (si hay IP) siguen.

### 4.4 Colas

| Cola | Política si se llena |
|---|---|
| Publicación (telemetría y eventos) | Descarta el ítem nuevo |
| Comandos de nube | Descarta el comando nuevo |
| Riegos concluidos (profundidad 8) | Descarta el más antiguo y mete el nuevo |

Un fallo de `publish` después de sacar el ítem de la cola no lo reencola.

### 4.5 Decisión de riego

En cada periodo, y solo si el modelo pasó `ml_model_init`:

1. Exige DHT11 válido, suelo válido y ambas muestras más nuevas que `3 × periodo del sensor + 1 s`.
2. Predice mililitros: `ml_model_predict_ml(plant_id, suelo_%, temperatura_C)`.
3. No riega si la predicción es menor que `A10_IRRIGATION_MIN_ML`.
4. No riega si el suelo ya está en o por encima de `A10_IRRIGATION_SOIL_MAX_PCT`.
5. Respeta `A10_IRRIGATION_COOLDOWN_S` desde el fin del riego anterior.
6. Duración = `ml × 60 000 / caudal_ml_por_min`, recortada a `A10_PUMP_MAX_ON_MS`.
7. Al terminar publica el evento `pump_off` y encola el registro de riego (ml predichos, ml estimados por tiempo × caudal, duración, suelo y temperatura de antes de regar).

### 4.6 Interfaz local

OLED SSD1306 de 128×64 por I2C (3,3 V, SDA y SCL). Sigue habiendo tres páginas: ambiente (temperatura y humedad), suelo y planta, última decisión de la IA y estado de la bomba. El pulsador entre GPIO y GND avanza de página.

HTTP en el puerto `A10_HTTP_PORT` (80), útil en la LAN del dispositivo:

- `GET /health`
- `GET /api/v1/status`
- `GET /api/v1/telemetry/latest`

La app PotAI no usa estas rutas.

### 4.7 Pines por defecto

| Señal | GPIO | Nota |
|---|---|---|
| DHT11 | 4 | Salida |
| LED | 2 | También lo gobierna el comando de nube |
| Puerta | 27 | Entrada, pull-up, activo en bajo |
| Suelo (AO) | 34 | ADC1, solo entrada. ADC2 no se usa con Wi-Fi |
| Relé de bomba | 26 | Activo en bajo |
| Buzzer | configurable | Activo en bajo |
| OLED I²C | SDA 21, SCL 22, dirección 0x3C | 3,3 V. Opcional (`A10_OLED_ENABLED`) |
| Botón de páginas | 33 | A GND, pull-up interno |

`app_main` rechaza el arranque si un GPIO está en 6..11 (flash), si una salida cae en 34..39, o si dos funciones comparten pin.

### 4.8 Tópicos MQTT

Prefijo fijo `psi3542/<thingName>/`.

| Tópico | Dirección | QoS típico | Contenido |
|---|---|---|---|
| `telemetry` | dispositivo → nube | 0 | Lectura periódica |
| `irrigation` | dispositivo → nube | — | Un mensaje al cerrar cada riego |
| `event` | dispositivo → nube | configurable | `door_open`, `door_closed`, `sensor_unavailable`, `sensor_recovered`, `pump_on`, `pump_off` |
| `health` | dispositivo → nube | configurable | Salud del firmware (`a10-v07`) |
| `command` | nube → dispositivo | 1 | `{"led": true\|false}` |

La política IoT del dispositivo tiene que permitir `iot:Publish` también en `psi3542/<thingName>/irrigation`. Sin eso los riegos no llegan a DynamoDB.

Telemetría:

```json
{
  "device_id": "psi3542-g01",
  "seq": 1,
  "temperature_c": 25.0,
  "humidity_pct": 58.0,
  "valid": true,
  "sample_uptime_ms": 1234,
  "soil_moisture_pct": 28.0,
  "soil_valid": true,
  "plant_id": 4,
  "ml_predicted": 120.0,
  "pump_on": false,
  "irrigation_count": 3
}
```

Riego:

```json
{
  "device_id": "psi3542-g01",
  "seq": 1,
  "plant_id": 4,
  "soil_moisture_pct": 28.0,
  "temperature_c": 25.0,
  "ml_predicted": 120.0,
  "ml_delivered": 118.0,
  "duration_ms": 7000,
  "pump_uptime_ms": 123456
}
```

Los campos PotAI de la telemetría son opcionales para la ingesta: un firmware anterior que solo mande temperatura, humedad y `valid` sigue aceptándose.

## 5. Modelo (`03_model_ML`)

Entrenamiento fuera del dispositivo. El ESP32 solo ejecuta el bosque ya exportado.

### 5.1 Datos y objetivo

Dataset `dataset_sintetico_plantas_numerico.csv` (sintético, en buena parte basado en reglas).

| Entrada | Significado |
|---|---|
| `localname` | Id de planta, entero 1..8 |
| `moisture` | Humedad del suelo, % |
| `temperature` | Temperatura, °C |

Objetivo: `mililitros_riego`.

Partición 80/20, `random_state=42`, estratificada por `localname`.

### 5.2 Candidatos

`train.py` entrena tres pipelines (one-hot del id de planta + `StandardScaler` de humedad y temperatura) y escribe `model_comparison.csv` más un pickle por modelo:

| Modelo | Para qué se mira |
|---|---|
| `ridge` | Línea base lineal, pequeña |
| `random_forest` | El que se exporta al ESP32 (`model_random_forest.pkl`) |
| `hist_gradient_boosting` | Referencia de precisión |

El pickle de scikit-learn no se flashea. `estimator_kb` y el número de nodos son solo un coste relativo.

### 5.3 Exportación al borde

`export_model_esp32.py` convierte `model_random_forest.pkl` en `01_firmware_esp32/components/ml_model/model/rf_model.bin`:

1. Tira el preprocesado. Cada umbral vuelve al dominio original (planta, % de suelo, °C).
2. Cuantiza entradas en décimas (×10, int16) y hojas en Q8.8 (`ml = payload / 256`, hasta 255,99 ml).
3. Cada nodo ocupa 4 bytes, recorrido en preorden. El hijo izquierdo es el nodo siguiente; solo se guarda el índice del derecho.
4. Comprueba el binario contra el pipeline original antes de escribirlo.

Cabecera little-endian: magic `A10M`, versión 1, número de árboles, número de nodos, `leaf_scale`, `input_scale`, CRC16-CCITT del cuerpo. Tipos de nodo: 0 humedad, 1 temperatura, 2 igualdad de planta, 3 hoja.

`ml_model_init` valida magic, versión, tamaño y CRC. Si falla, el riego automático queda deshabilitado.

### 5.4 Catálogo de plantas

| id | Nombre | Nombre científico |
|---|---|---|
| 1 | ajo | Allium sativum |
| 2 | geranio | Pelargonium |
| 3 | hierbabuena | Mentha spicata |
| 4 | menta | Mentha |
| 5 | orégano | Origanum vulgare |
| 6 | orquídea | Orchidaceae |
| 7 | rosa china | Hibiscus rosa-sinensis |
| 8 | tomate cherry | Solanum lycopersicum var. cerasiforme |

El catálogo que ve la app (nombre científico, días entre riegos, horas de luz, humedad óptima) vive en `plants.py`. El modelo no usa esos cuidados: solo el id, la humedad y la temperatura.

## 6. Nube (`02_cloud_aws`)

No hay un servidor permanente. AWS ejecuta dos funciones cuando llega tráfico. Todo el stack sale de `infra/cloudformation.yaml`. El código se empaqueta con `build_backend_zip.py` (`dist/ingest.zip`, `dist/api.zip`) y CloudFormation lo toma de S3.

```text
psi3542/+/telemetry   ─regla─► Lambda ingest ─► PSI3542Telemetry
psi3542/+/irrigation  ─regla─► Lambda ingest ─► PSI3542Irrigation
App ─HTTPS─► API Gateway ─► Lambda api ─Query─► las dos tablas
Lambda api ─iot:Publish─► psi3542/<thing>/command
```

Runtime Python 3.12, 128 MB, timeout 10 s. Logs en CloudWatch, retención 7 días.

### 6.1 Reglas de IoT

Ambas hacen `SELECT *` y añaden:

- `thingName = topic(2)`
- `ingested_at_ms = timestamp()`
- `message_trace_id = traceid()`

La regla de riego añade además `record_type = 'irrigation'`. Sin `record_type`, la Lambda trata el mensaje como telemetría.

### 6.2 DynamoDB

Pago por petición. Clave de partición `thingName` (string), clave de orden `ingestionKey` (string):

```text
{ingested_at_ms en 13 dígitos}#{seq en 10 dígitos}#{message_trace_id}
```

Así el query descendente devuelve lo más nuevo primero y un reintento con el mismo trace no duplica la fila (`attribute_not_exists(ingestionKey)`).

| Tabla | TTL (`expires_at`) | Contenido |
|---|---|---|
| `PSI3542Telemetry` | 7 días | Lecturas |
| `PSI3542Irrigation` | 90 días | Riegos cerrados |

La ingesta rechaza y no guarda: `thingName` que no empiece por `psi3542-`, `device_id` distinto del thing, `valid` que no sea booleano, números no finitos, humedad fuera de 0..100, y cualquier `record_type` desconocido.

### 6.3 API HTTP

Sin autenticación (`AuthorizationType: NONE`). Pensada para la práctica, no para exponerse en internet. CORS limitado al origen configurado en el stack.

| Método y ruta | Respuesta |
|---|---|
| `GET /plants` | Catálogo |
| `GET /devices/{thingName}/dashboard` | Lo que pinta Inicio: última lectura, planta, último riego, riegos y ml de las últimas 24 h, flag `online` |
| `GET /devices/{thingName}/telemetry/latest` | Última lectura. 404 `telemetry_not_found` si no hay filas |
| `GET /devices/{thingName}/telemetry/history?limit=` | Puntos, máximo 200, default 60, más nuevo primero |
| `GET /devices/{thingName}/irrigations?limit=` | Riegos, máximo 100, default 30 |
| `POST /devices/{thingName}/command` | Cuerpo exacto `{"led": true\|false}`. 202 si IoT Core acepta el publish |

`online` es verdadero si la última telemetría tiene como mucho 30 s (`ONLINE_WINDOW_MS`). Con el periodo por defecto de 3 s, el historial de 200 puntos cubre unos 10 minutos; para ver horas hay que subir el periodo del sensor en menuconfig.

`thingName` inválido → 400 `invalid_thing_name`. Error interno → 500 `internal_error`, sin traza al cliente.

### 6.4 Servidor local

`local_server.py` (o `docker compose up --build`) carga las mismas Lambdas contra un DynamoDB en memoria y un publicador IoT falso. Sirve para la app y el teléfono. No recibe MQTT del ESP32.

| Cliente | URL | Thing |
|---|---|---|
| PC / Chrome | `http://127.0.0.1:8080` | `psi3542-g01` |
| Emulador Android | `http://10.0.2.2:8080` | `psi3542-g01` |
| Teléfono en la misma Wi-Fi | `http://<IPv4 del PC>:8080` | `psi3542-g01` |

## 7. Aplicación (`04_app_potai`)

Flutter (SDK ^3.11). Paquete `potai`, versión 1.0.0. Dependencias de producto: `http`, `shared_preferences`, `fl_chart`.

```text
main.dart          MaterialApp + navegación de tres destinos
controller.dart    PotaiController: ajustes, sondeo cada 5 s, ChangeNotifier
api.dart           PotaiApi = HttpPotaiApi | DemoPotaiApi
models.dart        Dashboard, LatestReading, HistoryPoint, IrrigationRecord, PlantInfo
screens/           home_screen, history_screen, settings_screen
```

El controlador guarda en `SharedPreferences` la URL base, el thing y el flag de demo. En demo construye `DemoPotaiApi`; si no, `HttpPotaiApi` con timeout de 8 s. Cada ciclo pide `dashboard`, `telemetry/history` e `irrigations`.

| Pantalla | Qué muestra |
|---|---|
| Inicio | Planta, humedad del suelo, temperatura, recomendación de la IA, último riego, si el dispositivo está en línea |
| Historial | Gráfico de suelo, humedad ambiente y temperatura, más la lista de riegos |
| Ajustes | URL, Thing Name, modo demo |

La app no abre MQTT, no usa el SDK de AWS y no envía el comando del LED: ese `POST` queda en el contrato de la API para el cliente de la práctica original.

## 8. Flujos

### 8.1 Lectura hasta la pantalla de inicio

```text
sensor_task
  → device_state (DHT11 + suelo)
  → cola de publicación
  → mqtt_task publica psi3542/<thing>/telemetry
  → regla IoT añade thingName, tiempo y trace
  → Lambda ingest valida y hace PutItem
  → la app, cada 5 s, GET /dashboard
  → Lambda api lee la última fila (edad ≤ 30 s ⇒ online)
```

### 8.2 Riego

```text
irrigation_task
  → predicción en flash
  → si pasa umbral, techo de suelo y cooldown: pump_start(duración)
  → al apagarse, record_cb encola el informe
  → mqtt_task publica psi3542/<thing>/irrigation  (antes que la telemetría pendiente)
  → regla con record_type=irrigation
  → tabla PSI3542Irrigation (TTL 90 días)
  → GET /irrigations y el bloque irrigation del dashboard
```

### 8.3 Comando de LED

```text
POST /devices/{thing}/command  {"led": true}
  → Lambda api publica QoS 1 en psi3542/{thing}/command
  → HTTP 202
  → mqtt_service entrega el comando a la cola
  → command_task escribe el GPIO del LED y actualiza device_state
```

El 202 solo significa que la API entregó el mensaje a IoT Core.

## 9. Qué queda fuera de este diseño

- La API no autentica al llamante. Quien tenga la URL puede leer y puede mandar el comando del LED.
- Certificados, clave privada, endpoint real y URL real de la API no están en el repositorio.
- El Thing, el certificado y la política IoT del dispositivo se crean aparte del stack de CloudFormation y no deben borrarse al eliminar ese stack.
- `local_server.py` no sustituye el camino MQTT del ESP32.
- El dataset es sintético. Las métricas de `model_comparison.csv` describen ese dataset, no mediciones de una maceta real.
- Salud y eventos se publican por MQTT. La app PotAI no los consulta; la ingesta actual persiste telemetría y riegos.
