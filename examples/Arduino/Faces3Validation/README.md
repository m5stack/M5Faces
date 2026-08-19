# Arduino Faces3 Full Validation

This sketch is the shared implementation used by the PlatformIO validation
project. It supports:

- Hosts: M5Stack Core Basic, Core2, and CoreS3
- Faces: Calculator3, Keyboard3, and Gamepad3
- `M5.In_I2C`: GPIO21/22 on Core Basic and Core2; GPIO12/11 on CoreS3
- English screen output, UID/model/version display, per-key coverage, and common API validation
- Keyboard3 Normal/Direct modes, LED modes `0x00-0x08`, and Aa/SYM/FN/ALT layers
- Model-specific latest-version checks (`V03/0x03` or `V04/0x04`), on-demand IAP, NVS power-loss recovery, and post-upgrade verification
- Read-only startup validation; host button C explicitly runs the NVS-protected persistent I2C address test

## Arduino IDE

1. Install `M5Unified 0.2.17`, `M5GFX 0.2.24`, and this library.
2. Open `Faces3Validation.ino`.
3. Select the matching M5Stack Core Basic, Core2, or CoreS3 board.
4. Select an application partition of at least 4 MB. The complete program embeds one controlled current IAP image per model.
5. Build and upload the sketch, then open the serial monitor at 115200 baud.

The complete implementation is in `Faces3Validation.cpp` in the same directory.
This prevents Arduino's automatic prototype generation from reordering C++
types used by the IAP state machine.

The default IAP behavior matches the PlatformIO project. Calculator3 and
Keyboard3 use `V03/0x03`; Gamepad3 uses `V04/0x04`. Current firmware is not
rewritten. Any other recognizable version is updated only to the latest image
for its model, followed by complete API and input validation. During an upgrade,
the static screen is drawn once; only the stage text and progress bar are refreshed.
