import 'dart:convert';

import 'package:flutter/widgets.dart' show Size;
import 'package:flutter_test/flutter_test.dart';
import 'package:http/http.dart' as http;
import 'package:http/testing.dart';
import 'package:potai/api.dart';
import 'package:potai/controller.dart';
import 'package:potai/main.dart';
import 'package:potai/models.dart';
import 'package:shared_preferences/shared_preferences.dart';

/// Respuesta con la misma forma que devuelve GET /devices/{thing}/dashboard.
Map<String, dynamic> dashboardJson({bool pumpOn = false, double ml = 120}) => {
      'thingName': 'psi3542-g01',
      'server_time_ms': 1800000000000,
      'online': true,
      'latest': {
        'thingName': 'psi3542-g01',
        'seq': 12,
        'temperature_c': 25,
        'humidity_pct': 58,
        'valid': true,
        'sample_uptime_ms': 5000,
        'ingested_at_ms': 1799999998000,
        'ingested_at': '2027-01-15T08:00:00.000Z',
        'age_ms': 2000,
        'soil_valid': true,
        'soil_moisture_pct': 27.5,
        'plant_id': 4,
        'ml_predicted': ml,
        'pump_on': pumpOn,
        'irrigation_count': 2,
      },
      'plant': {
        'id': 4,
        'common_name': 'menta',
        'scientific_name': 'Mentha',
        'optimal_water_frequency_days': 2,
        'optimal_light_hours': 5,
        'optimal_humidity_percent': 70,
      },
      'irrigation': {
        'last': {
          'seq': 2,
          'plant_id': 4,
          'soil_moisture_pct': 28,
          'temperature_c': 26,
          'ml_predicted': 120,
          'ml_delivered': 118.5,
          'duration_ms': 5900,
          'ingested_at_ms': 1799996400000,
          'ingested_at': '2027-01-15T07:00:00.000Z',
        },
        'count_24h': 2,
        'ml_24h': 218.5,
      },
    };

class FakeApi implements PotaiApi {
  FakeApi(this.json);
  final Map<String, dynamic> json;

  @override
  Future<Dashboard> dashboard() async => Dashboard.fromJson(json);

  @override
  Future<List<HistoryPoint>> history({int limit = 100}) async => [];

  @override
  Future<List<IrrigationRecord>> irrigations({int limit = 30}) async => [
        IrrigationRecord.fromJson(
            (json['irrigation'] as Map<String, dynamic>)['last'] as Map<String, dynamic>),
      ];
}

Future<PotaiController> makeController({
  Map<String, Object> prefs = const {},
  ApiFactory? factory,
}) async {
  SharedPreferences.setMockInitialValues(prefs);
  final store = SettingsStore(await SharedPreferences.getInstance());
  return PotaiController(
    store: store,
    apiFactory: factory,
    pollInterval: const Duration(hours: 1),
  );
}

void main() {
  testWidgets('sin configurar muestra la bienvenida', (tester) async {
    final controller = await makeController();
    await tester.pumpWidget(PotaiApp(controller: controller));

    expect(find.text('¡Bienvenido a PotAI!'), findsOneWidget);
    expect(find.text('Ver con datos de ejemplo'), findsOneWidget);
    controller.dispose();
  });

  testWidgets('el inicio muestra planta, suelo, IA y bomba', (tester) async {
    // Pantalla alta: el ListView solo construye lo que esta visible.
    tester.view.physicalSize = const Size(800, 3000);
    tester.view.devicePixelRatio = 1.0;
    addTearDown(tester.view.reset);
    final controller = await makeController(
      prefs: {
        'potai.apiBaseUrl': 'https://api.example.com',
        'potai.thingName': 'psi3542-g01',
      },
      factory: (_) => FakeApi(dashboardJson(pumpOn: true)),
    );
    await tester.pumpWidget(PotaiApp(controller: controller));
    await controller.refresh();
    await tester.pump();

    expect(find.text('Menta'), findsOneWidget);
    expect(find.text('Mentha'), findsOneWidget);
    expect(find.text('En línea'), findsOneWidget);
    expect(find.text('28%'), findsOneWidget); // 27.5 redondeado
    expect(find.text('Suelo seco'), findsOneWidget);
    expect(find.text('25 °C'), findsOneWidget);
    expect(find.text('58 %'), findsOneWidget);
    expect(find.text('120'), findsOneWidget);
    expect(find.text('Regando ahora'), findsOneWidget);
    controller.dispose();
  });

  testWidgets('el modo demo carga datos de ejemplo', (tester) async {
    final controller = await makeController(prefs: {'potai.demo': true});
    await tester.pumpWidget(PotaiApp(controller: controller));
    await controller.refresh();
    await tester.pump();

    expect(find.text('Menta'), findsOneWidget);
    controller.dispose();
  });

  test('HttpPotaiApi interpreta la respuesta del backend', () async {
    late Uri requested;
    final client = MockClient((request) async {
      requested = request.url;
      return http.Response(jsonEncode(dashboardJson()), 200,
          headers: {'content-type': 'application/json'});
    });
    final api = HttpPotaiApi(
      baseUrl: 'https://api.example.com/',
      thingName: 'psi3542-g01',
      client: client,
    );

    final dash = await api.dashboard();
    expect(requested.toString(), 'https://api.example.com/devices/psi3542-g01/dashboard');
    expect(dash.online, isTrue);
    expect(dash.plant!.displayName, 'Menta');
    expect(dash.latest!.soilMoisturePct, 27.5);
    expect(dash.irrigation.ml24h, 218.5);
  });

  test('HttpPotaiApi traduce los errores a mensajes amigables', () async {
    final client = MockClient(
        (_) async => http.Response(jsonEncode({'error': 'invalid_thing_name'}), 400));
    final api = HttpPotaiApi(baseUrl: 'https://api.example.com', thingName: 'psi3542-x', client: client);

    expect(api.dashboard(), throwsA(isA<ApiException>()));
  });

  test('la validacion de ajustes detecta datos incorrectos', () {
    expect(const Settings(apiBaseUrl: 'abc', thingName: 'psi3542-g01').validate(), isNotNull);
    expect(const Settings(apiBaseUrl: 'https://x.execute-api.sa-east-1.amazonaws.com', thingName: 'otro').validate(), isNotNull);
    expect(const Settings(apiBaseUrl: 'https://x.execute-api.sa-east-1.amazonaws.com', thingName: 'psi3542-g01').validate(), isNull);
    expect(const Settings(apiBaseUrl: 'http://192.168.0.10:8080', thingName: 'psi3542-g01').validate(), isNull);
    expect(const Settings(demo: true).validate(), isNull);
  });

  test('los nombres de planta se muestran bonitos', () {
    PlantInfo plant(String name) => PlantInfo(
        id: 1, commonName: name, scientificName: '', waterEveryDays: 1, lightHours: 1, optimalHumidity: 1);
    expect(plant('tomatecherry').displayName, 'Tomate cherry');
    expect(plant('rosachina').displayName, 'Rosa china');
    expect(plant('ajo').displayName, 'Ajo');
  });
}
