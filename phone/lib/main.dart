import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import 'services/webrtc_service.dart';
import 'services/input_service.dart';
import 'screens/remote_desktop_screen.dart';

void main() {
  WidgetsFlutterBinding.ensureInitialized();
  runApp(const PhoneCtrlApp());
}

class PhoneCtrlApp extends StatelessWidget {
  const PhoneCtrlApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MultiProvider(
      providers: [
        ChangeNotifierProvider(create: (_) => WebRtcService()),
        ChangeNotifierProvider(create: (_) => InputService()),
      ],
      child: MaterialApp(
        title: 'phoneCtrl',
        debugShowCheckedModeBanner: false,
        theme: ThemeData(
          colorScheme: ColorScheme.fromSeed(
            seedColor: Colors.blueAccent,
            brightness: Brightness.dark,
          ),
          useMaterial3: true,
        ),
        home: const ConnectPage(),
      ),
    );
  }
}

/// Landing page: asks the user for the laptop's IP and port then navigates
/// to the [RemoteDesktopScreen].
class ConnectPage extends StatefulWidget {
  const ConnectPage({super.key});

  @override
  State<ConnectPage> createState() => _ConnectPageState();
}

class _ConnectPageState extends State<ConnectPage> {
  final _formKey  = GlobalKey<FormState>();
  final _hostCtrl = TextEditingController(text: '192.168.1.100');
  final _portCtrl = TextEditingController(text: '8080');
  bool _connecting = false;

  @override
  void dispose() {
    _hostCtrl.dispose();
    _portCtrl.dispose();
    super.dispose();
  }

  Future<void> _connect() async {
    if (!_formKey.currentState!.validate()) return;

    setState(() => _connecting = true);

    final host = _hostCtrl.text.trim();
    final port = int.parse(_portCtrl.text.trim());

    final webrtc = context.read<WebRtcService>();
    final input  = context.read<InputService>();

    final ok = await webrtc.connect(host: host, port: port);
    input.attachToWebRtc(webrtc);

    setState(() => _connecting = false);

    if (!mounted) return;

    if (ok) {
      Navigator.of(context).push(
        MaterialPageRoute(
          builder: (_) => const RemoteDesktopScreen(),
        ),
      );
    } else {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('Connection failed. Check host/port.')),
      );
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('phoneCtrl')),
      body: Center(
        child: SingleChildScrollView(
          padding: const EdgeInsets.all(32),
          child: Form(
            key: _formKey,
            child: Column(
              mainAxisSize: MainAxisSize.min,
              children: [
                const Icon(Icons.laptop, size: 80, color: Colors.blueAccent),
                const SizedBox(height: 24),
                const Text(
                  'Connect to Laptop',
                  style: TextStyle(fontSize: 22, fontWeight: FontWeight.bold),
                ),
                const SizedBox(height: 32),
                TextFormField(
                  controller: _hostCtrl,
                  decoration: const InputDecoration(
                    labelText: 'Laptop IP Address',
                    hintText: '192.168.1.100',
                    prefixIcon: Icon(Icons.computer),
                    border: OutlineInputBorder(),
                  ),
                  keyboardType: TextInputType.url,
                  validator: (v) {
                    if (v == null || v.isEmpty) return 'Enter an IP or hostname';
                    return null;
                  },
                ),
                const SizedBox(height: 16),
                TextFormField(
                  controller: _portCtrl,
                  decoration: const InputDecoration(
                    labelText: 'Port',
                    hintText: '8080',
                    prefixIcon: Icon(Icons.settings_ethernet),
                    border: OutlineInputBorder(),
                  ),
                  keyboardType: TextInputType.number,
                  validator: (v) {
                    if (v == null || v.isEmpty) return 'Enter a port number';
                    final n = int.tryParse(v);
                    if (n == null || n < 1 || n > 65535) return 'Invalid port';
                    return null;
                  },
                ),
                const SizedBox(height: 32),
                SizedBox(
                  width: double.infinity,
                  child: FilledButton.icon(
                    onPressed: _connecting ? null : _connect,
                    icon: _connecting
                        ? const SizedBox(
                            width: 18,
                            height: 18,
                            child: CircularProgressIndicator(strokeWidth: 2),
                          )
                        : const Icon(Icons.cast_connected),
                    label: Text(_connecting ? 'Connecting…' : 'Connect'),
                  ),
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}
