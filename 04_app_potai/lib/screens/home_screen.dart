import 'package:flutter/material.dart';

import '../controller.dart';
import '../format.dart';
import '../models.dart';
import '../theme.dart';
import '../widgets.dart';

/// Umbrales simples para explicar el estado del suelo al usuario.
const soilDryBelow = 30.0;
const soilWetAbove = 65.0;

({String label, Color color}) soilState(double? pct) {
  if (pct == null) return (label: 'Sin lectura del suelo', color: Colors.grey);
  if (pct < soilDryBelow) return (label: 'Suelo seco', color: potaiRed);
  if (pct > soilWetAbove) return (label: 'Suelo muy húmedo', color: potaiBlue);
  return (label: 'Humedad buena', color: potaiGreen);
}

class HomeScreen extends StatelessWidget {
  const HomeScreen({
    super.key,
    required this.controller,
    required this.onOpenSettings,
  });

  final PotaiController controller;
  final VoidCallback onOpenSettings;

  @override
  Widget build(BuildContext context) {
    return ListenableBuilder(
      listenable: controller,
      builder: (context, _) {
        if (!controller.settings.isConfigured) {
          return _Welcome(controller: controller, onOpenSettings: onOpenSettings);
        }
        final dash = controller.dashboard;
        if (dash == null) {
          if (controller.error != null) {
            return _ErrorView(
              message: controller.error!,
              onRetry: controller.refresh,
              onOpenSettings: onOpenSettings,
            );
          }
          return const Center(child: CircularProgressIndicator());
        }
        return RefreshIndicator(
          onRefresh: controller.refresh,
          child: ListView(
            physics: const AlwaysScrollableScrollPhysics(),
            padding: const EdgeInsets.fromLTRB(16, 4, 16, 24),
            children: [
              if (controller.error != null) _Banner(text: controller.error!),
              _PlantHeader(dashboard: dash),
              const SizedBox(height: 16),
              if (dash.latest == null)
                const _WaitingForData()
              else ...[
                _SoilCard(latest: dash.latest!),
                const SizedBox(height: 12),
                _AmbientRow(latest: dash.latest!, plant: dash.plant),
                const SizedBox(height: 12),
                _AiCard(latest: dash.latest!),
                const SizedBox(height: 12),
                _IrrigationCard(summary: dash.irrigation, serverTimeMs: dash.serverTimeMs),
                if (dash.plant != null) ...[
                  const SizedBox(height: 12),
                  _CareCard(plant: dash.plant!),
                ],
              ],
            ],
          ),
        );
      },
    );
  }
}

class _PlantHeader extends StatelessWidget {
  const _PlantHeader({required this.dashboard});
  final Dashboard dashboard;

