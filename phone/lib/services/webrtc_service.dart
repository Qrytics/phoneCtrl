import 'dart:async';
import 'dart:convert';

import 'package:flutter/foundation.dart';
import 'package:flutter_webrtc/flutter_webrtc.dart';
import 'package:logger/logger.dart';
import 'package:web_socket_channel/io.dart';
import 'package:web_socket_channel/web_socket_channel.dart';

/// Connection states exposed to the UI via [ChangeNotifier].
enum ConnectionState {
  disconnected,
  connecting,
  connected,
  error,
}

/// [WebRtcService] manages the full lifecycle of the WebRTC connection to the
/// laptop host.
///
/// Signaling flow:
///   1. Phone opens a WebSocket to ws://<host>:<port>/signal
///   2. Phone sends an SDP offer
///   3. Laptop replies with an SDP answer
///   4. ICE candidates are exchanged via the same WebSocket
///   5. Once connected, the laptop streams H.264 video via a media track and
///      the phone sends input events via a [RTCDataChannel].
class WebRtcService extends ChangeNotifier {
  static final _log = Logger();

  // ── Public state ────────────────────────────────────────────────────────────

  ConnectionState connectionState = ConnectionState.disconnected;
  RTCVideoRenderer? renderer;

  // ── Private WebRTC objects ───────────────────────────────────────────────────

  RTCPeerConnection? _pc;
  RTCDataChannel?   _dataChannel;
  WebSocketChannel? _signalingWs;
  StreamSubscription<dynamic>? _wsSub;

  // Buffered ICE candidates received before remote description is set
  final _pendingCandidates = <RTCIceCandidate>[];
  bool _remoteDescSet = false;

  // ── Public API ───────────────────────────────────────────────────────────────

  /// Connect to the laptop host.
  /// Returns `true` when the WebRTC [RTCPeerConnection] reaches "connected".
  Future<bool> connect({required String host, required int port}) async {
    _setState(ConnectionState.connecting);

    try {
      renderer = RTCVideoRenderer();
      await renderer!.initialize();

      // Create peer connection
      _pc = await _createPeerConnection();

      // Open a data channel for sending input events (laptop → phone text
      // messages can also flow here for acknowledgements)
      _dataChannel = await _pc!.createDataChannel(
        'input',
        RTCDataChannelInit()
          ..ordered = true
          // maxRetransmits=0: drop packets that can't be delivered rather than
          // buffering and retransmitting, which keeps input latency low.
          ..maxRetransmits = 0,
      );

      // Connect to the signaling WebSocket (server listens on root path)
      final wsUri = Uri.parse('ws://$host:$port/');
      _signalingWs = IOWebSocketChannel.connect(wsUri);
      _wsSub = _signalingWs!.stream.listen(
        _onSignalingMessage,
        onError: _onSignalingError,
        onDone: () => _onSignalingError('WebSocket closed'),
      );

      // Register as a phone client before negotiating
      _signalingWs!.sink.add(jsonEncode({'type': 'register', 'role': 'phone'}));

      // Build and send the SDP offer
      final offer = await _pc!.createOffer({
        'offerToReceiveVideo': true,
        'offerToReceiveAudio': false,
      });
      await _pc!.setLocalDescription(offer);

      _signalingWs!.sink.add(jsonEncode({
        'type': 'offer',
        'sdp': offer.sdp,
      }));

      // Wait up to 15 s for connection
      final completer = Completer<bool>();
      _pc!.onConnectionState = (state) {
        if (state == RTCPeerConnectionState.RTCPeerConnectionStateConnected) {
          _setState(ConnectionState.connected);
          if (!completer.isCompleted) completer.complete(true);
        } else if (state == RTCPeerConnectionState.RTCPeerConnectionStateFailed ||
                   state == RTCPeerConnectionState.RTCPeerConnectionStateDisconnected) {
          _setState(ConnectionState.error);
          if (!completer.isCompleted) completer.complete(false);
        }
      };

      return await completer.future
          .timeout(const Duration(seconds: 15), onTimeout: () => false);
    } catch (e) {
      _log.e('connect() failed', error: e);
      _setState(ConnectionState.error);
      return false;
    }
  }

  /// Disconnect and release all WebRTC resources.
  Future<void> disconnect() async {
    await _wsSub?.cancel();
    await _signalingWs?.sink.close();
    await _dataChannel?.close();
    await _pc?.close();
    await renderer?.dispose();

    _dataChannel = null;
    _pc          = null;
    _signalingWs = null;
    renderer     = null;
    _remoteDescSet = false;
    _pendingCandidates.clear();

    _setState(ConnectionState.disconnected);
  }

  /// Send a raw JSON input-event string to the laptop via the data channel.
  void sendInputEvent(String json) {
    if (_dataChannel?.state == RTCDataChannelState.RTCDataChannelOpen) {
      _dataChannel!.send(RTCDataChannelMessage(json));
    }
  }

  // ── Private helpers ──────────────────────────────────────────────────────────

  Future<RTCPeerConnection> _createPeerConnection() async {
    final config = {
      'iceServers': [
        // Public STUN server for NAT traversal
        {'urls': 'stun:stun.l.google.com:19302'},
      ],
      'sdpSemantics': 'unified-plan',
    };

    final pc = await createPeerConnection(config);

    pc.onIceCandidate = (candidate) {
      _signalingWs?.sink.add(jsonEncode({
        'type': 'ice',
        'candidate':     candidate.candidate,
        'sdpMid':        candidate.sdpMid,
        'sdpMLineIndex': candidate.sdpMlineIndex,
      }));
    };

    pc.onTrack = (event) {
      if (event.track.kind == 'video' && renderer != null) {
        renderer!.srcObject = event.streams.isNotEmpty
            ? event.streams.first
            : null;
        notifyListeners();
      }
    };

    return pc;
  }

  void _onSignalingMessage(dynamic raw) async {
    final msg = jsonDecode(raw as String) as Map<String, dynamic>;
    final type = msg['type'] as String?;

    if (type == 'answer') {
      final answer = RTCSessionDescription(
        msg['sdp'] as String,
        'answer',
      );
      await _pc!.setRemoteDescription(answer);
      _remoteDescSet = true;

      // Drain buffered ICE candidates
      for (final c in _pendingCandidates) {
        await _pc!.addCandidate(c);
      }
      _pendingCandidates.clear();

    } else if (type == 'ice') {
      final candidate = RTCIceCandidate(
        msg['candidate']     as String?,
        msg['sdpMid']        as String?,
        msg['sdpMLineIndex'] as int?,
      );
      if (_remoteDescSet) {
        await _pc!.addCandidate(candidate);
      } else {
        _pendingCandidates.add(candidate);
      }
    }
  }

  void _onSignalingError(dynamic error) {
    _log.e('Signaling error', error: error);
    _setState(ConnectionState.error);
  }

  void _setState(ConnectionState s) {
    connectionState = s;
    notifyListeners();
  }
}
