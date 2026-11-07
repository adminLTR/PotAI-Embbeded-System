import 'package:fl_chart/fl_chart.dart';
import 'package:flutter/material.dart';

import '../controller.dart';
import '../format.dart';
import '../models.dart';
import '../theme.dart';
import '../widgets.dart';

enum Series {
  soil('Suelo', Icons.water_drop_rounded, '%', potaiGreen, 0, 100),
  air('Ambiente', Icons.cloud_rounded, '%', potaiBlue, 0, 100),
  temperature('Temperatura', Icons.thermostat_rounded, '°C', potaiOrange, null, null);

  const Series(this.label, this.icon, this.unit, this.color, this.fixedMin, this.fixedMax);
  final String label;
  final IconData icon;
  final String unit;
  final Color color;
  final double? fixedMin;
  final double? fixedMax;

  double? valueOf(HistoryPoint p) => switch (this) {
        Series.soil => p.soilMoisturePct,
        Series.air => p.humidityPct,
        Series.temperature => p.temperatureC,
      };
}

class HistoryScreen extends StatefulWidget {
  const HistoryScreen({super.key, required this.controller});
  final PotaiController controller;

  @override
  State<HistoryScreen> createState() => _HistoryScreenState();
}

class _HistoryScreenState extends State<HistoryScreen> {
  Series _series = Series.soil;

  @override
  Widget build(BuildContext context) {
    final c = widget.controller;
    return ListenableBuilder(
      listenable: c,
      builder: (context, _) {
        if (!c.settings.isConfigured) {
          return const Center(
            child: Padding(
              padding: EdgeInsets.all(32),
              child: Text(
                'Conecta tu dispositivo en Ajustes para ver el historial.',
                textAlign: TextAlign.center,
              ),
            ),
          );
        }
        return RefreshIndicator(
          onRefresh: c.refresh,
          child: ListView(
            physics: const AlwaysScrollableScrollPhysics(),
            padding: const EdgeInsets.fromLTRB(16, 4, 16, 24),
            children: [
              const SectionTitle('Lecturas recientes'),
              Wrap(
                spacing: 8,
                children: [
                  for (final s in Series.values)
                    ChoiceChip(
                      avatar: Icon(s.icon, size: 18, color: _series == s ? Colors.white : s.color),
                      label: Text(s.label),
                      selected: _series == s,
                      selectedColor: s.color,
                      labelStyle: TextStyle(
                        color: _series == s ? Colors.white : null,
                        fontWeight: FontWeight.w600,
                      ),
                      showCheckmark: false,
                      onSelected: (_) => setState(() => _series = s),
                    ),
                ],
              ),
              const SizedBox(height: 12),
              Card(
                child: Padding(
                  padding: const EdgeInsets.fromLTRB(8, 20, 20, 12),
                  child: SizedBox(
                    height: 240,
                    child: _Chart(points: c.history, series: _series),
                  ),
                ),
              ),
              const SizedBox(height: 16),
              const SectionTitle('Historial de riegos'),
              if (c.irrigations.isEmpty)
                const Card(
                  child: Padding(
                    padding: EdgeInsets.all(24),
                    child: Center(child: Text('Aún no hay riegos registrados.')),
                  ),
                )
              else
                for (final r in c.irrigations) ...[
                  _IrrigationTile(record: r),
                  const SizedBox(height: 8),
                ],
            ],
          ),
        );
      },
    );
  }
}

class _Chart extends StatelessWidget {
  const _Chart({required this.points, required this.series});

  final List<HistoryPoint> points;
  final Series series;