  @override
  Widget build(BuildContext context) {
    final text = Theme.of(context).textTheme;
    final plant = dashboard.plant;
    final latest = dashboard.latest;
    final online = dashboard.online;
    return Card(
      child: Container(
        decoration: BoxDecoration(
          borderRadius: BorderRadius.circular(24),
          gradient: const LinearGradient(
            colors: [Color(0xFFE8F5E9), Colors.white],
            begin: Alignment.topLeft,
            end: Alignment.bottomRight,
          ),
        ),
        padding: const EdgeInsets.all(20),
        child: Row(
          children: [
            Text(plant?.emoji ?? '🪴', style: const TextStyle(fontSize: 56)),
            const SizedBox(width: 16),
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    plant?.displayName ?? 'Tu planta',
                    style: text.headlineSmall?.copyWith(fontWeight: FontWeight.w800),
                  ),
                  if (plant != null)
                    Text(
                      plant.scientificName,
                      style: text.bodyMedium?.copyWith(
                        fontStyle: FontStyle.italic,
                        color: Colors.black54,
                      ),
                    ),
                  const SizedBox(height: 10),
                  Wrap(
                    spacing: 8,
                    runSpacing: 6,
                    crossAxisAlignment: WrapCrossAlignment.center,
                    children: [
                      StatusChip(
                        text: online ? 'En línea' : 'Sin conexión',
                        color: online ? potaiGreen : Colors.grey,
                        icon: online ? Icons.wifi_rounded : Icons.wifi_off_rounded,
                      ),
                      if (latest != null)
                        Text(
                          'Dato ${timeAgoFromMs(latest.ageMs)}',
                          style: text.bodySmall?.copyWith(color: Colors.black45),
                        ),
                    ],
                  ),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }
}

class _SoilCard extends StatelessWidget {
  const _SoilCard({required this.latest});
  final LatestReading latest;

  @override
  Widget build(BuildContext context) {
    final soil = latest.soilValid ? latest.soilMoisturePct : null;
    final state = soilState(soil);
    return Card(
      child: Padding(
        padding: const EdgeInsets.symmetric(vertical: 24, horizontal: 16),
        child: Column(
          children: [
            SoilGauge(percent: soil, color: state.color),
            const SizedBox(height: 16),
            StatusChip(text: state.label, color: state.color),
          ],
        ),
      ),
    );
  }
}

class _AmbientRow extends StatelessWidget {
  const _AmbientRow({required this.latest, required this.plant});
  final LatestReading latest;
  final PlantInfo? plant;

  @override
  Widget build(BuildContext context) {
    final ok = latest.valid;
    return Row(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Expanded(
          child: MetricTile(
            icon: Icons.thermostat_rounded,
            label: 'Temperatura',
            value: ok ? fmt(latest.temperatureC, decimals: 0, unit: ' °C') : '—',
            color: potaiOrange,
            footnote: ok ? null : 'Sensor sin lectura',
          ),
        ),
        const SizedBox(width: 12),
        Expanded(
          child: MetricTile(
            icon: Icons.cloud_rounded,
            label: 'Humedad ambiente',
            value: ok ? fmt(latest.humidityPct, decimals: 0, unit: ' %') : '—',
            color: potaiBlue,
            footnote: plant == null ? null : 'Ideal: ${plant!.optimalHumidity} %',
          ),
        ),
      ],
    );
  }
}

class _AiCard extends StatelessWidget {
  const _AiCard({required this.latest});
  final LatestReading latest;

  @override
  Widget build(BuildContext context) {
    final text = Theme.of(context).textTheme;
    final ml = latest.mlPredicted;
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(20),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                const Icon(Icons.auto_awesome_rounded, color: potaiGreen),
                const SizedBox(width: 8),
                Text('Recomendación de la IA',
                    style: text.titleMedium?.copyWith(fontWeight: FontWeight.w800)),
              ],
            ),
            const SizedBox(height: 12),
            Row(
              crossAxisAlignment: CrossAxisAlignment.end,
              children: [
                Text(
                  ml == null ? '—' : ml.round().toString(),
                  style: text.displaySmall?.copyWith(fontWeight: FontWeight.w800),
                ),
                const SizedBox(width: 6),
                Padding(
                  padding: const EdgeInsets.only(bottom: 6),
                  child: Text('ml de agua', style: text.titleMedium),
                ),
              ],
            ),
            const SizedBox(height: 4),
            Text(
              (ml ?? 0) <= 0
                  ? 'Ahora mismo la planta no necesita riego.'
                  : 'Es lo que el modelo calcula según el suelo, la temperatura y el tipo de planta.',
              style: text.bodyMedium?.copyWith(color: Colors.black54),
            ),
            const SizedBox(height: 14),
            StatusChip(
              text: latest.pumpOn ? 'Regando ahora' : 'Bomba apagada',
              color: latest.pumpOn ? potaiBlue : Colors.grey,
              icon: latest.pumpOn ? Icons.water_rounded : Icons.power_settings_new_rounded,
            ),
          ],
        ),
      ),
    );
  }
}

class _IrrigationCard extends StatelessWidget {
  const _IrrigationCard({required this.summary, required this.serverTimeMs});
  final IrrigationSummary summary;
  final int serverTimeMs;

  @override
  Widget build(BuildContext context) {
    final text = Theme.of(context).textTheme;
    final last = summary.last;
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(20),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                const Icon(Icons.history_rounded, color: potaiBlue),
                const SizedBox(width: 8),
                Text('Riegos',
                    style: text.titleMedium?.copyWith(fontWeight: FontWeight.w800)),
              ],
            ),
            const SizedBox(height: 12),
            if (last == null)
              Text('Todavía no se ha registrado ningún riego.',
                  style: text.bodyMedium?.copyWith(color: Colors.black54))
            else ...[
              Text(
                'Último riego: ${timeAgoFromMs(serverTimeMs - last.ingestedAtMs)}',
                style: text.bodyLarge?.copyWith(fontWeight: FontWeight.w600),
              ),
              const SizedBox(height: 2),
              Text(
                '${last.mlDelivered.round()} ml · el suelo estaba al ${last.soilMoisturePct.round()}%',
                style: text.bodyMedium?.copyWith(color: Colors.black54),
              ),
              const SizedBox(height: 10),
              Text(
                'Últimas 24 h: ${summary.count24h} ${summary.count24h == 1 ? 'riego' : 'riegos'} · ${summary.ml24h.round()} ml',
                style: text.bodyMedium,
              ),
            ],
          ],
        ),
      ),
    );
  }
}

class _CareCard extends StatelessWidget {
  const _CareCard({required this.plant});
  final PlantInfo plant;

