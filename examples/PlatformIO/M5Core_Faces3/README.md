# M5Core Basic / Core2 / CoreS3 Faces3 Full Validation

This is a functional validation project for the M5Faces Arduino library, not
production-test firmware. One project automatically detects and validates:

- `M5Faces_Calculator3` (`0x01`)
- `M5Faces_Keyboard3` (`0x02`)
- `M5Faces_Gamepad3` (`0x03`)

Supported hosts are M5Stack Core Basic, Core2, and CoreS3. Faces connects to
`M5.In_I2C`: Core Basic and Core2 use SDA GPIO21/SCL GPIO22; CoreS3 uses SDA
GPIO12/SCL GPIO11. Before M5Unified initialization, each PIO environment fixes
the M5GFX board cache for its host so an NVS value from another host is not
reused. Core2 and CoreS3 retain the internal bus created by M5Unified because
touch, system devices, and Faces share IN.I2C. The project does not rebind those
pins to Arduino `Wire`.

## Firmware Version Check and Update

The program reads the model from register `0xD0` and the firmware version from
`0xFE`. Calculator3 and Keyboard3 use `V03 / 0x03`; Gamepad3 uses `V04 / 0x04`.
When the device already runs its current version, the check is logged and
flashing is skipped. Any other recognizable version is updated only with the
latest image for that model. After the update, the program re-reads the model,
version, and UID; Keyboard3 also verifies its operating mode. Success is never
inferred from write completion alone.

Flashing uses the M5Faces library entry point
`faces_iap_upgrade_by_variant(&M5.In_I2C, ...)`; the PIO project does not carry a
separate upgrade protocol. `IAP_BACKEND framework=Arduino
api=faces_iap_upgrade_by_variant` indicates that the library IAP backend was
entered. Final success requires both `IAP_LIBRARY_RESULT ... result=PASS` and
`IAP_PASS`.

Before flashing, the project stores the model, target version, and application
I2C address in the dedicated `M5FacesIAP` NVS namespace. If power is lost during
flashing, the next boot detects bootloader address `0x54` and resumes the latest
image for that model. If the image is already complete, only post-write verification runs.
After the application restarts, detection is limited to the default address,
recorded address, and controlled candidate addresses. It does not scan the full
Core2/CoreS3 internal bus or misidentify host peripherals. When the address of
the same model changes, the recovery record is updated instead of reporting a
device mismatch. Execution continues only after verification succeeds and the
NVS recovery record is cleared successfully.

Legacy recovery records are migrated to the latest model-specific target.
Gamepad3 records targeting the previous `V03 / 0x03` image are migrated to
`V04 / 0x04`. No downgrade image is included. Keep host power and the Faces
connection stable during IAP.

| Model | Current version | Action |
| --- | --- | --- |
| Calculator3 | `0x03` | No update |
| Keyboard3 | `0x03` | No update |
| Gamepad3 | `0x04` | No update |
| Calculator3/Keyboard3, other recognizable version | Other | Update to `V03 / 0x03` |
| Gamepad3, other recognizable version | Other | Update to `V04 / 0x04` |

## Automatic API Validation

Before opening the key-test screen, the program automatically validates these
Arduino APIs and behaviors:

- Initialization state, not-initialized errors, null-argument errors, and invalid-address errors
- Model read, model match, model name, and Direct-mode capability
- 12-byte UID, firmware version, and I2C address
- `getVersionAddr()` on all models and `getModeLED()` additionally on Keyboard3
- Raw `readReg()` and `writeReg()` register access
- Both `M5.In_I2C` and Arduino `TwoWire` `begin()` adapters
- Dynamic 100 kHz/400 kHz switching and communication after each switch
- Normal/Direct mode write and readback on Keyboard3
- Keyboard3 LED modes `0x00` through `0x08`, followed by restoration of the original state
- User-triggered temporary change to an unused I2C address, verification at the new address, and restoration of the original address
- Static parser helpers for Calculator3, Gamepad3, and Keyboard3
- `M5FacesPoller` Normal/Direct dispatch and callback paths

Each passing check emits `FV_API name=... result=PASS`; the summary is emitted as
`FV_API_SUMMARY`. Automatic startup validation is read-only for the I2C address
and emits `FV_SKIP api=i2c_address_change_restore`. Host button C explicitly
runs the persistent address-change test as part of the common API retest. Before
changing the address, the example commits the model, original address, and
temporary address to NVS. On boot it restores an interrupted address change
before detection or any other API checks.

