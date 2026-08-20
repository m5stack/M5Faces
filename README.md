# M5Faces

## Overview

### SKU:A003-V3

Faces Keyboard3 is a keyboard input module designed for the Faces Bottom series. Featuring a 35-key layout, it supports multi-character input via key combinations, meeting basic text entry and shortcut control requirements. The module integrates an STM32 core controller and communicates with the host system via I2C protocol and interrupt pins, offering fast response and high integration.

### SKU:A004-V3

The Faces Gamepad3 is a game control input module designed for the Faces Bottom series. It features classic directional controls, A/B buttons, and interactive keys such as pause/start, making it ideal for gaming and quick operation scenarios. The module integrates an STM32 core controller and communicates with the host via I2C protocol and interrupt pins, offering fast response times and high integration.

### SKU:A005-V3

Faces Calculator3 is a calculator input module designed for the Faces Bottom series. Featuring a 4×5 keypad layout, it includes commonly used digits, operators, and function keys, making it ideal for building interactive terminals with local computing capabilities. The module communicates with the host via I2C and enables interaction functions such as numeric input, basic arithmetic operations, and calculation result display.



## Related Link

- [A003-V3 Document & Datasheet](https://docs.m5stack.com/en/faces/Faces_Keyboard3)
- [A004-V3 Document & Datasheet](https://docs.m5stack.com/en/faces/Faces_Gamepad3)
- [A005-V3 Document & Datasheet](https://docs.m5stack.com/en/faces/Faces_Calculator3)

## Built-in Firmware:

- [A003-V3 Firmware ](https://github.com/m5stack/M5Faces-Keyboard3-Internal-FW)
- [A004-V3 Firmware ](https://github.com/m5stack/M5Faces-Gamepad3-Internal-FW)
- [A005-V3 Firmware ](https://github.com/m5stack/M5Faces-Calculator3-Internal-FW)

## License

- [Product Name- MIT](https://github.com/m5stack/M5Faces/blob/main/LICENSE)

## Arduino

Install the folder as an Arduino library and include the unified header:

```cpp
#include <M5Faces.h>

M5Faces_Keyboard3 keyboard;

void setup() {
    Serial.begin(115200);
    Wire.begin();

    if (keyboard.begin(&Wire) != M5FACES_OK) {
        Serial.println("Keyboard3 not found");
    }
}

void loop() {
    if (keyboard.update() && keyboard.isPressed()) {
        char c = keyboard.getChar();
        if (c != '\0') Serial.print(c);
    }
    delay(5);
}
```

[Faces3Validation](examples/Arduino/Faces3Validation) is the complete
three-model, three-host validation sketch shared by the PlatformIO project.

## PlatformIO Validation

[`examples/PlatformIO/M5Core_Faces3`](examples/PlatformIO/M5Core_Faces3)
builds the shared Arduino validation sketch for Core Basic, Core2, and CoreS3.
It checks controlled firmware, exercises register and configuration APIs, routes
input through `M5FacesPoller`, and displays the matching English input screen.
Keyboard3 covers Normal and Direct modes plus the Aa, SYM, FN, and ALT layers.
Firmware `0x03` is current for Calculator3 and Keyboard3; firmware `0x04` is
current for Gamepad3. Current firmware is not rewritten, and older recognizable
firmware is upgraded only to the latest controlled image for its model.

## ESP-IDF

Add this repository as a component and pass either an initialized
`i2c_master_bus_handle_t` or, when available, an M5Unified `I2C_Class`:

```cpp
#include "M5Faces.h"

M5Faces_Gamepad3 gamepad;
m5faces_err_t result = gamepad.begin(bus_handle);
```

`begin(bus_handle)` registers and owns its device handle. The caller continues
to own the bus. `begin(device_handle)` is also available when the caller wants
to own device registration.

See [ESP-IDF Faces3Validation](examples/ESP-IDF/Faces3Validation) for native
three-host detection, complete API checks, live input parsing, and recoverable
updates to the latest model-specific firmware. The [basic example](examples/ESP-IDF/basic)
remains the minimal Gamepad3 example.

## IAP

`M5Faces_IAP.hpp` is included by `M5Faces.h` on all supported frameworks. Select
a firmware image by model ID and exact controlled variant name.

Arduino and PlatformIO use the shared M5Unified internal I2C bus:

```cpp
esp_err_t err = faces_iap_upgrade_by_variant(
    &M5.In_I2C,
    M5Faces_Keyboard3::MODEL_ID,
    "V03",
    12,
    11,
    M5FACES_BOTTOM3_ADDR,
    progress_callback,
    nullptr);
```

Native ESP-IDF uses an `i2c_master_bus_handle_t`:

```cpp
esp_err_t err = faces_iap_upgrade_by_variant(
    bus_handle,
    M5Faces_Keyboard3::MODEL_ID,
    "V03",
    GPIO_NUM_12,
    GPIO_NUM_11,
    M5FACES_BOTTOM3_ADDR,
    progress_callback,
    nullptr);
```

IAP is blocking and should run from a dedicated FreeRTOS task. The Arduino/PIO
entry requires M5Unified and accepts `&M5.In_I2C`; the native ESP-IDF entry
accepts `i2c_master_bus_handle_t`. After flashing, the library re-reads the model
ID and firmware-version registers before returning success. Choose the variant
that matches the hardware revision and production release matrix. The library
does not infer a variant from the `3` suffix.

The current controlled target is `V03/0x03` for Calculator3 and Keyboard3, and
`V04/0x04` for Gamepad3. Source commits and binary SHA-256 values are recorded
in the PlatformIO validation README.