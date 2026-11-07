# PotAI

App Flutter del sistema de riego con IA. Lee lecturas, predicciones y riegos
desde el backend de AWS (`02_cloud_aws`) y los muestra en tres pantallas:

- **Inicio**: planta, humedad del suelo, temperatura, recomendación de la IA y último riego.
- **Historial**: gráfico de suelo / ambiente / temperatura y lista de riegos.
- **Ajustes**: URL de la API y Thing Name. También hay un **modo demo** (datos de ejemplo, sin internet).

La ESP32 no habla con la app. El puente es:

```text
ESP32  --MQTT-->  AWS IoT  -->  Lambda ingest  -->  DynamoDB
App PotAI  --HTTPS-->  API Gateway  -->  Lambda api  -->  DynamoDB
```

## Cómo ejecutarla

Necesitas [Flutter](https://docs.flutter.dev/get-started/install). Luego:

```text
cd 04_app_potai
flutter pub get
flutter run
```

En Ajustes pega:

- **URL de la API**: CloudFormation → Outputs → `ApiBaseUrl`
- **Thing Name**: el mismo de menuconfig (`A10_THING_NAME`, p.ej. `psi3542-g01`)

Si aún no tienes AWS, entra con **Ver con datos de ejemplo**.

La guía del backend (qué es cada pieza de AWS y cómo desplegarla) está en
`02_cloud_aws/POTAI_BACKEND.md`.

## Pruebas

```text
cd 04_app_potai
flutter test
```