  @override
  Widget build(BuildContext context) {
    final text = Theme.of(context).textTheme;
    Widget item(IconData icon, String value, String label) => Expanded(
          child: Column(
            children: [
              Icon(icon, color: potaiGreen),
              const SizedBox(height: 6),
              Text(value, style: text.titleMedium?.copyWith(fontWeight: FontWeight.w800)),
              Text(label,
                  textAlign: TextAlign.center,
                  style: text.bodySmall?.copyWith(color: Colors.black54)),
            ],
          ),
        );
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(20),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text('Cuidados de ${plant.displayName.toLowerCase()}',
                style: text.titleMedium?.copyWith(fontWeight: FontWeight.w800)),
            const SizedBox(height: 16),
            Row(
              children: [
                item(Icons.water_drop_outlined, 'cada ${plant.waterEveryDays} d', 'riego'),
                item(Icons.wb_sunny_outlined, '${plant.lightHours} h', 'luz al día'),
                item(Icons.cloud_outlined, '${plant.optimalHumidity} %', 'humedad ideal'),
              ],
            ),
          ],
        ),
      ),
    );
  }
}

class _WaitingForData extends StatelessWidget {
  const _WaitingForData();

  @override
  Widget build(BuildContext context) {
    return const Card(
      child: Padding(
        padding: EdgeInsets.all(24),
        child: Column(
          children: [
            Icon(Icons.hourglass_empty_rounded, size: 40, color: Colors.black38),
            SizedBox(height: 12),
            Text('Esperando datos del dispositivo',
                style: TextStyle(fontWeight: FontWeight.w800, fontSize: 16)),
            SizedBox(height: 6),
            Text(
              'Cuando la ESP32 se conecte a internet y envíe su primera lectura, aparecerá aquí.',
              textAlign: TextAlign.center,
              style: TextStyle(color: Colors.black54),
            ),
          ],
        ),
      ),
    );
  }
}

class _Banner extends StatelessWidget {
  const _Banner({required this.text});
  final String text;

  @override
  Widget build(BuildContext context) {
    return Container(
      margin: const EdgeInsets.only(bottom: 12),
      padding: const EdgeInsets.all(12),
      decoration: BoxDecoration(
        color: potaiOrange.withValues(alpha: 0.12),
        borderRadius: BorderRadius.circular(16),
      ),
      child: Row(
        children: [
          const Icon(Icons.warning_amber_rounded, color: potaiOrange),
          const SizedBox(width: 10),
          Expanded(child: Text('$text Mostrando los últimos datos recibidos.')),
        ],
      ),
    );
  }
}

class _ErrorView extends StatelessWidget {
  const _ErrorView({
    required this.message,
    required this.onRetry,
    required this.onOpenSettings,
  });

  final String message;
  final Future<void> Function() onRetry;
  final VoidCallback onOpenSettings;

  @override
  Widget build(BuildContext context) {
    return Center(
      child: Padding(
        padding: const EdgeInsets.all(32),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            const Icon(Icons.cloud_off_rounded, size: 56, color: Colors.black38),
            const SizedBox(height: 16),
            Text(message, textAlign: TextAlign.center, style: Theme.of(context).textTheme.titleMedium),
            const SizedBox(height: 20),
            FilledButton.icon(
              onPressed: onRetry,
              icon: const Icon(Icons.refresh_rounded),
              label: const Text('Reintentar'),
            ),
            TextButton(onPressed: onOpenSettings, child: const Text('Revisar ajustes')),
          ],
        ),
      ),
    );
  }
}

class _Welcome extends StatelessWidget {
  const _Welcome({required this.controller, required this.onOpenSettings});
  final PotaiController controller;
  final VoidCallback onOpenSettings;

  @override
  Widget build(BuildContext context) {
    final text = Theme.of(context).textTheme;
    return ListView(
      padding: const EdgeInsets.all(24),
      children: [
        const SizedBox(height: 24),
        const Center(child: Text('🪴', style: TextStyle(fontSize: 80))),
        const SizedBox(height: 16),
        Text('¡Bienvenido a PotAI!',
            textAlign: TextAlign.center,
            style: text.headlineMedium?.copyWith(fontWeight: FontWeight.w800)),
        const SizedBox(height: 8),
        Text(
          'Mira en tiempo real cómo está tu planta y cuándo la riega la inteligencia artificial.',
          textAlign: TextAlign.center,
          style: text.bodyLarge?.copyWith(color: Colors.black54),
        ),
        const SizedBox(height: 32),
        FilledButton.icon(
          onPressed: onOpenSettings,
          icon: const Icon(Icons.settings_rounded),
          label: const Text('Conectar mi dispositivo'),
        ),
        const SizedBox(height: 8),
        OutlinedButton.icon(
          onPressed: () => controller.applySettings(controller.settings.copyWith(demo: true)),
          icon: const Icon(Icons.play_circle_outline_rounded),
          label: const Text('Ver con datos de ejemplo'),
        ),
      ],
    );
  }
}
