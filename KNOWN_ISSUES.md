# Known limitations — ChairoLight 0.9.0

## Capture

- Protected video (DRM), secure desktop surfaces and some hardware overlays can appear black. This is a Windows/content-protection limitation; ChairoLight does not attempt to bypass it.
- HDR output is experimental. Color appearance may vary with Windows HDR settings, GPU drivers, monitor tone mapping and capture backend. Use SDR for predictable calibration.
- On hybrid-GPU laptops, capture can fail or produce an empty frame when the application and display are handled by different adapters. Try Desktop Duplication or select the same GPU for ChairoLight as the target application.
- Full-screen exclusive games may require Desktop Duplication or borderless-window mode.

## Camera calibration

- Camera calibration is experimental.
- Automatic exposure, automatic white balance, HDR webcam processing and room-light changes can invalidate measurements.
- Calibration should target the illuminated wall around the monitor, not only the LCD panel. Lock exposure and white balance in the camera driver when possible.
- A green/yellow cast can also come from the wall color, LED binning, power voltage drop or an incorrect RGB/GRB channel order.

## Devices and profiles

- A portable release starts without a personal device profile. COM port, LED count, channel order and physical layout must be configured by the user.
- Arduino reconnect behavior depends on the board bootloader and driver. Some boards change COM number after reconnection.
- Very long strips need power injection. Voltage drop can change white balance even when software calibration is correct.

## Interface

- The 0.9.0 release is optimized for approximately 1080×850 and tested locally at standard DPI. The complete 125–150% DPI matrix must be signed off before 1.0.0.
- The hidden legacy plugin manager remains in the codebase for compatibility but is not exposed in ChairoLight navigation.

Report new issues with the information requested in `README.md`, without attaching unreviewed personal configuration files.
