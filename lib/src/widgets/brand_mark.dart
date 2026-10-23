import 'package:flutter/material.dart';

/// Logo de Portail (assets/logo/portail.png, rendu depuis le SVG) : décoratif, exclu de TalkBack.
class BrandMark extends StatelessWidget {
  const BrandMark({super.key, this.size = 32});

  final double size;

  @override
  Widget build(BuildContext context) => Image.asset(
        'assets/logo/portail.png',
        width: size,
        height: size,
        excludeFromSemantics: true,
        filterQuality: FilterQuality.medium,
      );
}
