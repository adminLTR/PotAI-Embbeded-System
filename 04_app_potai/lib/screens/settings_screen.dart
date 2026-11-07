import 'package:flutter/material.dart';

import '../controller.dart';
import '../theme.dart';
import '../widgets.dart';

class SettingsScreen extends StatefulWidget {
  const SettingsScreen({super.key, required this.controller, required this.onConnected});

  final PotaiController controller;

  /// Se llama cuando los ajustes se guardaron y la conexion funciono.
  final VoidCallback onConnected;

  @override
  State<SettingsScreen> createState() => _SettingsScreenState();
}

class _SettingsScreenState extends State<SettingsScreen> {
  late final TextEditingController _url;
  late final TextEditingController _thing;
  late bool _demo;
  bool _saving = false;
  String? _message;
  bool _messageIsError = false;

  @override
  void initState() {
    super.initState();
    final s = widget.controller.settings;
    _url = TextEditingController(text: s.apiBaseUrl);
    _thing = TextEditingController(text: s.thingName);
    _demo = s.demo;
  }

  @override
  void dispose() {
    _url.dispose();
    _thing.dispose();
    super.dispose();
  }

  Future<void> _save() async {
    setState(() {
      _saving = true;
      _message = null;
    });
    final error = await widget.controller.applySettings(
      Settings(apiBaseUrl: _url.text.trim(), thingName: _thing.text.trim(), demo: _demo),
    );
    if (!mounted) return;
    setState(() {
      _saving = false;
      _messageIsError = error != null;
      _message = error ?? '¡Conectado! Ya puedes ver tu planta.';
    });
    if (error == null) widget.onConnected();
  }

  @override
  Widget build(BuildContext context) {
    final text = Theme.of(context).textTheme;
    return ListView(
      padding: const EdgeInsets.fromLTRB(16, 4, 16, 24),
      children: [
        const SectionTitle('Conexión con tu dispositivo'),
        Card(
          child: Padding(
            padding: const EdgeInsets.all(20),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                SwitchListTile(
                  contentPadding: EdgeInsets.zero,
                  title: const Text('Modo demo'),
                  subtitle: const Text('Datos de ejemplo, sin internet'),
                  value: _demo,
                  onChanged: (v) => setState(() => _demo = v),
                ),
                const SizedBox(height: 8),
                TextField(
                  controller: _url,
                  enabled: !_demo,
                  keyboardType: TextInputType.url,
                  autocorrect: false,
                  decoration: const InputDecoration(
                    labelText: 'URL de la API (ApiBaseUrl)',
                    hintText: 'https://abc123.execute-api.sa-east-1.amazonaws.com',
                    prefixIcon: Icon(Icons.link_rounded),
                  ),
                ),
                const SizedBox(height: 12),
                TextField(
                  controller: _thing,
                  enabled: !_demo,
                  autocorrect: false,
                  decoration: const InputDecoration(
                    labelText: 'Nombre del dispositivo (Thing Name)',
                    hintText: 'psi3542-g01',
                    prefixIcon: Icon(Icons.memory_rounded),
                  ),
                ),
                const SizedBox(height: 16),
                FilledButton.icon(
                  onPressed: _saving ? null : _save,
                  icon: _saving
                      ? const SizedBox(
                          width: 18,
                          height: 18,
                          child: CircularProgressIndicator(strokeWidth: 2),
                        )
                      : const Icon(Icons.check_rounded),
                  label: Text(_saving ? 'Conectando…' : 'Guardar y conectar'),
                ),
                if (_message != null) ...[
                  const SizedBox(height: 12),
                  Text(
                    _message!,
                    style: TextStyle(
                      color: _messageIsError ? potaiRed : potaiGreen,
                      fontWeight: FontWeight.w600,
                    ),
                  ),
                ],
              ],
            ),
          ),
        ),
        const SizedBox(height: 16),
        Card(
          child: Padding(
            padding: const EdgeInsets.all(20),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text('¿De dónde saco estos datos?',
                    style: text.titleMedium?.copyWith(fontWeight: FontWeight.w800)),
                const SizedBox(height: 8),
                Text(
                  '• URL de la API: en la consola de AWS, CloudFormation → tu stack → '
                  'pestaña Salidas (Outputs) → ApiBaseUrl.\n'
                  '• Nombre del dispositivo: el mismo "Thing Name" que configuraste '
                  'en el menuconfig de la ESP32.',
                  style: text.bodyMedium?.copyWith(color: Colors.black54, height: 1.4),
                ),
              ],
            ),
          ),
        ),
      ],
    );
  }
}
