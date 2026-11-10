# PotAI

App Flutter del sistema de riego con IA. Lee lecturas, predicciones y riegos
desde el backend (`02_cloud_aws`) y los muestra en tres pantallas:

- **Inicio**: planta, humedad del suelo, temperatura, recomendación de la IA y último riego.
- **Historial**: gráfico de suelo / ambiente / temperatura y lista de riegos.
- **Ajustes**: URL de la API y Thing Name. También hay un **modo demo** (datos de ejemplo, sin internet).

La ESP32 no habla con la app. El puente es:

```text
ESP32  --MQTT-->  AWS IoT  -->  Lambda ingest  -->  DynamoDB
App PotAI  --HTTPS-->  API Gateway  -->  Lambda api  -->  DynamoDB
```

Sin AWS, el puente local es `python local_server.py` (o Docker) en `02_cloud_aws`.

## Cómo ejecutarla en el PC

Necesitas [Flutter](https://docs.flutter.dev/get-started/install/windows). Luego:

```text
cd 04_app_potai
flutter pub get
flutter run -d chrome
```

O `flutter run -d windows` si tienes el workload de Visual Studio C++.

### Tres formas de ver datos

1. **Modo demo** (lo más rápido): abre la app → *Ver con datos de ejemplo*. No necesita backend.
2. **API local** (la app habla de verdad con el backend, sin AWS):

   ```text
   cd 02_cloud_aws
   python local_server.py
   ```

   o `docker compose up --build`. En Ajustes: URL `http://127.0.0.1:8080`, Thing Name `psi3542-g01`.
3. **AWS** (cuando lo despliegues): pega `ApiBaseUrl` y el Thing Name de menuconfig.

La guía del backend está en `02_cloud_aws/POTAI_BACKEND.md`.

## Cómo instalarla en el teléfono (Android)

En este PC Flutter ya está instalado. Falta completar el SDK de Android y conectar el móvil.

1. Abre **Android Studio** → *More Actions* → *SDK Manager* → pestaña *SDK Tools*.
   Marca **Android SDK Command-line Tools** e instálalo.
2. En una terminal:

   ```text
   flutter doctor --android-licenses
   ```

   Acepta todas las licencias (teclas `y`).
3. En el teléfono: *Ajustes* → *Acerca del teléfono* → toca **Número de compilación** 7 veces.
   Vuelve atrás → *Opciones de desarrollador* → activa **Depuración USB**.
4. Conecta el USB, acepta “¿Permitir depuración USB?” en el teléfono.
5. Comprueba que Flutter lo ve:

   ```text
   cd 04_app_potai
   flutter devices
   flutter run
   ```

   Si hay varios dispositivos, elige el del teléfono (`flutter run -d <id>`).

### Instalar un APK (sin cable, después)

```text
cd 04_app_potai
flutter build apk
```

El archivo queda en `build/app/outputs/flutter-apk/app-release.apk`. Cópialo al teléfono y ábrelo (hay que permitir “instalar apps desconocidas”).

### Teléfono + servidor local

El teléfono y el PC deben estar en la **misma WiFi**. En Ajustes de PotAI:

- URL: `http://192.168.x.x:8080` (la IPv4 del PC: `ipconfig` → *Dirección IPv4*).
  **No uses** `127.0.0.1` en el teléfono: eso es el propio teléfono.
- Thing Name: `psi3542-g01`.
- Modo demo: apagado.

Si no conecta, permite el puerto 8080 en el Firewall de Windows (red privada) o prueba primero el modo demo.

iOS hace falta un Mac y Xcode; en Windows solo Android.

## Pruebas

```text
cd 04_app_potai
flutter test
```
