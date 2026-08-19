# ESP-IDF Faces3 Full Validation

This is a native ESP-IDF 5.2-5.5 example with no Arduino Core dependency. It
supports Calculator3, Keyboard3, and Gamepad3, and covers:

- Controlled-address detection without scanning the entire Core2/CoreS3 internal I2C bus
- Model ID, firmware version, I2C address, and 12-byte UID
- 100/400 kHz switching, temporary address changes, and original-address restoration
- Keyboard3 Normal/Direct modes, LED modes `0x00-0x08`, parsers, and `SYM+0 = ESC`
- Calculator3 and Gamepad3 parsers plus live input logs for all three models
- Model-specific latest-version checks (`V03/0x03` or `V04/0x04`), on-demand IAP, and pending/known-identity NVS recovery
- Post-upgrade verification of the version, compatible model ID, and UID
- Read-only startup validation; host button/touch C explicitly runs the NVS-protected persistent I2C address test

The example pins `M5Unified 0.2.17` through `main/idf_component.yml`. M5GFX is
provided transitively by the M5Unified component manifest to avoid declaring the
same managed component twice. The example provides the same English screen,
partial refresh, Core Basic/Core2 host controls, and CoreS3 bottom touch controls
as the Arduino/PIO validation program. `FV_*` and `IAP_*` logs are also written
to the serial console.

The current M5Unified/M5GFX releases are not compatible with ESP-IDF 6.0 or
6.1-dev. Use ESP-IDF 5.2-5.5; version 5.5.3 is the recommended verified version.
After changing IDF versions, remove `build`, `managed_components`,
`dependencies.lock`, and `sdkconfig` before configuring the project again.

The complete program embeds one controlled current IAP image per model. All three host configurations use
16 MB flash and the 4 MB factory-app partition in `partitions.csv`.

## Build

For Core Basic or Core2:

```sh
idf.py set-target esp32
idf.py menuconfig
idf.py build flash monitor
```

For CoreS3:

```sh
idf.py set-target esp32s3
idf.py menuconfig
idf.py build flash monitor
```

Run `idf.py fullclean` before switching between the `esp32` and `esp32s3`
targets. Select the host in the `M5Faces3 validation example` menu:

| Host | I2C port | SDA | SCL |
| --- | ---: | ---: | ---: |
| Core Basic | 0 | 21 | 22 |
| Core2 | 1 | 21 | 22 |
| CoreS3 | 1 | 12 | 11 |

IAP is enabled by default. Calculator3 and Keyboard3 use `V03/0x03`; Gamepad3
uses `V04/0x04`. Current firmware is not rewritten. Any other recognizable
version is updated only to the latest image for its model, followed by complete
library API and live input validation. Do not clear NVS if an upgrade is
interrupted; the next boot uses the pending record and bootloader address `0x54`
to recover the device to its current target.

`examples/ESP-IDF/basic` remains the minimal Gamepad3 usage example.
