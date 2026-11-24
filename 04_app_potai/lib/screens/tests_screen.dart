import 'package:flutter/material.dart';

import '../controller.dart';
import '../theme.dart';

/// Pantalla para inyectar lecturas. El ESP32 deja de muestrear sensores
/// mientras el modo test está activo.
class TestsScreen extends StatefulWidget {
  const TestsScreen({super.key, required this.controller});

  final PotaiController controller;

  @override
  State<TestsScreen> createState() => _TestsScreenState();
}

class _TestsScreenState extends State<TestsScreen> {
  bool _enabled = false;
  double _temp = 25;
  double _hum = 58;
  double _soil = 30;
  bool _seeded = false;
  bool _touched = false;
  bool _draggingSoil = false;
  bool _busy = false;

  @override
  void initState() {
    super.initState();
    widget.controller.addListener(_onDevice);
    _onDevice();
  }

  @override
  void dispose() {
    widget.controller.removeListener(_onDevice);
    super.dispose();
  }

  void _onDevice() {
    final latest = widget.controller.dashboard?.latest;
    if (!mounted || latest == null || _busy) return;
    if (!_seeded) {
      _seeded = true;
      if (_touched) return;
      setState(() {
        _enabled = latest.testMode;
        _temp = (latest.temperatureC ?? _temp).clamp(0, 50);
        _hum = (latest.humidityPct ?? _hum).clamp(0, 100);
        _soil = (latest.soilMoisturePct ?? _soil).clamp(0, 100);
      });
      return;
    }
    final soil = latest.soilMoisturePct;
    if (_enabled && !_draggingSoil && soil != null && soil > _soil + 5) {
      setState(() => _soil = soil.clamp(0, 100));
    }
  }

  Future<void> _push() async {
    setState(() => _busy = true);
    final message = await widget.controller.applyTest(
      enabled: _enabled,
      temperatureC: _temp,
      humidityPct: _hum,
      soilMoisturePct: _soil,
    );
    if (!mounted) return;
    setState(() => _busy = false);
    ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(message)));
  }

  @override
  Widget build(BuildContext context) {
    final text = Theme.of(context).textTheme;
    final ready = widget.controller.settings.isConfigured;
    return ListView(
      padding: const EdgeInsets.fromLTRB(16, 8, 16, 24),
      children: [
        Card(
          child: SwitchListTile(
            value: _enabled,
            onChanged: !ready || _busy
                ? null
                : (on) {
                    _touched = true;
                    setState(() => _enabled = on);
                    _push();
                  },
            secondary: Icon(
              _enabled ? Icons.science_rounded : Icons.sensors_rounded,
              color: _enabled ? potaiOrange : potaiGreen,
            ),
            title: Text(_enabled ? 'Modo test' : 'Modo real',
                style: text.titleMedium?.copyWith(fontWeight: FontWeight.w800)),
            subtitle: Text(
              _enabled
                  ? 'La maceta no lee el DHT11 ni el suelo. Usa los valores de abajo.'
                  : 'La maceta lee sus sensores. Activa el test para calibrar un escenario.',
            ),
          ),
        ),
        const SizedBox(height: 12),
        if (!ready)
          const Card(
            child: Padding(
              padding: EdgeInsets.all(20),
              child: Text('Conecta el dispositivo en Ajustes para enviar el modo test.'),
            ),
          )
        else
          Card(
            child: Padding(
              padding: const EdgeInsets.fromLTRB(20, 16, 20, 8),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text('Escenario', style: text.titleMedium?.copyWith(fontWeight: FontWeight.w800)),
                  const SizedBox(height: 4),
                  Text(
                    'Al soltar cada control se envía a la maceta. Si riega en modo test, el suelo sube solo para simular el agua.',
                    style: text.bodySmall?.copyWith(color: Colors.black54),
                  ),
                  _slider(
                    label: 'Temperatura',
                    value: _temp,
                    unit: '°C',
                    max: 50,
                    enabled: _enabled && !_busy,
                    onChanged: (v) {
                      _touched = true;
                      setState(() => _temp = v);
                    },
                    onChangeEnd: (_) => _push(),
                  ),
                  _slider(
                    label: 'Humedad ambiente',
                    value: _hum,
                    unit: '%',
                    max: 100,
                    enabled: _enabled && !_busy,
                    onChanged: (v) {
                      _touched = true;
                      setState(() => _hum = v);
                    },
                    onChangeEnd: (_) => _push(),
                  ),
                  _slider(
                    label: 'Humedad del suelo',
                    value: _soil,
                    unit: '%',
                    max: 100,
                    enabled: _enabled && !_busy,
                    onChanged: (v) {
                      _touched = true;
                      setState(() => _soil = v);
                    },
                    onChangeStart: (_) => _draggingSoil = true,
                    onChangeEnd: (_) {
                      _draggingSoil = false;
                      _push();
                    },
                  ),
                ],
              ),
            ),
          ),
      ],
    );
  }

  Widget _slider({
    required String label,
    required double value,
    required String unit,
    required double max,
    required bool enabled,
    required ValueChanged<double> onChanged,
    ValueChanged<double>? onChangeStart,
    required ValueChanged<double> onChangeEnd,
  }) {
    final shown = value.round();
    return Padding(
      padding: const EdgeInsets.only(top: 8),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Expanded(child: Text(label)),
              Text('$shown $unit', style: const TextStyle(fontWeight: FontWeight.w800)),
            ],
          ),
          Slider(
            value: value.clamp(0, max),
            min: 0,
            max: max,
            divisions: max.round(),
            label: '$shown $unit',
            onChanged: enabled ? onChanged : null,
            onChangeStart: enabled ? onChangeStart : null,
            onChangeEnd: enabled ? onChangeEnd : null,
          ),
        ],
      ),
    );
  }
}
