import 'dart:async';
import 'dart:convert';
import 'dart:math' as math;

import 'package:http/http.dart' as http;

import 'models.dart';

class ApiException implements Exception {
  ApiException(this.message);
  final String message;

  @override
  String toString() => message;
}

abstract class PotaiApi {
  Future<Dashboard> dashboard();

  /// Lecturas recientes, de la mas nueva a la mas vieja.
  Future<List<HistoryPoint>> history({int limit = 100});

  /// Riegos recientes, del mas nuevo al mas viejo.
  Future<List<IrrigationRecord>> irrigations({int limit = 30});
}

/// Habla con la HTTP API de AWS (API Gateway + Lambda).
class HttpPotaiApi implements PotaiApi {
  HttpPotaiApi({
    required String baseUrl,
    required this.thingName,
    http.Client? client,
  })  : baseUrl = baseUrl.trim().replaceAll(RegExp(r'/+$'), ''),
        _client = client ?? http.Client();

  final String baseUrl;
  final String thingName;
  final http.Client _client;

  static const _timeout = Duration(seconds: 8);

  Future<Map<String, dynamic>> _get(String path,
      [Map<String, String>? query]) async {
    final uri = Uri.parse('$baseUrl/devices/$thingName/$path')
        .replace(queryParameters: query);
    final http.Response response;
    try {
      response = await _client.get(uri).timeout(_timeout);
    } on TimeoutException {
      throw ApiException('El servidor no respondió a tiempo.');
    } catch (_) {
      throw ApiException('No se pudo conectar. Revisa tu internet y la URL.');
    }

    Map<String, dynamic> body;
    try {
      body = jsonDecode(utf8.decode(response.bodyBytes)) as Map<String, dynamic>;
    } catch (_) {
      throw ApiException('Respuesta inesperada del servidor (${response.statusCode}).');
    }
    if (response.statusCode != 200) {
      throw ApiException(_friendlyError(response.statusCode, body['error']));
    }
    return body;
  }

  String _friendlyError(int status, Object? code) {
    switch (code) {
      case 'invalid_thing_name':
        return 'El nombre del dispositivo no es válido.';
      case 'telemetry_not_found':
        return 'Aún no hay datos de este dispositivo.';
      case 'internal_error':
        return 'Error interno del servidor. Intenta de nuevo.';
    }
    return 'Error del servidor (HTTP $status).';
  }

  @override
  Future<Dashboard> dashboard() async =>
      Dashboard.fromJson(await _get('dashboard'));

  @override
  Future<List<HistoryPoint>> history({int limit = 100}) async {
    final body = await _get('telemetry/history', {'limit': '$limit'});
    return (body['items'] as List)
        .map((e) => HistoryPoint.fromJson(e as Map<String, dynamic>))
        .toList();
  }

  @override
  Future<List<IrrigationRecord>> irrigations({int limit = 30}) async {
    final body = await _get('irrigations', {'limit': '$limit'});
    return (body['items'] as List)
        .map((e) => IrrigationRecord.fromJson(e as Map<String, dynamic>))
        .toList();
  }
}

/// Datos de ejemplo para ver la interfaz sin AWS ni ESP32.
class DemoPotaiApi implements PotaiApi {
  DemoPotaiApi({DateTime Function()? clock}) : _clock = clock ?? DateTime.now;

  final DateTime Function() _clock;

  static const _plant = PlantInfo(
    id: 4,
    commonName: 'menta',
    scientificName: 'Mentha',
    waterEveryDays: 2,
    lightHours: 5,
    optimalHumidity: 70,
  );

  int get _now => _clock().millisecondsSinceEpoch;

  double _soilAt(int ms) {
    // Ciclo lento: el suelo se seca y "se riega" cada ~10 minutos.
    final phase = (ms / 600000.0) % 1.0;
    return (75 - 50 * phase).clamp(20, 80).toDouble();
  }

  double _tempAt(int ms) => 24 + 3 * math.sin(ms / 900000.0);

  @override
  Future<Dashboard> dashboard() async {
    final now = _now;
    final irr = await irrigations(limit: 10);
    final soil = _soilAt(now);
    return Dashboard(
      thingName: 'psi3542-demo',
      serverTimeMs: now,
      online: true,
      latest: LatestReading(
        seq: now ~/ 3000,
        temperatureC: double.parse(_tempAt(now).toStringAsFixed(1)),
        humidityPct: 58,
        valid: true,
        soilMoisturePct: double.parse(soil.toStringAsFixed(1)),
        soilValid: true,
        plantId: _plant.id,
        mlPredicted: soil < 40 ? 120 : 0,
        pumpOn: false,
        irrigationCount: irr.length,
        ageMs: 1500,
        ingestedAtMs: now - 1500,
      ),
      plant: _plant,
      irrigation: IrrigationSummary(
        last: irr.first,
        count24h: irr.length,
        ml24h: irr.fold(0.0, (sum, r) => sum + r.mlDelivered),
      ),
    );
  }

  @override
  Future<List<HistoryPoint>> history({int limit = 100}) async {
    final now = _now;
    return List.generate(limit, (i) {
      final ms = now - i * 30000;
      return HistoryPoint(
        ingestedAtMs: ms,
        temperatureC: _tempAt(ms),
        humidityPct: 58,
        soilMoisturePct: _soilAt(ms),
        pumpOn: false,
      );
    });
  }

  @override
  Future<List<IrrigationRecord>> irrigations({int limit = 30}) async {
    final now = _now;
    return List.generate(math.min(limit, 5), (i) {
      return IrrigationRecord(
        seq: 5 - i,
        plantId: _plant.id,
        soilMoisturePct: 31.0 + i,
        temperatureC: 25.5,
        mlPredicted: 120,
        mlDelivered: 118.0 - i,
        durationMs: 5900,
        ingestedAtMs: now - (i + 1) * 4 * 3600 * 1000,
      );
    });
  }
}
