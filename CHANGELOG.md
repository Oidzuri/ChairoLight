# Changelog

All notable ChairoLight changes are documented here. Older Prismatik history remains available in `Software/CHANGELOG` and in the Git history.

## [Unreleased]

- Complete the Windows 10/11, HDR, multi-monitor and DPI validation matrix.
- Replace preview documentation images with final release screenshots after the UI freeze.
- Optional Authenticode signing when a code-signing certificate is available.

## [0.9.0] - 2026-07-15

### Added

- Modern dark ChairoLight interface with reusable cards, navigation and status bar styling.
- Start-with-Windows option and start-minimized-to-tray behavior.
- Tray Settings action that restores the main window.
- Windows Graphics Capture support alongside Desktop Duplication and WinAPI capture.
- Adaptive edge color mode, scene analysis and Anime/Game/Cinema presets.
- Perceptual tone mapping and soft highlight compression for bright white, yellow and red scenes.
- Smart color correction controls and experimental camera-assisted calibration.
- Settings import/export and connection recovery.
- Arduino/Adalight connection animation inspired by a green spiral-energy effect.
- Regression tests for BGRA black-frame handling and updated API compatibility tests.

### Changed

- Product name and user-facing branding changed to ChairoLight.
- About page reduced to the product description and clear Prismatik attribution.
- Legacy plugin-manager entry previously mislabeled as “Zones” removed from navigation.
- Main page reorganized into aligned capture, calibration and eye-comfort cards.

### Fixed

- Windows Graphics Capture no longer interprets the alpha byte as blue for BGRA frames.
- Black capture frames remain neutral instead of becoming blue.
- Clipped labels, inconsistent buttons, broken navigation icons and overlapping layouts.
- Startup behavior that previously opened the main window over the desktop.

### Compatibility

- The executable filename remains `Prismatik.exe` for compatibility with existing profiles, scripts and updater assumptions.
- Prismatik API compatibility is retained unless otherwise noted.
