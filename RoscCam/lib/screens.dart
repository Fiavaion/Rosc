// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

/// The app's three screens: find an editor, pair with it, and operate its camera.
library;


import 'package:flutter/services.dart';
import 'package:material_ui/material_ui.dart';

import 'device.dart';
import 'link.dart';
import 'main.dart';
import 'protocol.dart';

/// Every control is at least this tall and wide, the platforms' smallest comfortable target.
const double minTarget = 48;

/// The width of the controls down the side of the picture.
const double railWidth = 248;

final ButtonStyle _targetStyle = ButtonStyle(minimumSize: WidgetStateProperty.all(const Size(minTarget, minTarget)));

class _Problem extends StatelessWidget {
  const _Problem(this.text);
  final String text;

  @override
  Widget build(BuildContext context) {
    if (text.isEmpty) {
      return const SizedBox.shrink();
    }
    final color = Theme.of(context).colorScheme.error;
    return Padding(
      padding: const EdgeInsets.only(top: 12),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Icon(Icons.error_outline, color: color, semanticLabel: 'Problem'),
          const SizedBox(width: 8),
          Expanded(child: Text(text)),
        ],
      ),
    );
  }
}

class ConnectScreen extends StatefulWidget {
  const ConnectScreen({super.key, required this.controller});
  final RoscCamController controller;

  @override
  State<ConnectScreen> createState() => _ConnectScreenState();
}

class _ConnectScreenState extends State<ConnectScreen> {
  final TextEditingController _address = TextEditingController();

  @override
  void dispose() {
    _address.dispose();
    super.dispose();
  }

  void _connectTyped() {
    if (_address.text.trim().isNotEmpty) {
      widget.controller.connect(_address.text);
    }
  }

