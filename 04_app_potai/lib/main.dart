import 'package:flutter/material.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'controller.dart';
import 'screens/history_screen.dart';
import 'screens/home_screen.dart';
import 'screens/settings_screen.dart';
import 'theme.dart';

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  final prefs = await SharedPreferences.getInstance();
  final controller = PotaiController(store: SettingsStore(prefs))..start();
  runApp(PotaiApp(controller: controller));
}

class PotaiApp extends StatelessWidget {
  const PotaiApp({super.key, required this.controller});

  final PotaiController controller;

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'PotAI',
      debugShowCheckedModeBanner: false,
      theme: buildTheme(),
      home: HomeShell(controller: controller),
    );
  }
}

class HomeShell extends StatefulWidget {
  const HomeShell({super.key, required this.controller});

  final PotaiController controller;

  @override
  State<HomeShell> createState() => _HomeShellState();
}

class _HomeShellState extends State<HomeShell> {
  int _index = 0;

  void _go(int index) => setState(() => _index = index);

  @override
  Widget build(BuildContext context) {
    final c = widget.controller;
    return Scaffold(
      appBar: AppBar(
        title: const Text('🌱 PotAI'),
        actions: [
          IconButton(
            tooltip: 'Actualizar',
            onPressed: c.refresh,
            icon: const Icon(Icons.refresh_rounded),
          ),
        ],
      ),
      body: IndexedStack(
        index: _index,
        children: [
          HomeScreen(controller: c, onOpenSettings: () => _go(2)),
          HistoryScreen(controller: c),
          SettingsScreen(controller: c, onConnected: () => _go(0)),
        ],
      ),
      bottomNavigationBar: NavigationBar(
        selectedIndex: _index,
        onDestinationSelected: _go,
        destinations: const [
          NavigationDestination(
            icon: Icon(Icons.eco_outlined),
            selectedIcon: Icon(Icons.eco_rounded),
            label: 'Inicio',
          ),
          NavigationDestination(
            icon: Icon(Icons.show_chart_rounded),
            selectedIcon: Icon(Icons.show_chart_rounded),
            label: 'Historial',
          ),
          NavigationDestination(
            icon: Icon(Icons.settings_outlined),
            selectedIcon: Icon(Icons.settings_rounded),
            label: 'Ajustes',
          ),
        ],
      ),
    );
  }
}
