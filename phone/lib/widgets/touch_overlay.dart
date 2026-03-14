import 'package:flutter/gestures.dart';
import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../services/input_service.dart';

/// [TouchOverlay] is a transparent widget that sits on top of the video
/// renderer and translates touch gestures into [InputService] calls.
///
/// Gesture mapping:
///   Single tap          → left click
///   Long press          → right click
///   Single-finger drag  → mouse move
///   Two-finger vertical drag → vertical scroll
class TouchOverlay extends StatefulWidget {
  const TouchOverlay({super.key, required this.child});

  /// The video widget that sits below the touch surface.
  final Widget child;

  @override
  State<TouchOverlay> createState() => _TouchOverlayState();
}

class _TouchOverlayState extends State<TouchOverlay> {
  // Track pointers for multi-touch detection
  final Map<int, Offset> _pointers = {};

  // Last known position of primary pointer (for drag detection)
  Offset? _lastPrimaryPos;
  // Last known mid-point of two fingers (for scroll detection)
  Offset? _lastScrollMidpoint;

  @override
  Widget build(BuildContext context) {
    return LayoutBuilder(
      builder: (ctx, constraints) {
        // Keep InputService up to date with the current widget size
        WidgetsBinding.instance.addPostFrameCallback((_) {
          context.read<InputService>().updateViewSize(
            constraints.maxWidth,
            constraints.maxHeight,
          );
        });

        return Listener(
          behavior: HitTestBehavior.translucent,
          onPointerDown:   _onPointerDown,
          onPointerMove:   _onPointerMove,
          onPointerUp:     _onPointerUp,
          onPointerCancel: _onPointerCancel,
          child: GestureDetector(
            behavior: HitTestBehavior.translucent,
            onTapUp: (details) {
              context.read<InputService>()
                  .onTap(details.localPosition.dx, details.localPosition.dy);
            },
            onLongPressStart: (details) {
              context.read<InputService>().onLongPress(
                  details.localPosition.dx, details.localPosition.dy);
            },
            child: widget.child,
          ),
        );
      },
    );
  }

  void _onPointerDown(PointerDownEvent e) {
    _pointers[e.pointer] = e.localPosition;

    if (_pointers.length == 1) {
      // Single touch: start drag tracking
      _lastPrimaryPos    = e.localPosition;
      _lastScrollMidpoint = null;
    } else if (_pointers.length == 2) {
      // Two-finger gesture: compute initial midpoint for scroll
      final positions = _pointers.values.toList();
      _lastScrollMidpoint = (positions[0] + positions[1]) / 2;
      _lastPrimaryPos = null; // disable single-finger move
    }
  }

  void _onPointerMove(PointerMoveEvent e) {
    _pointers[e.pointer] = e.localPosition;

    final input = context.read<InputService>();

    if (_pointers.length == 1) {
      // Single-finger drag → mouse move
      final cur = e.localPosition;
      input.onPointerMove(cur.dx, cur.dy);
      _lastPrimaryPos = cur;

    } else if (_pointers.length == 2) {
      // Two-finger drag → scroll
      final positions = _pointers.values.toList();
      final midpoint  = (positions[0] + positions[1]) / 2;

      if (_lastScrollMidpoint != null) {
        final dy = midpoint.dy - _lastScrollMidpoint!.dy;
        // Threshold to avoid noise
        if (dy.abs() > 2) {
          // Divide by the sensitivity factor to convert touch pixels to scroll
          // units (larger value = less sensitive, smaller = more sensitive).
          const double scrollSensitivity = 8.0;
          input.onScroll(dy / scrollSensitivity);
        }
      }
      _lastScrollMidpoint = midpoint;
    }
  }

  void _onPointerUp(PointerUpEvent e) {
    _pointers.remove(e.pointer);
    if (_pointers.length < 2) _lastScrollMidpoint = null;
  }

  void _onPointerCancel(PointerCancelEvent e) {
    _pointers.remove(e.pointer);
    if (_pointers.length < 2) _lastScrollMidpoint = null;
  }
}
