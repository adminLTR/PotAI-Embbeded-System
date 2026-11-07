import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'api.dart';
import 'models.dart';

class Settings {
  const Settings({this.apiBaseUrl = '', this.thingName = '', this.demo = false});

  final String apiBaseUrl;
  final String thingName;
  final bool demo;

  bool get isConfigured =>
      demo || (apiBaseUrl.isNotEmpty && thingName.isNotEmpty);

  Settings copyWith({String? apiBaseUrl, String? thingName, bool? demo}) =>
      Settings(
        apiBaseUrl: apiBaseUrl ?? this.apiBaseUrl,
        thingName: thingName ?? this.thingName,
        demo: demo ?? this.demo,
      );

  static final _thingRegex = RegExp(r'^psi3542-[A-Za-z0-9_-]+$');

  /// Devuelve un mensaje de error o null si todo esta bien.
  String? validate() {
    if (demo) return null;
    final url = apiBaseUrl.trim();
    if (!RegExp(r'^https?://[A-Za-z0-9.-]+(:\d+)?(/\S*)?$').hasMatch(url)) {
      return 'La URL debe empezar con https:// (la copias de CloudFormation → Outputs → ApiBaseUrl).';
    }
    if (!_thingRegex.hasMatch(thingName.trim())) {
      return 'El nombre del dispositivo debe ser como psi3542-g01.';
    }
    return null;
  }
}

class SettingsStore {
  SettingsStore(this._prefs);
  final SharedPreferences _prefs;

  static const _kUrl = 'potai.apiBaseUrl';
  static const _kThing = 'potai.thingName';
  static const _kDemo = 'potai.demo';

  Settings load() => Settings(
        apiBaseUrl: _prefs.getString(_kUrl) ?? '',
        thingName: _prefs.getString(_kThing) ?? '',
        demo: _prefs.getBool(_kDemo) ?? false,
      );

  Future<void> save(Settings s) async {
    await _prefs.setString(_kUrl, s.apiBaseUrl.trim());
    await _prefs.setString(_kThing, s.thingName.trim());
    await _prefs.setBool(_kDemo, s.demo);
  }
}

typedef ApiFactory = PotaiApi Function(Settings settings);

PotaiApi defaultApiFactory(Settings s) => s.demo
    ? DemoPotaiApi()
    : HttpPotaiApi(baseUrl: s.apiBaseUrl, thingName: s.thingName.trim());

/// Estado de la app: consulta la API cada pocos segundos y avisa a la UI.
class PotaiController extends ChangeNotifier {
  PotaiController({
    required this.store,
    ApiFactory? apiFactory,
    this.pollInterval = const Duration(seconds: 5),
  })  : _apiFactory = apiFactory ?? defaultApiFactory,
        settings = store.load();

  final SettingsStore store;
  final ApiFactory _apiFactory;
  final Duration pollInterval;

  Settings settings;
  Dashboard? dashboard;
  List<HistoryPoint> history = const [];
  List<IrrigationRecord> irrigations = const [];
  String? error;
  bool loading = false;
  DateTime? lastRefresh;

  Timer? _timer;
  bool _busy = false;
  bool _disposed = false;

  void start() {
    refresh();
    _timer?.cancel();
    _timer = Timer.periodic(pollInterval, (_) => refresh(silent: true));
  }

  Future<void> refresh({bool silent = false}) async {
    if (!settings.isConfigured || _busy) return;
    _busy = true;
    if (!silent) {
      loading = true;
      notifyListeners();
    }
    try {
      final api = _apiFactory(settings);
      final results = await Future.wait([
        api.dashboard(),
        api.history(limit: 100),
        api.irrigations(limit: 30),
      ]);
      dashboard = results[0] as Dashboard;
      history = results[1] as List<HistoryPoint>;
      irrigations = results[2] as List<IrrigationRecord>;
      error = null;
      lastRefresh = DateTime.now();
    } on ApiException catch (e) {
      error = e.message;
    } catch (_) {
      error = 'Ocurrió un error inesperado.';
    } finally {
      _busy = false;
      loading = false;
      if (!_disposed) notifyListeners();
    }
  }

  /// Guarda los ajustes y consulta enseguida. Devuelve null si salio bien o
  /// el mensaje de error.
  Future<String?> applySettings(Settings next) async {
    final problem = next.validate();
    if (problem != null) return problem;
    await store.save(next);
    settings = next;
    dashboard = null;
    history = const [];
    irrigations = const [];
    error = null;
    await refresh();
    return error;
  }

  @override
  void dispose() {
    _disposed = true;
    _timer?.cancel();
    super.dispose();
  }
}