The official Calculator3 V03 and Gamepad3 V04 protocols do not provide the `0xF0`
mode register or `0xF1` LED register. Those checks emit `FV_SKIP`; this is not a
failure and does not block the key test. On Core2/CoreS3, the Arduino `Wire`
adapter check also emits `FV_SKIP` so the test does not release the shared
IN.I2C bus used by touch, system devices, and Faces. The `Wire` adapter remains
covered by the Core Basic environment and smoke checks.

## Interactive Validation

Calculator3 validates all 20 keys, `getKey()`, `getChar()`, `isPressed()`,
`keyChanged()`, and control-key parsing.

Gamepad3 validates all 8 keys, raw state, press/release edges, individual button
queries, and combination-name parsing. Gamepad3 register responses and key events
share the device transmit buffer. In blind polling, raw `0x00` is treated as an
invalid empty TX-buffer sample and does not change the stable button state. After
common API checks, the project reads one synchronization baseline and emits
`FV_INPUT_SYNC ... dispatched=0`; synchronization data does not update the screen.

Keyboard3 requires all 35 physical keys and six functional categories:

- Normal-mode output through `update()`, `getChar()`, and `isPrintable()`
- Direct default layer
- Aa uppercase/lowercase layer
- SYM symbol layer (`SYM+0` outputs `ESC / 0x1B`)
- FN navigation/editing layer
- ALT extended layer

Keyboard3 starts in Direct mode. Host button B switches between Normal and
Direct. One modifier plus one action key displays the mapped result and emits
`FV_COMBO`. Pressing Aa toggles the validation program's uppercase/lowercase
state. Conflicting modifiers or multiple action keys are displayed but do not
count toward completion.

Host controls:

- A: clear key and Keyboard3 feature-coverage records
- B: switch Keyboard3 between Normal and Direct modes
- C: rerun common API validation, including the persistent address-change test

Core Basic uses physical A/B/C buttons. Core2 uses the virtual A/B/C buttons
below the screen. CoreS3 has no A/B/C buttons, so the bottom of the validation
screen provides Reset, Mode, and API touch zones. On a detection-failure screen,
touch any bottom zone to retry.

The UI uses partial refresh. The static IAP layout is drawn once when the update
starts; only the stage and progress bar change while writing. The key page updates
only the top status area and changed keys. The key-progress line continuously
shows the detected model ID and firmware version, while the footer shows the
12-byte UID as 24 hexadecimal characters. `FV_PASS` is emitted only when all
automatic API checks pass, every key for the current model has been tested, and
all six Keyboard3 functional categories have been covered when applicable.

## Current Limitations

- The RGB/Neopixel backend is not enabled in the Arduino build, so serial output includes `FV_SKIP api=rgb`.
- The reference project does not define the GPIO connection from Faces INT to Core Basic. The interrupt path therefore does not guess a pin and emits `FV_SKIP api=interrupt`; register polling is fully validated.
- IAP enumeration/query APIs are covered jointly by the PIO and ESP-IDF projects. This PIO project validates version checking, flashing, power-loss recovery, and post-write verification for the three current controlled images below.

## Controlled IAP Images

| Model | Version | Source | Base address | Image size | Image SHA-256 |
| --- | --- | --- | --- | ---: | --- |
| Calculator3 | `V03 / 0x03` | `f457b86816df2e3fa5898a367b0b8f80aea4f8b1` | `0x08002800` | 53248 | `E75460FC97F92576265C47B39C47EFE7E0D554828766FE7BC21991C33C925F5D` |
| Keyboard3 | `V03 / 0x03` | `c59042bedc513483f656fa283f40798460edf3b3` | `0x08001000` | 53248 | `0ABCDB9B9E70219164059F4C10488391363236E35DB47A0203A220E936A3E352` |
| Gamepad3 | `V04 / 0x04` | Local controlled image | `0x08001000` | 11264 | `92352972E2637FF047E99FA96A59A8223F31786B8E74EC59C9D05B11F5D8B532` |

## Build

```powershell
pio run -e m5core-basic-16m
pio run -e m5core2
pio run -e m5cores3
pio run -e m5core-basic-16m -t upload
pio run -e m5core2 -t upload
pio run -e m5cores3 -t upload
pio device monitor -b 115200
```

A successful build confirms only compile-time validation. `FV_PASS` still
requires interaction with real hardware.
