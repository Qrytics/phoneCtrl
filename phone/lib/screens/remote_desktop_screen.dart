import 'package:flutter/material.dart';
import 'package:flutter_webrtc/flutter_webrtc.dart';
import 'package:provider/provider.dart';

import '../services/webrtc_service.dart' as svc;
import '../services/input_service.dart';
import '../widgets/touch_overlay.dart';

/// [RemoteDesktopScreen] is the main view shown while connected.
///
/// It displays the laptop's screen video stream and overlays touch input
/// handling via [TouchOverlay].
class RemoteDesktopScreen extends StatefulWidget {
  const RemoteDesktopScreen({super.key});

  @override
  State<RemoteDesktopScreen> createState() => _RemoteDesktopScreenState();
}

class _RemoteDesktopScreenState extends State<RemoteDesktopScreen> {
  bool _overlayVisible = true;

  @override
  void initState() {
    super.initState();
    // Listen for disconnection and pop back to connect page
    WidgetsBinding.instance.addPostFrameCallback((_) {
      context.read<svc.WebRtcService>().addListener(_onConnectionStateChange);
    });
  }

  @override
  void dispose() {
    context.read<svc.WebRtcService>().removeListener(_onConnectionStateChange);
    super.dispose();
  }

  void _onConnectionStateChange() {
    final state = context.read<svc.WebRtcService>().connectionState;
    if (state == svc.ConnectionState.disconnected ||
        state == svc.ConnectionState.error) {
      if (mounted) Navigator.of(context).pop();
    }
  }

  Future<void> _disconnect() async {
    await context.read<svc.WebRtcService>().disconnect();
  }

  @override
  Widget build(BuildContext context) {
    final webrtc  = context.watch<svc.WebRtcService>();
    final renderer = webrtc.renderer;

    return Scaffold(
      backgroundColor: Colors.black,
      // ── AppBar ──────────────────────────────────────────────────────────────
      appBar: _overlayVisible
          ? AppBar(
              backgroundColor: Colors.black54,
              title: const Text('Remote Desktop'),
              actions: [
                // Connection status indicator
                Padding(
                  padding: const EdgeInsets.symmetric(horizontal: 8),
                  child: _ConnectionChip(state: webrtc.connectionState),
                ),
                IconButton(
                  icon: const Icon(Icons.close),
                  tooltip: 'Disconnect',
                  onPressed: _disconnect,
                ),
              ],
            )
          : null,

      // ── Body: video + touch overlay ─────────────────────────────────────────
      body: GestureDetector(
        // Single tap anywhere to show/hide the toolbar
        onDoubleTap: () => setState(() => _overlayVisible = !_overlayVisible),
        child: Stack(
          children: [
            // Video renderer (fills the entire screen)
            Positioned.fill(
              child: renderer != null
                  ? TouchOverlay(
                      child: RTCVideoView(
                        renderer,
                        objectFit: RTCVideoViewObjectFit.RTCVideoViewObjectFitContain,
                      ),
                    )
                  : _buildWaitingView(webrtc.connectionState),
            ),

            // Bottom hint bar
            if (_overlayVisible)
              Positioned(
                left: 0,
                right: 0,
                bottom: 0,
                child: Container(
                  color: Colors.black54,
                  padding: const EdgeInsets.symmetric(
                      horizontal: 16, vertical: 8),
                  child: const Text(
                    'Tap: left click  •  Long-press: right click  •  '
                    'Two-finger drag: scroll  •  Double-tap: hide toolbar',
                    style: TextStyle(color: Colors.white70, fontSize: 11),
                    textAlign: TextAlign.center,
                  ),
                ),
              ),
          ],
        ),
      ),
    );
  }

  Widget _buildWaitingView(svc.ConnectionState state) {
    String msg;
    switch (state) {
      case svc.ConnectionState.connecting:
        msg = 'Connecting…';
      case svc.ConnectionState.connected:
        msg = 'Waiting for first video frame…';
      case svc.ConnectionState.error:
        msg = 'Connection error';
      case svc.ConnectionState.disconnected:
        msg = 'Disconnected';
    }
    return Center(
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          const CircularProgressIndicator(),
          const SizedBox(height: 16),
          Text(msg, style: const TextStyle(color: Colors.white70)),
        ],
      ),
    );
  }
}

/// Small chip that reflects the current [svc.ConnectionState].
class _ConnectionChip extends StatelessWidget {
  const _ConnectionChip({required this.state});
  final svc.ConnectionState state;

  @override
  Widget build(BuildContext context) {
    Color color;
    String label;
    switch (state) {
      case svc.ConnectionState.connected:
        color = Colors.greenAccent;
        label = 'Live';
      case svc.ConnectionState.connecting:
        color = Colors.orangeAccent;
        label = 'Connecting';
      case svc.ConnectionState.error:
        color = Colors.redAccent;
        label = 'Error';
      case svc.ConnectionState.disconnected:
        color = Colors.grey;
        label = 'Off';
    }
    return Chip(
      backgroundColor: color.withOpacity(0.2),
      side: BorderSide(color: color),
      label: Text(label,
          style: TextStyle(color: color, fontSize: 12)),
      padding: EdgeInsets.zero,
      materialTapTargetSize: MaterialTapTargetSize.shrinkWrap,
    );
  }
}
