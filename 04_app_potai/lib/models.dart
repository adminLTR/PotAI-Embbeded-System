// Modelos de datos que devuelve la API de PotAI (02_cloud_aws/lambda_api).

double? _d(Object? v) => v is num ? v.toDouble() : null;
int? _i(Object? v) => v is num ? v.toInt() : null;

class PlantInfo {
  const PlantInfo({
    required this.id,
    required this.commonName,
    required this.scientificName,
    required this.waterEveryDays,
    required this.lightHours,
    required this.optimalHumidity,
  });

  final int id;
  final String commonName;
  final String scientificName;
  final int waterEveryDays;
  final int lightHours;
  final int optimalHumidity;

  factory PlantInfo.fromJson(Map<String, dynamic> json) => PlantInfo(
        id: _i(json['id']) ?? 0,
        commonName: json['common_name'] as String? ?? '',
        scientificName: json['scientific_name'] as String? ?? '',
        waterEveryDays: _i(json['optimal_water_frequency_days']) ?? 0,
        lightHours: _i(json['optimal_light_hours']) ?? 0,
        optimalHumidity: _i(json['optimal_humidity_percent']) ?? 0,
      );

  /// El backend guarda el nombre "tal cual" (rosachina, tomatecherry...);
  /// aqui solo se embellece para mostrarlo.
  String get displayName => _prettyNames[commonName] ?? _capitalize(commonName);

  String get emoji => _emojis[commonName] ?? '🪴';

  static const _prettyNames = {
    'oregano': 'Orégano',
    'orquidea': 'Orquídea',
    'rosachina': 'Rosa china',
    'tomatecherry': 'Tomate cherry',
    'hierbabuena': 'Hierbabuena',
  };

  static const _emojis = {
    'ajo': '🧄',
    'geranio': '🌼',
    'hierbabuena': '🌿',
    'menta': '🍃',
    'oregano': '🌱',
    'orquidea': '🌸',
    'rosachina': '🌺',
    'tomatecherry': '🍅',
  };

  static String _capitalize(String s) =>
      s.isEmpty ? s : '${s[0].toUpperCase()}${s.substring(1)}';
}

class LatestReading {
  const LatestReading({
    required this.seq,
    required this.temperatureC,
    required this.humidityPct,
    required this.valid,
    required this.soilMoisturePct,
    required this.soilValid,
    required this.plantId,
    required this.mlPredicted,
    required this.pumpOn,
    required this.irrigationCount,
    required this.ageMs,
    required this.ingestedAtMs,
  });

  final int seq;
  final double? temperatureC;
  final double? humidityPct;
  final bool valid; // lectura del DHT11 valida
  final double? soilMoisturePct;
  final bool soilValid;
  final int? plantId;
  final double? mlPredicted;
  final bool pumpOn;
  final int? irrigationCount;
  final int ageMs;
  final int ingestedAtMs;

  factory LatestReading.fromJson(Map<String, dynamic> json) => LatestReading(
        seq: _i(json['seq']) ?? 0,
        temperatureC: _d(json['temperature_c']),
        humidityPct: _d(json['humidity_pct']),
        valid: json['valid'] as bool? ?? false,
        soilMoisturePct: _d(json['soil_moisture_pct']),
        soilValid: json['soil_valid'] as bool? ?? false,
        plantId: _i(json['plant_id']),
        mlPredicted: _d(json['ml_predicted']),
        pumpOn: json['pump_on'] as bool? ?? false,
        irrigationCount: _i(json['irrigation_count']),
        ageMs: _i(json['age_ms']) ?? 0,
        ingestedAtMs: _i(json['ingested_at_ms']) ?? 0,
      );
}

class IrrigationRecord {
  const IrrigationRecord({
    required this.seq,
    required this.plantId,
    required this.soilMoisturePct,
    required this.temperatureC,
    required this.mlPredicted,
    required this.mlDelivered,
    required this.durationMs,
    required this.ingestedAtMs,
  });

  final int seq;
  final int plantId;
  final double soilMoisturePct; // antes de regar
  final double temperatureC;
  final double mlPredicted;
  final double mlDelivered;
  final int durationMs;
  final int ingestedAtMs;

  factory IrrigationRecord.fromJson(Map<String, dynamic> json) =>
      IrrigationRecord(
        seq: _i(json['seq']) ?? 0,
        plantId: _i(json['plant_id']) ?? 0,
        soilMoisturePct: _d(json['soil_moisture_pct']) ?? 0,
        temperatureC: _d(json['temperature_c']) ?? 0,
        mlPredicted: _d(json['ml_predicted']) ?? 0,
        mlDelivered: _d(json['ml_delivered']) ?? 0,
        durationMs: _i(json['duration_ms']) ?? 0,
        ingestedAtMs: _i(json['ingested_at_ms']) ?? 0,
      );
}

class IrrigationSummary {
  const IrrigationSummary({this.last, this.count24h = 0, this.ml24h = 0});

  final IrrigationRecord? last;
  final int count24h;
  final double ml24h;

  factory IrrigationSummary.fromJson(Map<String, dynamic> json) =>
      IrrigationSummary(
        last: json['last'] == null
            ? null
            : IrrigationRecord.fromJson(json['last'] as Map<String, dynamic>),
        count24h: _i(json['count_24h']) ?? 0,
        ml24h: _d(json['ml_24h']) ?? 0,
      );
}

class Dashboard {
  const Dashboard({
    required this.thingName,
    required this.serverTimeMs,
    required this.online,
    required this.latest,
    required this.plant,
    required this.irrigation,
  });

  final String thingName;
  final int serverTimeMs;
  final bool online;
  final LatestReading? latest;
  final PlantInfo? plant;
  final IrrigationSummary irrigation;

  factory Dashboard.fromJson(Map<String, dynamic> json) => Dashboard(
        thingName: json['thingName'] as String? ?? '',
        serverTimeMs: _i(json['server_time_ms']) ?? 0,
        online: json['online'] as bool? ?? false,
        latest: json['latest'] == null
            ? null
            : LatestReading.fromJson(json['latest'] as Map<String, dynamic>),
        plant: json['plant'] == null
            ? null
            : PlantInfo.fromJson(json['plant'] as Map<String, dynamic>),
        irrigation: json['irrigation'] == null
            ? const IrrigationSummary()
            : IrrigationSummary.fromJson(
                json['irrigation'] as Map<String, dynamic>),
      );
}

class HistoryPoint {
  const HistoryPoint({
    required this.ingestedAtMs,
    required this.temperatureC,
    required this.humidityPct,
    required this.soilMoisturePct,
    required this.pumpOn,
  });

  final int ingestedAtMs;
  final double? temperatureC;
  final double? humidityPct;
  final double? soilMoisturePct;
  final bool pumpOn;

  factory HistoryPoint.fromJson(Map<String, dynamic> json) => HistoryPoint(
        ingestedAtMs: _i(json['ingested_at_ms']) ?? 0,
        temperatureC: _d(json['temperature_c']),
        humidityPct: _d(json['humidity_pct']),
        soilMoisturePct: _d(json['soil_moisture_pct']),
        pumpOn: json['pump_on'] as bool? ?? false,
      );
}