  @override
  Widget build(BuildContext context) {
    // La API devuelve lo mas nuevo primero; el grafico va de viejo a nuevo.
    final data = points.reversed
        .where((p) => series.valueOf(p) != null)
        .toList(growable: false);
    if (data.length < 2) {
      return const Center(
        child: Text('Aún no hay suficientes lecturas para graficar.'),
      );
    }

    final firstMs = data.first.ingestedAtMs;
    final spots = [
      for (final p in data)
        FlSpot((p.ingestedAtMs - firstMs) / 60000.0, series.valueOf(p)!),
    ];
    final spanMin = spots.last.x <= 0 ? 1.0 : spots.last.x;

    final values = spots.map((s) => s.y);
    final minV = values.reduce((a, b) => a < b ? a : b);
    final maxV = values.reduce((a, b) => a > b ? a : b);
    final minY = series.fixedMin ?? (minV - 2).floorToDouble();
    final maxY = series.fixedMax ?? (maxV + 2).ceilToDouble();

    return LineChart(
      LineChartData(
        minX: 0,
        maxX: spanMin,
        minY: minY,
        maxY: maxY,
        gridData: FlGridData(
          drawVerticalLine: false,
          getDrawingHorizontalLine: (_) =>
              FlLine(color: Colors.black12, strokeWidth: 1),
        ),
        borderData: FlBorderData(show: false),
        titlesData: FlTitlesData(
          topTitles: const AxisTitles(),
          rightTitles: const AxisTitles(),
          leftTitles: AxisTitles(
            sideTitles: SideTitles(
              showTitles: true,
              reservedSize: 40,
              getTitlesWidget: (value, meta) => SideTitleWidget(
                meta: meta,
                child: Text('${value.round()}',
                    style: const TextStyle(fontSize: 11, color: Colors.black54)),
              ),
            ),
          ),
          bottomTitles: AxisTitles(
            sideTitles: SideTitles(
              showTitles: true,
              reservedSize: 28,
              interval: spanMin / 3,
              getTitlesWidget: (value, meta) {
                if (value == meta.min && value != 0) return const SizedBox.shrink();
                final ms = firstMs + (value * 60000).round();
                return SideTitleWidget(
                  meta: meta,
                  child: Text(formatHour(ms),
                      style: const TextStyle(fontSize: 11, color: Colors.black54)),
                );
              },
            ),
          ),
        ),
        lineTouchData: LineTouchData(
          touchTooltipData: LineTouchTooltipData(
            getTooltipItems: (touched) => [
              for (final t in touched)
                LineTooltipItem(
                  '${t.y.toStringAsFixed(1)} ${series.unit}\n${formatHour(firstMs + (t.x * 60000).round())}',
                  const TextStyle(color: Colors.white, fontWeight: FontWeight.w700),
                ),
            ],
          ),
        ),
        lineBarsData: [
          LineChartBarData(
            spots: spots,
            isCurved: true,
            curveSmoothness: 0.2,
            preventCurveOverShooting: true,
            color: series.color,
            barWidth: 3,
            dotData: const FlDotData(show: false),
            belowBarData: BarAreaData(
              show: true,
              color: series.color.withValues(alpha: 0.12),
            ),
          ),
        ],
      ),
    );
  }
}

class _IrrigationTile extends StatelessWidget {
  const _IrrigationTile({required this.record});
  final IrrigationRecord record;

  @override
  Widget build(BuildContext context) {
    final text = Theme.of(context).textTheme;
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Row(
          children: [
            Container(
              padding: const EdgeInsets.all(10),
              decoration: BoxDecoration(
                color: potaiBlue.withValues(alpha: 0.12),
                borderRadius: BorderRadius.circular(14),
              ),
              child: const Icon(Icons.water_drop_rounded, color: potaiBlue),
            ),
            const SizedBox(width: 14),
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text('${record.mlDelivered.round()} ml',
                      style: text.titleMedium?.copyWith(fontWeight: FontWeight.w800)),
                  Text(
                    'IA pidió ${record.mlPredicted.round()} ml · bomba ${formatSeconds(record.durationMs)}',
                    style: text.bodySmall?.copyWith(color: Colors.black54),
                  ),
                  Text(
                    'Suelo ${record.soilMoisturePct.round()}% · ${record.temperatureC.round()} °C',
                    style: text.bodySmall?.copyWith(color: Colors.black54),
                  ),
                ],
              ),
            ),
            Text(formatDateTime(record.ingestedAtMs),
                style: text.bodySmall?.copyWith(color: Colors.black45)),
          ],
        ),
      ),
    );
  }
}