  @override
  Widget build(BuildContext context) {
    final controller = widget.controller;
    final text = Theme.of(context).textTheme;
    return Scaffold(
      body: SafeArea(
        child: Center(
          child: ConstrainedBox(
            constraints: const BoxConstraints(maxWidth: 560),
            child: ListView(
              padding: const EdgeInsets.all(24),
              children: [
                Text('RoscCam', style: text.headlineMedium),
                const SizedBox(height: 4),
                const Text('Operate a Rosc camera from this device. In Rosc, open the Grip tab and start the RoscCam server.'),
                const SizedBox(height: 20),
                Text('Editors on this network', style: text.titleMedium),
                ListenableBuilder(
                  listenable: controller.finder,
                  builder: (context, _) {
                    final editors = controller.finder.editors;
                    if (editors.isEmpty) {
                      return Padding(
                        padding: const EdgeInsets.symmetric(vertical: 12),
                        child: Text(controller.finder.problem.isNotEmpty
                            ? controller.finder.problem
                            : 'None heard yet. Start the RoscCam server in the Grip tab; this device and the computer need to be on the same network.'),
                      );
                    }
                    return Column(
                      children: [
                        for (final editor in editors)
                          ListTile(
                            minTileHeight: 56,
                            leading: const Icon(Icons.computer),
                            title: Text(editor.name),
                            subtitle: Text(editor.address),
                            trailing: const Icon(Icons.chevron_right),
                            enabled: !controller.connecting,
                            onTap: () => controller.connect(editor.address, port: editor.port),
                          ),
                      ],
                    );
                  },
                ),
                const SizedBox(height: 12),
                Text('Or type its address', style: text.titleMedium),
                const SizedBox(height: 8),
                Row(
                  children: [
                    Expanded(
                      child: TextField(
                        controller: _address,
                        keyboardType: TextInputType.url,
                        decoration: const InputDecoration(labelText: 'Address, as the Grip tab shows it', border: OutlineInputBorder()),
                        onSubmitted: (_) => _connectTyped(),
                      ),
                    ),
                    const SizedBox(width: 12),
                    FilledButton(style: _targetStyle, onPressed: controller.connecting ? null : _connectTyped, child: const Text('Connect')),
                  ],
                ),
                if (controller.connecting) const Padding(padding: EdgeInsets.only(top: 12), child: Text('Connecting…')),
                _Problem(controller.problem),
                const SizedBox(height: 24),
                // Google's terms for ARCore ask for this on the app's main screen.
                Text(
                  'This application runs on Google Play Services for AR (ARCore), which is provided by Google and governed by '
                  'the Google Privacy Policy (policies.google.com/privacy); its use is subject to the Google Terms of Service '
                  '(policies.google.com/terms). RoscCam uses it, where the device has it, only to track where the device is carried.',
                  style: text.bodySmall,
                ),
                Align(
                  alignment: Alignment.centerLeft,
                  child: TextButton(
                    style: _targetStyle,
                    onPressed: () => showLicensePage(context: context, applicationName: 'RoscCam', applicationVersion: appVersion),
                    child: const Text('Licences'),
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

class PairScreen extends StatefulWidget {
  const PairScreen({super.key, required this.link});
  final EditorLink link;

  @override
  State<PairScreen> createState() => _PairScreenState();
}

class _PairScreenState extends State<PairScreen> {
  final TextEditingController _code = TextEditingController();

  @override
  void dispose() {
    _code.dispose();
    super.dispose();
  }

  void _pair() {
    if (_code.text.length == 4) {
      widget.link.pair(_code.text);
    }
  }

  @override
  Widget build(BuildContext context) {
    final link = widget.link;
    final text = Theme.of(context).textTheme;
    return Scaffold(
      body: SafeArea(
        child: Center(
          child: ConstrainedBox(
            constraints: const BoxConstraints(maxWidth: 520),
            child: ListView(
              padding: const EdgeInsets.all(24),
              children: [
                Text(link.editor.isEmpty ? 'Pair' : 'Pair with ${link.editor}', style: text.headlineSmall),
                const SizedBox(height: 8),
                const Text('Type the four-digit code that the Grip tab in Rosc shows. Until this device is paired, the editor sends it nothing.'),
                const SizedBox(height: 16),
                TextField(
                  controller: _code,
                  autofocus: true,
                  keyboardType: TextInputType.number,
                  inputFormatters: [FilteringTextInputFormatter.digitsOnly, LengthLimitingTextInputFormatter(4)],
                  style: text.headlineMedium,
                  decoration: const InputDecoration(labelText: 'Code', border: OutlineInputBorder()),
                  onSubmitted: (_) => _pair(),
                ),
                const SizedBox(height: 16),
                Row(
                  children: [
                    FilledButton(style: _targetStyle, onPressed: _pair, child: const Text('Pair')),
                    const SizedBox(width: 12),
                    TextButton(style: _targetStyle, onPressed: link.close, child: const Text('Cancel')),
                  ],
                ),
                _Problem(link.problem),
              ],
            ),
          ),
        ),
      ),
    );
  }
}

class CameraScreen extends StatefulWidget {
  const CameraScreen({super.key, required this.link, required this.motion});
  final EditorLink link;
  final DeviceMotion motion;

  @override
  State<CameraScreen> createState() => _CameraScreenState();
}

class _CameraScreenState extends State<CameraScreen> {
  @override
  void initState() {
    super.initState();
    widget.link.result.addListener(_showResult);
  }

  @override
  void dispose() {
    widget.link.result.removeListener(_showResult);
    super.dispose();
  }

  void _showResult() {
    final result = widget.link.result.value;
    if (result == null || (result.ok && result.message.isEmpty) || !mounted) {
      return;
    }
    ScaffoldMessenger.of(context)
      ..hideCurrentSnackBar()
      ..showSnackBar(SnackBar(
        content: Text(result.message),
        duration: const Duration(seconds: 4),
        // Over the picture, clear of the controls.
        behavior: SnackBarBehavior.floating,
        margin: const EdgeInsets.fromLTRB(16, 0, railWidth + 16, 16),
      ));
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: Row(
        children: [
          Expanded(child: PictureView(link: widget.link)),
          SizedBox(width: railWidth, child: ControlRail(link: widget.link, motion: widget.motion)),
        ],
      ),
    );
  }
}

/// The camera's picture, as large as it fits, with frame lines for the delivery aspect.
class PictureView extends StatelessWidget {
  const PictureView({super.key, required this.link});
  final EditorLink link;

  @override
  Widget build(BuildContext context) {
    return ColoredBox(
      color: Colors.black,
      child: ValueListenableBuilder<Uint8List?>(
        valueListenable: link.picture,
        builder: (context, jpeg, _) {
          final size = jpeg != null ? jpegSize(jpeg) : null;
          if (jpeg == null || size == null || size.height == 0) {
            return const Center(child: Text('Waiting for the picture…'));
          }
          return Center(
            child: AspectRatio(
              aspectRatio: size.width / size.height,
              child: Stack(
                fit: StackFit.expand,
                children: [
                  Image.memory(jpeg, gaplessPlayback: true, fit: BoxFit.fill, semanticLabel: "The camera's picture"),
                  CustomPaint(painter: FrameLines(link.state.aspect)),
                ],
              ),
            ),
          );
        },
      ),
    );
  }
}

/// Lines round the part of the picture that is delivered, and the rest dimmed.
class FrameLines extends CustomPainter {
  const FrameLines(this.aspect);
  final double aspect;

  /// The delivered frame within a picture of this size; the whole picture when the aspect is zero.
  static Rect frameIn(Size size, double aspect) {
    if (aspect <= 0 || size.height <= 0) {
      return Offset.zero & size;
    }
    final pictureAspect = size.width / size.height;
    final frame = aspect >= pictureAspect ? Size(size.width, size.width / aspect) : Size(size.height * aspect, size.height);
    return Rect.fromCenter(center: size.center(Offset.zero), width: frame.width, height: frame.height);
  }

  @override
  void paint(Canvas canvas, Size size) {
    final frame = frameIn(size, aspect);
    if (frame == Offset.zero & size) {
      return;
    }
    final outside = Path.combine(PathOperation.difference, Path()..addRect(Offset.zero & size), Path()..addRect(frame));
    canvas.drawPath(outside, Paint()..color = const Color(0x99000000));
    canvas.drawRect(frame.deflate(1), Paint()
      ..style = PaintingStyle.stroke
      ..strokeWidth = 2
      ..color = const Color(0xE6FFFFFF));
  }

  @override
  bool shouldRepaint(FrameLines oldDelegate) => oldDelegate.aspect != aspect;
}

/// The camera's state and the operator's controls, down the side of the picture.
class ControlRail extends StatelessWidget {
  const ControlRail({super.key, required this.link, required this.motion});
  final EditorLink link;
  final DeviceMotion motion;

  Widget _button(String label, VoidCallback? onPressed, {bool primary = false}) {
    final child = Text(label, textAlign: TextAlign.center);
    return primary
        ? FilledButton(style: _targetStyle, onPressed: onPressed, child: child)
        : FilledButton.tonal(style: _targetStyle, onPressed: onPressed, child: child);
  }

  Future<void> _chooseMark(BuildContext context) async {
    final marks = link.state.marks;
    final mark = await showModalBottomSheet<int>(
      context: context,
      builder: (context) => SafeArea(
        child: ListView(
          shrinkWrap: true,
          children: [
            const ListTile(title: Text('Rack the focus to')),
            for (var index = 0; index < marks.length; index++)
              ListTile(minTileHeight: 56, title: Text(marks[index]), onTap: () => Navigator.pop(context, index)),
          ],
        ),
      ),
    );
    if (mark != null) {
      link.control('rack', {'mark': mark});
    }
  }

  @override
  Widget build(BuildContext context) {
    final state = link.state;
    final text = Theme.of(context).textTheme;
    final colors = Theme.of(context).colorScheme;
    final String status;
    if (state.recording) {
      status = 'Recording ${state.takeSeconds.toStringAsFixed(1)} s';
    } else if (state.operating) {
      status = state.live ? 'Operating ${state.support}' : 'Operating, but the editor hears no motion';
    } else {
      status = 'Not operating';
    }
    return Material(
      color: colors.surfaceContainer,
      child: SafeArea(
        left: false,
        child: ListView(
          padding: const EdgeInsets.all(12),
          children: [
            Text(state.camera.isEmpty ? link.editor : state.camera, style: text.titleMedium),
            Text(state.support),
            const SizedBox(height: 4),
            Text([state.lens, state.tStop, state.focus].where((part) => part.isNotEmpty).join(' · ')),
            const SizedBox(height: 8),
            Row(
              children: [
                Icon(state.recording ? Icons.fiber_manual_record : Icons.videocam_outlined,
                    color: state.recording ? Colors.redAccent : null, semanticLabel: state.recording ? 'Recording' : null),
                const SizedBox(width: 6),
                Expanded(child: Text(status)),
              ],
            ),
            Text(motion.isPositionTracked ? 'Position tracked by AR' : 'Position not tracked: turning only', style: text.bodySmall),
            if (motion.problem.isNotEmpty) _Problem(motion.problem),
            const SizedBox(height: 12),
            GridView.count(
              crossAxisCount: 2,
              shrinkWrap: true,
              physics: const NeverScrollableScrollPhysics(),
              mainAxisSpacing: 8,
              crossAxisSpacing: 8,
              childAspectRatio: 2,
              children: [
                _button(state.operating ? 'Release' : 'Operate', () => link.control(state.operating ? 'release' : 'operate'), primary: true),
                _button('Zero', state.operating ? () => link.control('zero') : null),
                _button(state.recording ? 'Stop' : 'Record', state.operating || state.recording ? () => link.control(state.recording ? 'stop' : 'record') : null),
                _button('Bake Take', state.hasTake && !state.recording ? () => link.control('bake') : null),
                _button('Lens −', () => link.control('lens', {'step': -1})),
                _button('Lens +', () => link.control('lens', {'step': 1})),
              ],
            ),
            const SizedBox(height: 8),
            _button('Rack to Mark', state.marks.isEmpty ? null : () => _chooseMark(context)),
            for (final warning in state.warnings)
              Padding(
                padding: const EdgeInsets.only(top: 10),
                child: Row(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Icon(Icons.warning_amber, color: colors.tertiary, semanticLabel: 'Warning'),
                    const SizedBox(width: 6),
                    Expanded(child: Text(warning)),
                  ],
                ),
              ),
            const SizedBox(height: 12),
            TextButton(style: _targetStyle, onPressed: link.close, child: const Text('Disconnect')),
          ],
        ),
      ),
    );
  }
}
