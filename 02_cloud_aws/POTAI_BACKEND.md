# PotAI – Backend (guía para quien nunca usó AWS)

## 1. ¿Qué es el "backend" aquí?

No hay un servidor Python encendido 24/7. Son **funciones pequeñas (AWS Lambda)**
que AWS ejecuta solo cuando llega algo. Son el puente entre el ESP32 y la app:

```
ESP32 --MQTT--> AWS IoT Core --Regla--> Lambda "ingest" --> DynamoDB (base de datos)
                                                                  |
App PotAI <--HTTPS-- API Gateway <-- Lambda "api" <---------------+
```

| Pieza | Para qué sirve | Código |
|---|---|---|
| IoT Core | Recibe los mensajes MQTT del ESP32 | (servicio de AWS) |
| IoT Rule | Reenvía cada mensaje a la Lambda correcta | `infra/cloudformation.yaml` |
| Lambda `ingest` | Valida y guarda lecturas y riegos | `lambda_ingest/lambda_function.py` |
| DynamoDB | Guarda lecturas (7 días) y riegos (90 días) | `infra/cloudformation.yaml` |
| Lambda `api` | Responde a la app (Python) | `lambda_api/lambda_function.py` |
| API Gateway | Da una URL https a la Lambda `api` | `infra/cloudformation.yaml` |

Todo se crea con **un solo archivo**: `infra/cloudformation.yaml`.

## 2. Tipos de planta

El modelo recibe un número. Regla: **id = posición en la lista de `train.py`**.

| id | planta | id | planta |
|---|---|---|---|
| 1 | ajo | 5 | orégano |
| 2 | geranio | 6 | orquídea |
| 3 | hierbabuena | 7 | rosa china |
| 4 | menta | 8 | tomate cherry |

- Firmware: `idf.py menuconfig` → *Riego automatico con IA* → *Tipo de planta*.
  Se envía en cada mensaje como `plant_id`.
- Backend: `lambda_api/plants.py` (mismo mapa, con nombre científico y cuidados).
- **Verifica** que esta numeración coincide con la que usó tu `train.py`
  (el `localname` real). Si no, cambia el mapa en `plants.py`, en Kconfig y en
  `04_app_potai/lib/models.dart` (nombres/emoji).

## 3. Despliegue paso a paso (consola de AWS)

1. Genera los zips: `python build_backend_zip.py` → `dist/ingest.zip` y `dist/api.zip`.
2. En **S3** crea un bucket (misma región que usarás en todo) y sube los dos
   zips a la carpeta `potai/` (`potai/ingest.zip`, `potai/api.zip`).
3. En **IoT Core → Settings** copia el *Device data endpoint*
   (`xxxx-ats.iot.<región>.amazonaws.com`).
4. En **CloudFormation → Create stack → Upload a template file** sube
   `infra/cloudformation.yaml`. Parámetros importantes:
   - `CodeBucket`: nombre del bucket del paso 2.
   - `IotDataEndpoint`: `https://` + endpoint del paso 3.
   - El resto puede quedar por defecto.
   Marca la casilla *"I acknowledge that AWS CloudFormation might create IAM resources"*.
5. Cuando el stack diga `CREATE_COMPLETE`, pestaña **Outputs** → copia `ApiBaseUrl`.
6. En la app PotAI → *Ajustes*: pega `ApiBaseUrl` y el *Thing Name* del ESP32
   (el configurado en menuconfig, `A10_THING_NAME`).

El certificado/Thing de IoT Core del ESP32 es el mismo flujo que ya usabas en la
Aula 10. **Sí hay que ampliar la IoT Policy** del dispositivo: añade permiso de
`iot:Publish` al tópico `psi3542/<thingName>/irrigation` (si no, los riegos no
llegan a DynamoDB y la app no los muestra). Telemetría, eventos, health y
comando siguen igual.

## 4. Contrato MQTT (ESP32 → nube)

Topics: `psi3542/<thingName>/telemetry` y `psi3542/<thingName>/irrigation`.

Telemetría:
```json
{"device_id":"...","seq":1,"temperature_c":25.0,"humidity_pct":58.0,"valid":true,
 "sample_uptime_ms":1234,"soil_moisture_pct":28.0,"soil_valid":true,
 "plant_id":4,"ml_predicted":120.0,"pump_on":false,"irrigation_count":3}
```
Riego (se envía al terminar de regar):
```json
{"device_id":"...","seq":1,"plant_id":4,"soil_moisture_pct":28.0,
 "temperature_c":25.0,"ml_predicted":120.0,"ml_delivered":118.0,
 "duration_ms":7000,"pump_uptime_ms":123456}
```

## 5. API que consume la app

| Método y ruta | Devuelve |
|---|---|
| `GET /plants` | Catálogo de plantas |
| `GET /devices/{thing}/dashboard` | Estado completo (lo que usa la pantalla de inicio) |
| `GET /devices/{thing}/telemetry/latest` | Última lectura |
| `GET /devices/{thing}/telemetry/history?limit=60` | Historial (máx. 200, más nuevo primero) |
| `GET /devices/{thing}/irrigations?limit=30` | Últimos riegos (máx. 100) |
| `POST /devices/{thing}/command` | Comando al ESP32 (ya existía) |

## 6. Limitaciones y consejos

- **La API no tiene autenticación.** Cualquiera con la URL puede leer los datos
  (y enviar comandos). Está bien para el proyecto de clase; no lo publiques.
- El historial devuelve máx. 200 puntos: con lecturas cada 5 s son ~17 min.
  Para ver horas, sube el período del sensor en menuconfig a 30–60 s.
- Costos: con un solo dispositivo cabe holgadamente en la capa gratuita, pero
  borra el stack (CloudFormation → Delete) cuando termines.
- Pruebas locales del backend (sin AWS): `python -m unittest discover -s tests`
  desde `02_cloud_aws`. No se probó con AWS real.
- Modo **Demo** de la app: datos de ejemplo sin backend ni ESP32.
- **API local** (misma interfaz que AWS, DynamoDB en memoria). No simula IoT Core
  ni el MQTT de la ESP32; sirve para la app y el teléfono:

  ```text
  cd 02_cloud_aws
  python local_server.py
  # o: docker compose up --build
  ```

  En PotAI → Ajustes: `http://127.0.0.1:8080` (PC), `http://10.0.2.2:8080`
  (emulador Android) o `http://<IP-de-tu-PC>:8080` (teléfono en la misma WiFi).
  Thing Name: `psi3542-g01`.
