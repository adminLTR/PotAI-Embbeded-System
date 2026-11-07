// Utilidades de formato (sin dependencias externas).

String two(int n) => n.toString().padLeft(2, '0');

/// "hace 5 min" a partir de los milisegundos transcurridos.
String timeAgoFromMs(int elapsedMs) {
  final s = elapsedMs ~/ 1000;
  if (s < 5) return 'ahora mismo';
  if (s < 60) return 'hace $s s';
  final m = s ~/ 60;
  if (m < 60) return 'hace $m min';
  final h = m ~/ 60;
  if (h < 24) return 'hace $h h';
  return 'hace ${h ~/ 24} d';
}

/// "07/11 14:32" en hora local.
String formatDateTime(int epochMs) {
  final d = DateTime.fromMillisecondsSinceEpoch(epochMs);
  return '${two(d.day)}/${two(d.month)} ${two(d.hour)}:${two(d.minute)}';
}

String formatHour(int epochMs) {
  final d = DateTime.fromMillisecondsSinceEpoch(epochMs);
  return '${two(d.hour)}:${two(d.minute)}';
}

String fmt(double? value, {int decimals = 0, String unit = ''}) =>
    value == null ? '—' : '${value.toStringAsFixed(decimals)}$unit';

String formatSeconds(int ms) {
  final s = ms / 1000;
  return s >= 10 ? '${s.round()} s' : '${s.toStringAsFixed(1)} s';
}
