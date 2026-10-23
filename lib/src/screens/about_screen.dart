import 'package:flutter/material.dart';

import '../bridge/fluluks_api.g.dart';
import '../state/app_state.dart';
import '../strings_fr.dart';
import '../ui/tokens.dart';
import '../ui/widgets.dart';
import '../widgets/brand_mark.dart';

/// « À propos » : logo, version et appareil, ce que fait Portail, licences des bibliothèques et note sur la GPL.
class AboutScreen extends StatefulWidget {
  const AboutScreen({super.key, required this.state});

  final AppState state;

  @override
  State<AboutScreen> createState() => _AboutScreenState();
}

class _AboutScreenState extends State<AboutScreen> {
  late final Future<AppInfoMsg> _info = widget.state.appInfo();

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final muted = theme.textTheme.bodyMedium?.copyWith(color: theme.colorScheme.onSurfaceVariant);
    return Scaffold(
      appBar: AppBar(title: const Text(Fr.about)),
      body: FutureBuilder<AppInfoMsg>(
        future: _info,
        builder: (context, snap) {
          final info = snap.data; // absente si le pont échoue : la page reste utile sans la version
          return AdaptiveBody(
            maxWidth: Layout.maxForm,
            child: ListView(padding: listPadding(context, const EdgeInsets.all(Space.xl)), children: [
              const Center(child: BrandMark(size: 72)),
              const SizedBox(height: Space.l),
              Text(Fr.title, style: theme.textTheme.headlineSmall, textAlign: TextAlign.center),
              if (info != null) ...[
                const SizedBox(height: Space.xs),
                Text(
                  Fr.version(info.versionName, info.versionCode),
                  style: numeric(theme.textTheme.bodyLarge!),
                  textAlign: TextAlign.center,
                ),
                Text(Fr.deviceLine(info.androidRelease, info.device), style: muted, textAlign: TextAlign.center),
              ],
              const SizedBox(height: Space.xl),
              Text(Fr.aboutTagline, style: theme.textTheme.bodyLarge, textAlign: TextAlign.center),
              const SizedBox(height: Space.xl),
              Center(
                child: FilledButton.tonalIcon(
                  icon: const Icon(Icons.description_rounded),
                  label: const Text(Fr.licensesButton),
                  onPressed: () => showLicensePage(
                    context: context,
                    applicationName: Fr.title,
                    applicationVersion: info?.versionName,
                    applicationIcon: const BrandMark(size: 48),
                  ),
                ),
              ),
              const SizedBox(height: Space.xl),
              const StatusBanner(icon: Icons.gavel_rounded, text: Fr.gplNote, tone: ChipTone.neutral),
            ]),
          );
        },
      ),
    );
  }
}
