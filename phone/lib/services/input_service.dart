import 'dart:convert';

import 'package:flutter/foundation.dart';
import 'package:logger/logger.dart';

import 'webrtc_service.dart';

/// [InputService] translates raw touch gestures captured by [TouchOverlay]
/// into JSON input-event strings and forwards them to the laptop via the
/// WebRTC data channel.
///
/// Supported events (mirror the laptop-side InputHandler):
///   - mousemove  : single-finger drag
///   - click      : single tap (left click)
///   - scroll     : two-finger drag (vertical scroll)
///   - keypress   : virtual keyboard key press (not exposed by overlay yet)
class InputService extends ChangeNotifier {
  static final _log = Logger();

  WebRtcService? _webrtc;

  /// The resolution of the laptop screen, received once connection is
  /// established (the host sends a {"type":"resolution",...} message).
  int laptopWidth  = 1920;
  int laptopHeight = 1080;

  /// The size of the view that shows the video on the phone screen.
  /// Must be updated by the UI when the video widget is laid out.
  double viewWidth  = 1;
  double viewHeight = 1;

  // ── Public API ───────────────────────────────────────────────────────────────

  /// Attach to a [WebRtcService] instance so events can be sent.
  void attachToWebRtc(WebRtcService webrtc) {
    _webrtc = webrtc;
  }

  void updateViewSize(double w, double h) {
    viewWidth  = w;
    viewHeight = h;
  }

  void updateLaptopResolution(int w, int h) {
    laptopWidth  = w;
    laptopHeight = h;
    notifyListeners();
  }

  /// Called when the user taps (single tap → left click).
  void onTap(double viewX, double viewY) {
    final lp = _toLaptopCoords(viewX, viewY);
    _send({
      'type': 'click',
      'x':    lp.$1,
      'y':    lp.$2,
      'button': 'left',
    });
  }

  /// Called when the user starts a single-finger drag.
  void onPointerMove(double viewX, double viewY) {
    final lp = _toLaptopCoords(viewX, viewY);
    _send({
      'type': 'mousemove',
      'x':    lp.$1,
      'y':    lp.$2,
    });
  }

  /// Called on mouse-button press (long press → right click).
  void onLongPress(double viewX, double viewY) {
    final lp = _toLaptopCoords(viewX, viewY);
    _send({
      'type':   'click',
      'x':      lp.$1,
      'y':      lp.$2,
      'button': 'right',
    });
  }

  /// Called when a two-finger vertical scroll is detected.
  /// [delta] is positive for scroll-up, negative for scroll-down.
  void onScroll(double delta) {
    _send({
      'type': 'scroll',
      'dx':   0,
      'dy':   delta.round(),
    });
  }

  /// Called when a virtual keyboard key is pressed.
  /// [vk] is a Windows Virtual-Key code (e.g. 0x41 for 'A').
  void onKeyPress(int vk) {
    _send({
      'type': 'keypress',
      'key':  '0x${vk.toRadixString(16).padLeft(2, '0')}',
    });
  }

  // ── Private helpers ──────────────────────────────────────────────────────────

  /// Scales phone view coordinates to laptop screen coordinates.
  (int, int) _toLaptopCoords(double viewX, double viewY) {
    if (viewWidth <= 0 || viewHeight <= 0) return (0, 0);
    final x = (viewX / viewWidth  * laptopWidth).round();
    final y = (viewY / viewHeight * laptopHeight).round();
    return (x.clamp(0, laptopWidth - 1), y.clamp(0, laptopHeight - 1));
  }

  void _send(Map<String, dynamic> event) {
    if (_webrtc == null) return;
    final json = jsonEncode(event);
    _log.d('send: $json');
    _webrtc!.sendInputEvent(json);
  }
}
