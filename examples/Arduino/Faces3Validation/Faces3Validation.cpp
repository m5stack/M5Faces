#ifdef ARDUINO
#include <Arduino.h>
#include <Wire.h>
#else
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#endif

#include <M5Faces.h>
#include <M5FacesPoller.hpp>
#include <M5Unified.h>

#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <nvs.h>

#ifndef ARDUINO
static inline uint32_t millis()
{
    return static_cast<uint32_t>(m5gfx::millis());
}

static inline void delay(uint32_t milliseconds)
{
    m5gfx::delay(milliseconds);
}

template <typename T>
static inline T constrain(T value, T minimum, T maximum)
{
    return value < minimum ? minimum : (value > maximum ? maximum : value);
}
#endif

#if (defined(ARDUINO_M5STACK_Core2) || defined(ARDUINO_M5STACK_CORE2)) && !defined(M5FACES_HOST_CORE2)
#define M5FACES_HOST_CORE2 1
#endif

#if defined(ARDUINO_M5STACK_CORES3) && !defined(M5FACES_HOST_CORES3)
#define M5FACES_HOST_CORES3 1
#endif

#ifndef M5FACES_VALIDATION_FW
#define M5FACES_VALIDATION_FW "ARDUINO2.9.0"
#endif

namespace {

constexpr uint32_t kI2CFreq = 400000;
#if defined(M5FACES_HOST_CORES3)
constexpr int kFacesSda = 12;
constexpr int kFacesScl = 11;
#else
constexpr int kFacesSda = 21;
constexpr int kFacesScl = 22;
#endif
constexpr uint8_t kAppAddress  = M5FACES_BOTTOM3_ADDR;
constexpr uint8_t kBootAddress = 0x54;
constexpr size_t kMaxKeys      = 35;
#if defined(M5FACES_HOST_CORES3)
constexpr int kHostTouchY  = 220;
constexpr int kErrorTouchY = 202;
#endif

enum class RunState { waiting, testing, error };
enum class VersionDecision { up_to_date, update, unsupported };

struct FirmwareImage {
    FirmwareImage() : size(0), target(0), variant(nullptr)
    {
    }
    FirmwareImage(size_t size_in, uint8_t target_in, const char* variant_in)
        : size(size_in), target(target_in), variant(variant_in)
    {
    }

    size_t size;
    uint8_t target;
    const char* variant;
};

const char* const kCalcLabels[20] = {"AC", "M", "%", "/", "7", "8", "9", "X", "4",   "5",
                                     "6",  "-", "1", "2", "3", "+", ".", "0", "-/+", "="};
const uint8_t kCalcCodes[20]      = {'A', 'M', '%', '/', '7', '8', '9', '*', '4', '5',
                                     '6', '-', '1', '2', '3', '+', '.', '0', '`', '='};

const char* const kGamepadLabels[8] = {"UP", "DOWN", "LEFT", "RIGHT", "A", "B", "SELECT", "START"};
const uint8_t kGamepadMasks[8]      = {GAMEPAD3_BTN_UP, GAMEPAD3_BTN_DOWN, GAMEPAD3_BTN_LEFT,   GAMEPAD3_BTN_RIGHT,
                                       GAMEPAD3_BTN_A,  GAMEPAD3_BTN_B,    GAMEPAD3_BTN_SELECT, GAMEPAD3_BTN_START};

const char* const kKeyboardLabels[35] = {"Q", "W", "E", "R", "T", "Y",   "U",  "I",   "O",   "P",   "A", "S",
                                         "D", "F", "G", "H", "J", "K",   "L",  "DEL", "ALT", "Z",   "X", "C",
                                         "V", "B", "N", "M", "$", "ENT", "aA", "0",   "SPC", "SYM", "FN"};

M5Faces_Calculator3 g_calculator;
M5Faces_Gamepad3 g_gamepad;
M5Faces_Keyboard3 g_keyboard;
M5FacesPoller g_poller;
RunState g_state                                 = RunState::waiting;
uint8_t g_device_addr                            = kAppAddress;
uint8_t g_model                                  = 0;
uint8_t g_fw                                     = 0;
bool g_tested[kMaxKeys]                          = {};
bool g_pressed[kMaxKeys]                         = {};
bool g_drawn_tested[kMaxKeys]                    = {};
bool g_drawn_pressed[kMaxKeys]                   = {};
bool g_pass_logged                               = false;
bool g_auto_probe                                = true;
bool g_detect_error_logged                       = false;
bool g_test_screen_ready                         = false;
bool g_iap_screen_ready                          = false;
bool g_iap_pending                               = false;
bool g_iap_resume_verify                         = false;
uint8_t g_iap_pending_target                     = 0;
bool g_keyboard_uppercase                        = false;
bool g_keyboard_aa_down                          = false;
bool g_keyboard_layers[5]                        = {};
bool g_keyboard_normal_seen                      = false;
bool g_api_ready                                 = false;
size_t g_api_passed                              = 0;
size_t g_api_total                               = 0;
m5faces_mode_t g_input_mode                      = M5FACES_MODE_NORMAL;
uint8_t g_last_calc_key                          = 0;
uint32_t g_last_probe_ms                         = 0;
char g_status[128]                               = "Waiting for Faces3";
char g_uid_text[M5FACES_DEVICE_UID_SIZE * 2 + 1] = "Not read";

void log_pt(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
}

void set_status(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(g_status, sizeof(g_status), fmt, args);
    va_end(args);
}

const char* model_name()
{
    return M5FacesBase::modelName(g_model);
}

size_t key_count()
{
    if (g_model == M5Faces_Calculator3::MODEL_ID) return 20;
    if (g_model == M5Faces_Gamepad3::MODEL_ID) return 8;
    if (g_model == M5Faces_Keyboard3::MODEL_ID) return 35;
    return 0;
}

const char* key_label(size_t index)
{
    if (g_model == M5Faces_Calculator3::MODEL_ID) return kCalcLabels[index];
    if (g_model == M5Faces_Gamepad3::MODEL_ID) return kGamepadLabels[index];
    return kKeyboardLabels[index];
}

size_t tested_count()
{
    size_t count = 0;
    for (size_t i = 0; i < key_count(); ++i) count += g_tested[i] ? 1U : 0U;
    return count;
}

M5FacesBase* active_driver()
{
    if (g_model == M5Faces_Calculator3::MODEL_ID) return &g_calculator;
    if (g_model == M5Faces_Gamepad3::MODEL_ID) return &g_gamepad;
    if (g_model == M5Faces_Keyboard3::MODEL_ID) return &g_keyboard;
    return nullptr;
}

size_t keyboard_feature_count()
{
    size_t count = g_keyboard_normal_seen ? 1U : 0U;
    for (bool tested : g_keyboard_layers) count += tested ? 1U : 0U;
    return count;
}

bool api_check(const char* name, bool passed, const char* detail = nullptr)
{
    ++g_api_total;
    if (passed) ++g_api_passed;
    log_pt("FV_API name=%s result=%s%s%s", name, passed ? "PASS" : "FAIL", detail ? " detail=" : "",
           detail ? detail : "");
    if (!passed) set_status("API check failed: %s", name);
    return passed;
}

bool runtime_check(const char* name, bool passed)
{
    log_pt("FV_RUNTIME_API name=%s result=%s", name, passed ? "PASS" : "FAIL");
    if (!passed) {
        g_api_ready = false;
        set_status("Runtime API failed: %s", name);
    }
    return passed;
}

bool probe(uint8_t address)
{
    return M5.In_I2C.scanID(address, kI2CFreq);
}

bool wait_for_address(uint8_t address, uint32_t timeout_ms)
{
    const uint32_t start = millis();
    while (millis() - start < timeout_ms) {
        if (probe(address)) return true;
        delay(100);
    }
    return false;
}

struct IapPendingRecord {
    uint8_t model   = 0;
    uint8_t target  = 0;
    uint8_t address = 0;
};

struct AddressRecoveryRecord {
    uint8_t model     = 0;
    uint8_t original  = 0;
    uint8_t temporary = 0;
};

bool load_address_recovery(AddressRecoveryRecord& record)
{
    nvs_handle_t handle = 0;
    if (nvs_open("M5FacesIAP", NVS_READONLY, &handle) != ESP_OK) return false;
    const bool loaded = nvs_get_u8(handle, "addr_model", &record.model) == ESP_OK &&
                        nvs_get_u8(handle, "addr_orig", &record.original) == ESP_OK &&
                        nvs_get_u8(handle, "addr_temp", &record.temporary) == ESP_OK;
    nvs_close(handle);
    return loaded;
}

bool save_address_recovery(uint8_t model, uint8_t original, uint8_t temporary)
{
    nvs_handle_t handle = 0;
    if (nvs_open("M5FacesIAP", NVS_READWRITE, &handle) != ESP_OK) return false;
    esp_err_t result = nvs_set_u8(handle, "addr_model", model);
    if (result == ESP_OK) result = nvs_set_u8(handle, "addr_orig", original);
    if (result == ESP_OK) result = nvs_set_u8(handle, "addr_temp", temporary);
    if (result == ESP_OK) result = nvs_commit(handle);
    nvs_close(handle);
    return result == ESP_OK;
}

bool clear_address_recovery()
{
    nvs_handle_t handle         = 0;
    const esp_err_t open_result = nvs_open("M5FacesIAP", NVS_READWRITE, &handle);
    if (open_result == ESP_ERR_NVS_NOT_FOUND) return true;
    if (open_result != ESP_OK) return false;
    const char* keys[] = {"addr_model", "addr_orig", "addr_temp"};
    esp_err_t result   = ESP_OK;
    for (const char* key : keys) {
        if (result != ESP_OK) break;
        result = nvs_erase_key(handle, key);
        if (result == ESP_ERR_NVS_NOT_FOUND) result = ESP_OK;
    }
    if (result == ESP_OK) result = nvs_commit(handle);
    nvs_close(handle);
    return result == ESP_OK;
}

bool load_iap_pending(IapPendingRecord& record)
{
    nvs_handle_t handle = 0;
    if (nvs_open("M5FacesIAP", NVS_READONLY, &handle) != ESP_OK) return false;
    const bool loaded = nvs_get_u8(handle, "model", &record.model) == ESP_OK &&
                        nvs_get_u8(handle, "target", &record.target) == ESP_OK &&
                        nvs_get_u8(handle, "address", &record.address) == ESP_OK;
    nvs_close(handle);
    return loaded;
}

bool save_iap_pending(uint8_t model, uint8_t target, uint8_t address)
{
    nvs_handle_t handle = 0;
    if (nvs_open("M5FacesIAP", NVS_READWRITE, &handle) != ESP_OK) return false;
    esp_err_t result = nvs_set_u8(handle, "model", model);
    if (result == ESP_OK) result = nvs_set_u8(handle, "target", target);
    if (result == ESP_OK) result = nvs_set_u8(handle, "address", address);
    if (result == ESP_OK) result = nvs_commit(handle);
    nvs_close(handle);
    return result == ESP_OK;
}

bool load_iap_identity(uint8_t& model, uint8_t& address)
{
    nvs_handle_t handle = 0;
    if (nvs_open("M5FacesIAP", NVS_READONLY, &handle) != ESP_OK) return false;
    const bool loaded =
        nvs_get_u8(handle, "known_model", &model) == ESP_OK && nvs_get_u8(handle, "known_addr", &address) == ESP_OK;
    nvs_close(handle);
    return loaded;
}

bool save_iap_identity(uint8_t model, uint8_t address)
{
    nvs_handle_t handle = 0;
    if (nvs_open("M5FacesIAP", NVS_READWRITE, &handle) != ESP_OK) return false;
    esp_err_t result = nvs_set_u8(handle, "known_model", model);
    if (result == ESP_OK) result = nvs_set_u8(handle, "known_addr", address);
    if (result == ESP_OK) result = nvs_commit(handle);
    nvs_close(handle);
    return result == ESP_OK;
}

bool clear_iap_pending()
{
    nvs_handle_t handle         = 0;
    const esp_err_t open_result = nvs_open("M5FacesIAP", NVS_READWRITE, &handle);
    if (open_result == ESP_ERR_NVS_NOT_FOUND) return true;
    if (open_result != ESP_OK) return false;
    esp_err_t result = nvs_erase_key(handle, "model");
    if (result == ESP_ERR_NVS_NOT_FOUND) result = ESP_OK;
    if (result == ESP_OK) {
        result = nvs_erase_key(handle, "target");
        if (result == ESP_ERR_NVS_NOT_FOUND) result = ESP_OK;
    }
    if (result == ESP_OK) {
        result = nvs_erase_key(handle, "address");
        if (result == ESP_ERR_NVS_NOT_FOUND) result = ESP_OK;
    }
    if (result == ESP_OK) result = nvs_commit(handle);
    nvs_close(handle);
    return result == ESP_OK;
}

bool recover_pending_address()
{
    AddressRecoveryRecord record;
    if (!load_address_recovery(record)) return true;
    const bool record_valid = record.model >= M5Faces_Calculator3::MODEL_ID &&
                              record.model <= M5Faces_Gamepad3::MODEL_ID && record.original >= 0x08 &&
                              record.original < 0x78 && record.temporary >= 0x08 && record.temporary < 0x78 &&
                              record.original != record.temporary && record.original != kBootAddress &&
                              record.temporary != kBootAddress;
    if (!record_valid) {
        set_status("Invalid address recovery record; clear NVS");
        log_pt("FV_ERROR stage=address_recovery_record model=0x%02X original=0x%02X temporary=0x%02X",
               record.model, record.original, record.temporary);
        return false;
    }

    const bool original_present  = probe(record.original);
    const bool temporary_present = probe(record.temporary);
    if (original_present && !temporary_present) {
        const bool cleared = clear_address_recovery();
        log_pt("FV_ADDR_RECOVERY state=already_restored original=0x%02X clear=%s", record.original,
               cleared ? "PASS" : "FAIL");
        return cleared;
    }
    if (!temporary_present || original_present) {
        set_status("Address recovery device state is ambiguous");
        log_pt("FV_ERROR stage=address_recovery_probe original=%d temporary=%d", original_present ? 1 : 0,
               temporary_present ? 1 : 0);
        return false;
    }

    M5FacesBase device;
    uint8_t model = 0;
    const bool restored = device.begin(&M5.In_I2C, record.temporary, M5FACES_I2C_FREQ_STANDARD) == M5FACES_OK &&
                          device.getModelID(&model) == M5FACES_OK && model == record.model &&
                          device.setI2CAddress(record.original) == M5FACES_OK &&
                          wait_for_address(record.original, 1000);
    const bool cleared = restored && clear_address_recovery();
    log_pt("FV_ADDR_RECOVERY state=restore model=0x%02X temporary=0x%02X original=0x%02X restore=%s clear=%s",
           record.model, record.temporary, record.original, restored ? "PASS" : "FAIL", cleared ? "PASS" : "FAIL");
    if (!cleared) set_status(restored ? "Address restored; recovery record clear failed" : "Address recovery failed");
    return cleared;
}

uint8_t latest_version(uint8_t model)
{
    if (model == M5Faces_Gamepad3::MODEL_ID) return 0x04;
    if (model == M5Faces_Calculator3::MODEL_ID || model == M5Faces_Keyboard3::MODEL_ID) return 0x03;
    return 0;
}

const char* latest_variant(uint8_t model)
{
    return model == M5Faces_Gamepad3::MODEL_ID ? "V04" : "V03";
}

VersionDecision select_image(uint8_t model, uint8_t current, uint8_t forced_target, FirmwareImage& image)
{
    const uint8_t target = latest_version(model);
    if (target == 0) return VersionDecision::unsupported;
    if (forced_target == 0 && current == target) return VersionDecision::up_to_date;
    if (forced_target != 0 && forced_target != target) return VersionDecision::unsupported;

    const faces_iap_fw_t fw               = faces_iap_find_firmware(model, latest_variant(model));
    const faces_iap_firmware_info_t* info = faces_iap_get_firmware_info(fw);
    if (!info) return VersionDecision::unsupported;
    image = FirmwareImage(info->size, target, info->variant_name);
    return VersionDecision::update;
}

void draw_update_screen(const char* stage, int percent, const FirmwareImage& image, uint32_t color = TFT_YELLOW)
{
    g_test_screen_ready = false;
    M5.Display.startWrite();
    char line[72];
    if (!g_iap_screen_ready) {
        M5.Display.fillScreen(TFT_BLACK);
        M5.Display.setFont(&fonts::efontCN_16_b);
        M5.Display.setTextDatum(textdatum_t::top_center);
        M5.Display.setTextColor(TFT_YELLOW);
        M5.Display.drawString("M5Faces3 Firmware Update", 160, 8);

        snprintf(line, sizeof(line), "%s  Current: 0x%02X", model_name(), g_fw);
        M5.Display.setFont(&fonts::efontCN_16);
        M5.Display.setTextColor(TFT_WHITE);
        M5.Display.drawString(line, 160, 48);
        snprintf(line, sizeof(line), "Target: 0x%02X  Image: %s", image.target, image.variant);
        M5.Display.drawString(line, 160, 76);
        M5.Display.drawRoundRect(16, 150, 288, 24, 4, 0x5A5A5A);
        M5.Display.setFont(&fonts::efontCN_12);
        M5.Display.setTextDatum(textdatum_t::bottom_center);
        M5.Display.setTextColor(0xBDBDBD);
        M5.Display.drawString("Keep power and Faces connected", 160, 228);
        g_iap_screen_ready = true;
    }

    M5.Display.fillRect(0, 104, 320, 28, TFT_BLACK);
    M5.Display.setFont(&fonts::efontCN_16);
    M5.Display.setTextDatum(textdatum_t::top_center);
    M5.Display.setTextColor(color);
    M5.Display.drawString(stage, 160, 108);

    M5.Display.fillRect(18, 152, 284, 20, TFT_BLACK);
    const int width = constrain(percent, 0, 100) * 284 / 100;
    if (width > 0) M5.Display.fillRoundRect(18, 152, width, 20, 3, TFT_GREEN);
    snprintf(line, sizeof(line), "%d%%", constrain(percent, 0, 100));
    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.drawString(line, 160, 162);
    M5.Display.endWrite();
}

void library_iap_progress(int percent, const char* status, void* ctx)
{
    const FirmwareImage& image = *static_cast<const FirmwareImage*>(ctx);
    const char* stage          = percent >= 99 ? "Verifying" : (percent > 0 ? "Writing" : "Preparing");
    draw_update_screen(stage, percent, image);
    log_pt("IAP_LIBRARY_PROGRESS model=%s image=%s percent=%d status=%s", model_name(), image.variant, percent,
           status ? status : "");
}

bool upgrade_firmware(const FirmwareImage& image)
{
    g_iap_screen_ready = false;
    draw_update_screen("Preparing", 0, image);
    log_pt("IAP_START model=%s id=0x%02X from=0x%02X target=0x%02X image=%s bytes=%u", model_name(), g_model, g_fw,
           image.target, image.variant, static_cast<unsigned>(image.size));
#ifdef ARDUINO
    log_pt("IAP_BACKEND framework=Arduino api=faces_iap_upgrade_by_variant bus=M5.In_I2C");
    const esp_err_t result =
        faces_iap_upgrade_by_variant(&M5.In_I2C, g_model, image.variant, kFacesSda, kFacesScl, g_device_addr,
                                     library_iap_progress, const_cast<FirmwareImage*>(&image));
#else
    log_pt("IAP_BACKEND framework=ESP-IDF api=faces_iap_upgrade_by_variant bus=i2c_master");
    const i2c_port_t port = M5.In_I2C.getPort();
    M5.In_I2C.release();
    delay(20);

    i2c_master_bus_config_t bus_config      = {};
    bus_config.i2c_port                     = static_cast<i2c_port_num_t>(port);
    bus_config.sda_io_num                   = static_cast<gpio_num_t>(kFacesSda);
    bus_config.scl_io_num                   = static_cast<gpio_num_t>(kFacesScl);
    bus_config.clk_source                   = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt            = 7;
    bus_config.flags.enable_internal_pullup = true;

    i2c_master_bus_handle_t iap_bus = nullptr;
    esp_err_t result                = i2c_new_master_bus(&bus_config, &iap_bus);
    if (result == ESP_OK) {
        result = faces_iap_upgrade_by_variant(iap_bus, g_model, image.variant, static_cast<gpio_num_t>(kFacesSda),
                                              static_cast<gpio_num_t>(kFacesScl), g_device_addr, library_iap_progress,
                                              const_cast<FirmwareImage*>(&image));
        i2c_del_master_bus(iap_bus);
    }
    const bool restored = M5.In_I2C.begin(port, kFacesSda, kFacesScl);
    if (result == ESP_OK && !restored) result = ESP_FAIL;
#endif

    if (result != ESP_OK) {
        log_pt("IAP_LIBRARY_RESULT model=%s image=%s result=FAIL error=%s", model_name(), image.variant,
               esp_err_to_name(result));
        log_pt("FV_ERROR stage=iap_library model=%s error=%s", model_name(), esp_err_to_name(result));
        return false;
    }
    log_pt("IAP_LIBRARY_RESULT model=%s image=%s result=PASS", model_name(), image.variant);
    return true;
}

bool verify_device(uint8_t expected_version)
{
    M5FacesBase device;
    if (device.begin(&M5.In_I2C, g_device_addr, 100000) != M5FACES_OK) return false;
    uint8_t model   = 0;
    uint8_t version = 0;
    if (device.getFirmwareVersion(&version) != M5FACES_OK || device.getModelID(&model) != M5FACES_OK ||
        version != expected_version)
        return false;
    if (model != g_model) return false;

    uint8_t uid[M5FACES_DEVICE_UID_SIZE] = {};
    if (device.getDeviceUID(uid) != M5FACES_OK) return false;
    bool uid_has_data = false;
    for (uint8_t value : uid) {
        uid_has_data = uid_has_data || (value != 0x00 && value != 0xFF);
    }
    if (!uid_has_data) return false;
    // The controlled Keyboard3 V03 image implements the 0xF0 operation-mode register.
    if (g_model == M5Faces_Keyboard3::MODEL_ID) {
        if (device.setMode(M5FACES_MODE_NORMAL) != M5FACES_OK) return false;
        m5faces_mode_t mode = M5FACES_MODE_DIRECT;
        if (device.getMode(&mode) != M5FACES_OK || mode != M5FACES_MODE_NORMAL) return false;
    }
    g_fw = version;
    return true;
}

bool find_faces()
{
    if (!recover_pending_address()) {
        g_auto_probe = false;
        return false;
    }
    g_iap_pending        = false;
    g_iap_resume_verify  = false;
    g_iap_pending_target = 0;

    IapPendingRecord pending;
    bool pending_loaded = load_iap_pending(pending);
    const bool pending_shape_valid = pending.model >= M5Faces_Calculator3::MODEL_ID &&
                                     pending.model <= M5Faces_Gamepad3::MODEL_ID && pending.address >= 0x08 &&
                                     pending.address < 0x78 && pending.address != kBootAddress;
    const uint8_t pending_latest = latest_version(pending.model);
    const bool migratable_target = pending.target == 0xF1 ||
                                   (pending.model == M5Faces_Gamepad3::MODEL_ID && pending.target == 0x03);
    if (pending_loaded && pending_shape_valid && migratable_target && pending.target != pending_latest) {
        log_pt("IAP_PENDING_MIGRATE model=0x%02X old_target=0x%02X new_target=0x%02X addr=0x%02X", pending.model,
               pending.target, pending_latest, pending.address);
        pending.target = pending_latest;
        if (!save_iap_pending(pending.model, pending.target, pending.address)) {
            set_status("IAP recovery record migration failed");
            log_pt("FV_ERROR stage=iap_pending_migrate model=0x%02X addr=0x%02X", pending.model, pending.address);
            g_auto_probe = false;
            return false;
        }
    }
    if (pending_loaded && (!pending_shape_valid || pending.target != pending_latest)) {
        set_status("Invalid IAP recovery record; clear NVS");
        log_pt("FV_ERROR stage=iap_pending_invalid model=0x%02X target=0x%02X addr=0x%02X", pending.model,
               pending.target, pending.address);
        g_auto_probe = false;
        return false;
    }

    uint8_t known_model   = 0;
    uint8_t known_address = 0;
    bool identity_loaded  = load_iap_identity(known_model, known_address);
    if (identity_loaded && (known_model < M5Faces_Calculator3::MODEL_ID || known_model > M5Faces_Gamepad3::MODEL_ID ||
                            known_address < 0x08 || known_address >= 0x78 || known_address == kBootAddress)) {
        log_pt("FV_WARN stage=iap_identity_invalid model=0x%02X addr=0x%02X", known_model, known_address);
        identity_loaded = false;
    }

    auto inspect = [&](uint8_t address) {
        if (address < 0x08 || address >= 0x78 || address == kBootAddress || !probe(address)) return false;
        M5FacesBase device;
        if (device.begin(&M5.In_I2C, address, 100000) != M5FACES_OK) return false;
        uint8_t model                      = 0;
        uint8_t version                    = 0;
        const m5faces_err_t version_result = device.getFirmwareVersion(&version);
        const m5faces_err_t model_result   = device.getModelID(&model);
        log_pt("FV_PROBE addr=0x%02X model_result=%d model=0x%02X fw_result=%d fw=0x%02X", address,
               static_cast<int>(model_result), model, static_cast<int>(version_result), version);
        if (model_result != M5FACES_OK || version_result != M5FACES_OK) return false;
        const bool model_valid = model >= M5Faces_Calculator3::MODEL_ID && model <= M5Faces_Gamepad3::MODEL_ID;
        if (!model_valid) {
            if (version != 0xF1) return false;
            if (pending_loaded)
                model = pending.model;
            else if (identity_loaded)
                model = known_model;
            else {
                log_pt("FV_ERROR stage=legacy_identity_missing addr=0x%02X fw=0x%02X", address, version);
                return false;
            }
            log_pt("FV_LEGACY_IDENTITY addr=0x%02X fw=0x%02X model_hint=0x%02X source=%s", address, version, model,
                   pending_loaded ? "pending" : "known");
        }
        g_device_addr = address;
        g_model       = model;
        g_fw          = version;
        return true;
    };
    bool app_found             = false;
    const uint8_t candidates[] = {
        static_cast<uint8_t>(pending_loaded ? pending.address : 0U),
        static_cast<uint8_t>(identity_loaded ? known_address : 0U),
        0x08,
        0x09,
        0x10,
    };
    for (size_t i = 0; i < sizeof(candidates); ++i) {
        const uint8_t address = candidates[i];
        bool duplicate        = false;
        for (size_t j = 0; j < i; ++j) duplicate = duplicate || candidates[j] == address;
        if (duplicate) continue;
        if (inspect(address)) {
            app_found = true;
            break;
        }
    }

    if (app_found) {
        if (!pending_loaded) return true;
        if (pending.model != g_model) {
            set_status("IAP record/device mismatch");
            log_pt(
                "FV_ERROR stage=iap_pending_model_mismatch stored_model=0x%02X current_model=0x%02X stored_addr=0x%02X "
                "current_addr=0x%02X",
                pending.model, g_model, pending.address, g_device_addr);
            g_auto_probe = false;
            return false;
        }
        if (pending.address != g_device_addr) {
            if (!save_iap_pending(g_model, pending.target, g_device_addr)) {
                set_status("Failed to save IAP address recovery record");
                log_pt("FV_ERROR stage=iap_pending_address_save old_addr=0x%02X new_addr=0x%02X", pending.address,
                       g_device_addr);
                g_auto_probe = false;
                return false;
            }
            log_pt("IAP_RESUME_ADDRESS model=%s old_addr=0x%02X new_addr=0x%02X", model_name(), pending.address,
                   g_device_addr);
            pending.address = g_device_addr;
        }
        g_iap_pending        = true;
        g_iap_pending_target = pending.target;
        g_iap_resume_verify  = g_fw == pending.target;
        log_pt("IAP_RESUME phase=%s model=%s current=0x%02X target=0x%02X addr=0x%02X",
               g_iap_resume_verify ? "post_write_verify" : "application_retry", model_name(), g_fw, pending.target,
               g_device_addr);
        return true;
    }

    if (probe(kBootAddress)) {
        if (!pending_loaded) {
            set_status("Device in bootloader without recovery record");
            log_pt("FV_ERROR stage=bootloader_no_pending addr=0x%02X", kBootAddress);
            g_auto_probe = false;
            return false;
        }
        g_model              = pending.model;
        g_fw                 = 0;
        g_device_addr        = pending.address;
        g_iap_pending        = true;
        g_iap_pending_target = pending.target;
        log_pt("IAP_RESUME phase=bootloader_recovery model=%s target=0x%02X app_addr=0x%02X boot_addr=0x%02X",
               model_name(), pending.target, pending.address, kBootAddress);
        return true;
    }
    return false;
}

void draw_key(size_t index)
{
    int x = 0, y = 0, w = 0, h = 0;
    if (g_model == M5Faces_Calculator3::MODEL_ID) {
        x = 6 + static_cast<int>(index % 4) * 78;
        y = 78 + static_cast<int>(index / 4) * 26;
        w = 74;
        h = 22;
    } else if (g_model == M5Faces_Keyboard3::MODEL_ID) {
        if (index < 30) {
            x = 6 + static_cast<int>(index % 10) * 32;
            y = 82 + static_cast<int>(index / 10) * 30;
            w = 29;
            h = 24;
        } else {
            x = 6 + static_cast<int>(index - 30) * 61;
            y = 172;
            w = 58;
            h = 24;
        }
    } else {
        static const int xs[8] = {54, 54, 8, 100, 262, 214, 22, 178};
        static const int ys[8] = {90, 150, 120, 120, 96, 120, 178, 178};
        static const int ws[8] = {56, 56, 56, 56, 44, 44, 120, 120};
        x                      = xs[index];
        y                      = ys[index];
        w                      = ws[index];
        h                      = 24;
    }
    const uint32_t fill = g_pressed[index] ? 0xF2B134 : (g_tested[index] ? 0x1E8E5A : 0x2F3640);
    const uint32_t edge = g_pressed[index] ? 0xFFF3C4 : (g_tested[index] ? 0x7BE3B0 : 0x57606F);
    M5.Display.fillRoundRect(x, y, w, h, 4, fill);
    M5.Display.drawRoundRect(x, y, w, h, 4, edge);
    M5.Display.setFont(&fonts::efontCN_12);
    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.setTextColor(g_pressed[index] ? 0x1B1404 : TFT_WHITE);
    M5.Display.drawString(key_label(index), x + w / 2, y + h / 2);
}

void draw_test_header()
{
    M5.Display.fillRect(0, 25, 320, 49, TFT_BLACK);
    char info[112];
    if (g_model == M5Faces_Keyboard3::MODEL_ID) {
        snprintf(info, sizeof(info), "API:%u/%u Keys:%u/%u ID:0x%02X V:0x%02X Fn:%u/6 %s",
                 static_cast<unsigned>(g_api_passed), static_cast<unsigned>(g_api_total),
                 static_cast<unsigned>(tested_count()), static_cast<unsigned>(key_count()), g_model, g_fw,
                 static_cast<unsigned>(keyboard_feature_count()),
                 g_input_mode == M5FACES_MODE_DIRECT ? "Direct" : "Normal");
    } else {
        snprintf(info, sizeof(info), "API:%u/%u Keys:%u/%u ID:0x%02X V:0x%02X", static_cast<unsigned>(g_api_passed),
                 static_cast<unsigned>(g_api_total), static_cast<unsigned>(tested_count()),
                 static_cast<unsigned>(key_count()), g_model, g_fw);
    }
    M5.Display.setFont(g_model == M5Faces_Keyboard3::MODEL_ID ? &fonts::efontCN_12 : &fonts::efontCN_16);
    M5.Display.setTextDatum(textdatum_t::top_left);
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.drawString(info, 8, g_model == M5Faces_Keyboard3::MODEL_ID ? 30 : 27);
    M5.Display.setFont(&fonts::efontCN_16);
    M5.Display.setTextColor(tested_count() == key_count() ? TFT_GREEN : TFT_WHITE);
    M5.Display.drawString(g_status, 8, 51);
}

void draw_test_footer()
{
    M5.Display.fillRect(0, 205, 320, 35, 0x101010);
    M5.Display.setFont(&fonts::efontCN_12);
    M5.Display.setTextDatum(textdatum_t::top_center);
    M5.Display.setTextColor(TFT_WHITE);
    char uid_line[40];
    snprintf(uid_line, sizeof(uid_line), "UID: %s", g_uid_text);
    M5.Display.drawString(uid_line, 160, 207);
#if defined(M5FACES_HOST_CORES3)
    static const char* const labels[3] = {"Reset", "Mode", "API+Addr"};
    for (int i = 0; i < 3; ++i) {
        const int x        = i * 107;
        const int width    = i == 2 ? 106 : 107;
        const bool enabled = i != 1 || g_model == M5Faces_Keyboard3::MODEL_ID;
        M5.Display.fillRect(x, kHostTouchY, width, 20, enabled ? 0x26384A : 0x181818);
        M5.Display.drawRect(x, kHostTouchY, width, 20, enabled ? 0x78B7E5 : 0x3A3A3A);
        M5.Display.setTextColor(enabled ? TFT_WHITE : 0x6B6B6B);
        M5.Display.drawString(labels[i], x + width / 2, kHostTouchY + 3);
    }
#else
    M5.Display.setTextColor(TFT_YELLOW);
    M5.Display.drawString(g_model == M5Faces_Keyboard3::MODEL_ID ? "A Reset  B Mode  C API+Addr"
                                                                      : "A Reset Keys  C API+Addr",
                          160,
                          224);
#endif
}

void draw_test_screen(bool full_refresh = false)
{
    if (full_refresh || !g_test_screen_ready) {
        M5.Display.fillScreen(TFT_BLACK);
        M5.Display.setFont(&fonts::efontCN_16_b);
        M5.Display.setTextDatum(textdatum_t::top_center);
        M5.Display.setTextColor(TFT_YELLOW);
        char title[48];
        snprintf(title, sizeof(title), "M5Faces %s Validation", model_name());
        M5.Display.drawString(title, 160, 3);
        draw_test_footer();
        for (size_t i = 0; i < key_count(); ++i) {
            draw_key(i);
            g_drawn_tested[i]  = g_tested[i];
            g_drawn_pressed[i] = g_pressed[i];
        }
        g_test_screen_ready = true;
    } else {
        for (size_t i = 0; i < key_count(); ++i) {
            if (g_drawn_tested[i] == g_tested[i] && g_drawn_pressed[i] == g_pressed[i]) continue;
            draw_key(i);
            g_drawn_tested[i]  = g_tested[i];
            g_drawn_pressed[i] = g_pressed[i];
        }
    }
    draw_test_header();
}

void draw_error(const char* message)
{
    g_test_screen_ready = false;
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextDatum(textdatum_t::top_center);
    M5.Display.setFont(&fonts::efontCN_16_b);
    M5.Display.setTextColor(TFT_RED);
    M5.Display.drawString("M5Faces3 Validation Failed", 160, 18);
    M5.Display.setFont(&fonts::efontCN_16);
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.drawString(message, 160, 78);
    M5.Display.setTextColor(TFT_YELLOW);
#if defined(M5FACES_HOST_CORES3)
    M5.Display.drawString("Touch any bottom area to retry", 160, 170);
    M5.Display.setFont(&fonts::efontCN_16);
    for (int i = 0; i < 3; ++i) {
        const int x     = i * 107;
        const int width = i == 2 ? 106 : 107;
        M5.Display.fillRect(x, kErrorTouchY, width, 38, 0x26384A);
        M5.Display.drawRect(x, kErrorTouchY, width, 38, 0x78B7E5);
        M5.Display.setTextColor(TFT_WHITE);
        M5.Display.drawString("Retry", x + width / 2, kErrorTouchY + 9);
    }
#else
    M5.Display.drawString("Press host A/B/C to retry", 160, 170);
#endif
}

void reset_key_test()
{
    memset(g_tested, 0, sizeof(g_tested));
    memset(g_pressed, 0, sizeof(g_pressed));
    memset(g_keyboard_layers, 0, sizeof(g_keyboard_layers));
    g_last_calc_key        = 0;
    g_keyboard_uppercase   = false;
    g_keyboard_aa_down     = false;
    g_keyboard_normal_seen = false;
    g_pass_logged          = false;
    set_status("Test every key and function");
    log_pt("FV_RESET model=%s progress=0/%u", model_name(), static_cast<unsigned>(key_count()));
    draw_test_screen(true);
}

void check_pass()
{
    if (!g_api_ready || tested_count() != key_count()) return;
    if (g_model == M5Faces_Keyboard3::MODEL_ID && keyboard_feature_count() != 6) return;
    set_status("All library functions passed");
    if (!g_pass_logged) {
        g_pass_logged = true;
        log_pt("FV_PASS model=%s id=0x%02X fw=0x%02X addr=0x%02X testfw=%s", model_name(), g_model, g_fw, g_device_addr,
               M5FACES_VALIDATION_FW);
    }
}

bool init_driver()
{
    if (g_model == M5Faces_Calculator3::MODEL_ID) {
        g_input_mode = M5FACES_MODE_NORMAL;
        return g_calculator.begin(&M5.In_I2C, g_device_addr, 100000) == M5FACES_OK;
    }
    if (g_model == M5Faces_Gamepad3::MODEL_ID) {
        g_input_mode = M5FACES_MODE_NORMAL;
        return g_gamepad.begin(&M5.In_I2C, g_device_addr, 100000) == M5FACES_OK;
    }
    g_input_mode = M5FACES_MODE_DIRECT;
    return g_keyboard.begin(&M5.In_I2C, g_device_addr, 100000) == M5FACES_OK &&
           g_keyboard.setMode(M5FACES_MODE_DIRECT) == M5FACES_OK;
}

bool run_common_api_validation(bool include_address_change = false)
{
    M5FacesBase* device = active_driver();
    g_api_passed        = 0;
    g_api_total         = 0;
    g_api_ready         = false;
    bool all_passed     = true;
    auto check          = [&](const char* name, bool passed, const char* detail = nullptr) {
        all_passed = api_check(name, passed, detail) && all_passed;
    };

    check("active_driver", device != nullptr);
    if (!device) return false;

    M5FacesBase uninitialized;
    uint8_t byte = 0;
    check("not_initialized_error", uninitialized.getModelID(&byte) == M5FACES_ERR_NOT_INIT);
    check("initialized", device->isInitialized());
    check("model_name", strcmp(M5FacesBase::modelName(g_model), model_name()) == 0 &&
                            strcmp(M5FacesBase::modelName(0xFF), "Unknown") == 0);

    const char* expected_variant                    = latest_variant(g_model);
    const faces_iap_fw_t latest_image               = faces_iap_find_firmware(g_model, expected_variant);
    const faces_iap_firmware_info_t* latest_info    = faces_iap_get_firmware_info(latest_image);
    check("iap_firmware_catalog",
          faces_iap_get_firmware_count() == 3 && faces_iap_get_firmware_count() == FACES_IAP_FW_COUNT &&
              faces_iap_get_model_firmware_count(g_model) == 1 && latest_image != FACES_IAP_FW_INVALID &&
              latest_info != nullptr && latest_info->model_id == g_model &&
              strcmp(latest_info->variant_name, expected_variant) == 0);

    uint8_t model = 0;
    check("get_model", device->getModelID(&model) == M5FACES_OK && model == g_model);
    check("model_match", device->isModelMatch(g_model) && !device->isModelMatch(static_cast<uint8_t>(g_model ^ 0x7F)));
    if (g_model == M5Faces_Calculator3::MODEL_ID) {
        M5Faces_Gamepad3 wrong_driver;
        check("subclass_model_mismatch",
              wrong_driver.begin(&M5.In_I2C, g_device_addr, M5FACES_I2C_FREQ_STANDARD) == M5FACES_ERR_MISMATCH &&
                  !wrong_driver.isInitialized());
    } else {
        M5Faces_Calculator3 wrong_driver;
        check("subclass_model_mismatch",
              wrong_driver.begin(&M5.In_I2C, g_device_addr, M5FACES_I2C_FREQ_STANDARD) == M5FACES_ERR_MISMATCH &&
                  !wrong_driver.isInitialized());
    }
    check("supports_direct", device->supportsDirectMode() == (g_model == M5Faces_Keyboard3::MODEL_ID));

    uint8_t uid[M5FACES_DEVICE_UID_SIZE]           = {};
    const bool uid_read                            = device->getDeviceUID(uid) == M5FACES_OK;
    bool uid_has_data                              = false;
    char uid_text[M5FACES_DEVICE_UID_SIZE * 2 + 1] = {};
    for (size_t i = 0; i < M5FACES_DEVICE_UID_SIZE; ++i) {
        uid_has_data = uid_has_data || (uid[i] != 0x00 && uid[i] != 0xFF);
        snprintf(uid_text + i * 2, sizeof(uid_text) - i * 2, "%02X", uid[i]);
    }
    snprintf(g_uid_text, sizeof(g_uid_text), "%s", uid_read ? uid_text : "Read failed");
    check("device_uid", uid_read && uid_has_data, uid_text);

    uint8_t version = 0;
    uint8_t address = 0;
    check("firmware_version", device->getFirmwareVersion(&version) == M5FACES_OK && version == g_fw);
    check("i2c_address_read", device->getI2CAddress(&address) == M5FACES_OK && address == g_device_addr);
    version = 0;
    address = 0;
    check("version_addr_batch",
          device->getVersionAddr(&version, &address) == M5FACES_OK && version == g_fw && address == g_device_addr);

    byte = 0;
    check("raw_read", device->readReg(M5FACES_REG_MODEL_ID, &byte) == M5FACES_OK && byte == g_model);
    check("invalid_arguments",
          device->getFirmwareVersion(nullptr) == M5FACES_ERR_INVALID &&
              device->getMode(nullptr) == M5FACES_ERR_INVALID && device->getLED(nullptr) == M5FACES_ERR_INVALID &&
              device->getI2CAddress(nullptr) == M5FACES_ERR_INVALID &&
              device->getModeLED(nullptr, nullptr) == M5FACES_ERR_INVALID &&
              device->getVersionAddr(nullptr, nullptr) == M5FACES_ERR_INVALID &&
              device->readReg(M5FACES_REG_MODEL_ID, nullptr) == M5FACES_ERR_INVALID &&
              device->writeReg(M5FACES_REG_LED, nullptr) == M5FACES_ERR_INVALID &&
              device->setI2CAddress(0x07) == M5FACES_ERR_INVALID && device->setI2CAddress(0x78) == M5FACES_ERR_INVALID);

    check("freq_400k", device->setI2CFreq(M5FACES_I2C_FREQ_FAST) == M5FACES_OK &&
                           device->getI2CFreq() == M5FACES_I2C_FREQ_FAST && device->getModelID(&model) == M5FACES_OK &&
                           model == g_model);
    check("freq_100k", device->setI2CFreq(M5FACES_I2C_FREQ_STANDARD) == M5FACES_OK &&
                           device->getI2CFreq() == M5FACES_I2C_FREQ_STANDARD &&
                           device->getModelID(&model) == M5FACES_OK && model == g_model);

    if (g_model == M5Faces_Keyboard3::MODEL_ID) {
        m5faces_mode_t mode = M5FACES_MODE_DIRECT;
        check("mode_normal", device->setMode(M5FACES_MODE_NORMAL) == M5FACES_OK &&
                                 device->getMode(&mode) == M5FACES_OK && mode == M5FACES_MODE_NORMAL);
        check("mode_direct", device->setMode(M5FACES_MODE_DIRECT) == M5FACES_OK &&
                                 device->getMode(&mode) == M5FACES_OK && mode == M5FACES_MODE_DIRECT);
        g_input_mode = M5FACES_MODE_DIRECT;
    } else {
        g_input_mode = M5FACES_MODE_NORMAL;
        log_pt("FV_SKIP api=mode reason=model_has_no_mode_register model=%s", model_name());
    }

    if (g_model == M5Faces_Keyboard3::MODEL_ID) {
        uint8_t original_led = 0;
        const bool led_read  = device->getLED(&original_led) == M5FACES_OK;
        bool led_cycle       = led_read;
        for (uint8_t led = M5FACES_LED_OFF; led <= M5FACES_LED_ALT_FAST; ++led) {
            uint8_t readback = 0xFF;
            led_cycle        = device->setLED(led) == M5FACES_OK && device->getLED(&readback) == M5FACES_OK &&
                        readback == led && led_cycle;
            log_pt("FV_LED mode=0x%02X readback=0x%02X result=%s", led, readback, readback == led ? "PASS" : "FAIL");
            delay(60);
        }
        const bool led_restored = led_read && device->setLED(original_led) == M5FACES_OK &&
                                  device->writeReg(M5FACES_REG_LED, &original_led) == M5FACES_OK;
        check("led_modes_0_8", led_cycle && led_restored);

        m5faces_mode_t batch_mode = M5FACES_MODE_NORMAL;
        uint8_t led_mode          = 0;
        check("mode_led_batch", device->getModeLED(&batch_mode, &led_mode) == M5FACES_OK &&
                                    batch_mode == g_input_mode && led_mode == original_led);
    } else {
        log_pt("FV_SKIP api=led reason=model_has_no_led_register model=%s", model_name());
    }

    if (include_address_change) {
        uint8_t temporary_address = 0;
        for (uint8_t candidate = 0x09; candidate < 0x20; ++candidate) {
            if (candidate != g_device_addr && candidate != kBootAddress && !probe(candidate)) {
                temporary_address = candidate;
                break;
            }
        }
        bool address_cycle = temporary_address != 0 &&
                             save_address_recovery(g_model, g_device_addr, temporary_address);
        if (address_cycle) {
            address_cycle =
                device->setI2CAddress(temporary_address) == M5FACES_OK && wait_for_address(temporary_address, 1000);
            uint8_t moved_address = 0;
            address_cycle = device->getI2CAddress(&moved_address) == M5FACES_OK &&
                            moved_address == temporary_address && address_cycle;
            const bool restored =
                device->setI2CAddress(g_device_addr) == M5FACES_OK && wait_for_address(g_device_addr, 1000);
            const bool record_cleared = restored && clear_address_recovery();
            address_cycle             = record_cleared && address_cycle;
        }
        check("i2c_address_change_restore", address_cycle);
    } else {
        log_pt("FV_SKIP api=i2c_address_change_restore reason=user_trigger_required action=host_C");
    }

#ifdef ARDUINO
#if defined(M5FACES_HOST_CORE2) || defined(M5FACES_HOST_CORES3)
    // Core2/CoreS3 touch, system devices, and Faces share M5.In_I2C.
    // Rebinding those pins to Arduino Wire disables host input and power devices.
    log_pt("FV_SKIP api=arduino_wire_transport reason=host_devices_share_in_i2c");
#else
    const i2c_port_t in_i2c_port = M5.In_I2C.getPort();
    M5.In_I2C.release();
    delay(20);
    const bool wire_started = Wire.begin(kFacesSda, kFacesScl, M5FACES_I2C_FREQ_STANDARD);
    M5FacesBase wire_device;
    uint8_t wire_model   = 0;
    uint8_t wire_version = 0;
    bool wire_transport  = wire_started &&
                          wire_device.begin(&Wire, g_device_addr, M5FACES_I2C_FREQ_STANDARD) == M5FACES_OK &&
                          wire_device.getModelID(&wire_model) == M5FACES_OK && wire_model == g_model &&
                          wire_device.getFirmwareVersion(&wire_version) == M5FACES_OK && wire_version == g_fw &&
                          wire_device.setI2CFreq(M5FACES_I2C_FREQ_FAST) == M5FACES_OK &&
                          wire_device.getI2CFreq() == M5FACES_I2C_FREQ_FAST;
    Wire.end();
    delay(20);
    const bool in_i2c_restored = M5.In_I2C.begin(in_i2c_port, kFacesSda, kFacesScl);
    wire_transport = wire_transport && in_i2c_restored && device->setI2CFreq(M5FACES_I2C_FREQ_STANDARD) == M5FACES_OK &&
                     device->getModelID(&model) == M5FACES_OK && model == g_model;
    check("arduino_wire_transport", wire_transport);
#endif
#else
    log_pt("FV_SKIP api=arduino_wire_transport reason=esp_idf_native_m5unified_transport");
#endif
    check("interrupt_default_state",
          !device->hasInterruptPin() && !device->isInterruptMode() && device->isInterruptLow());

    if (g_model == M5Faces_Calculator3::MODEL_ID) {
        check("calculator_parser", strcmp(M5Faces_Calculator3::calc3_code_parse(CALC3_KEY_BS), "BS") == 0 &&
                                       strcmp(M5Faces_Calculator3::calc3_code_parse(CALC3_KEY_ENTER), "Enter") == 0 &&
                                       M5Faces_Calculator3::calc3_code_parse('1') == nullptr);
    } else if (g_model == M5Faces_Gamepad3::MODEL_ID) {
        char parsed[48]      = {};
        const uint8_t sample = static_cast<uint8_t>(~(GAMEPAD3_BTN_UP | GAMEPAD3_BTN_A));
        const int count      = M5Faces_Gamepad3::gamepad3_code_parse(sample, parsed, sizeof(parsed));
        struct {
            char text[4];
            uint8_t guard;
        } tiny = {{}, 0xA5};
        const int tiny_count = M5Faces_Gamepad3::gamepad3_code_parse(0x00, tiny.text, sizeof(tiny.text));
        check("gamepad_parser", count == 2 && strcmp(parsed, "UP+A") == 0 && tiny.text[3] == '\0' &&
                                    tiny.guard == 0xA5 && tiny_count == 8);
    } else {
        uint8_t sample[10] = {0x0A, 0x01, 0xFF, 0x03, 0xFF, 0x03, 0xFF, 0x03, 0xFF, 0x00};
        char parsed[48]    = {};
        M5Faces_Keyboard3::keyboard3_direct_parse(sample, parsed, sizeof(parsed));
        struct {
            char text[4];
            uint8_t guard;
        } tiny = {{}, 0xA5};
        uint8_t all_pressed[10] = {0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        M5Faces_Keyboard3::keyboard3_direct_parse(all_pressed, tiny.text, sizeof(tiny.text));
        check("keyboard_helpers", strcmp(M5Faces_Keyboard3::keyboard3_key_map(0, 9), "Q") == 0 &&
                                      strcmp(parsed, "Q") == 0 &&
                                      tiny.text[3] == '\0' && tiny.guard == 0xA5 &&
                                      M5Faces_Keyboard3::KEYMAP[0][M5Faces_Keyboard3::COL_DEFAULT] == 'q' &&
                                      M5Faces_Keyboard3::KEYMAP[31][M5Faces_Keyboard3::COL_SYM] == KEYBOARD3_KEY_ESC &&
                                      strcmp(M5Faces_Keyboard3::keyboard3_code_parse(KEYBOARD3_KEY_ESC), "ESC") == 0 &&
                                      strcmp(M5Faces_Keyboard3::keyboard3_code_parse(KEYBOARD3_FN_UP), "FN+UP") == 0);
    }

    log_pt("FV_SKIP api=rgb reason=arduino_build_no_neopixel_backend");
    log_pt("FV_SKIP api=interrupt reason=no_interrupt_gpio_configured");

    if (g_model == M5Faces_Gamepad3::MODEL_ID) {
        // Gamepad3 only prepares a new key byte when its IRQ event is pending.
        // Register/API reads leave their response in the slave TX buffer, so the
        // first blind-poll sample is synchronization data, not a key event.
        g_gamepad.update();
        log_pt("FV_INPUT_SYNC model=Gamepad3 raw=0x%02X dispatched=0", g_gamepad.getKey());
    }

    g_api_ready = all_passed;
    log_pt("FV_API_SUMMARY result=%s passed=%u total=%u", all_passed ? "PASS" : "FAIL",
           static_cast<unsigned>(g_api_passed), static_cast<unsigned>(g_api_total));
    return all_passed;
}

bool prepare_device()
{
    if (!find_faces()) {
        if (g_auto_probe) set_status("Faces3 not found; check connection");
        if (!g_detect_error_logged) {
            g_detect_error_logged = true;
            log_pt("FV_ERROR stage=detect sda=%d scl=%d", kFacesSda, kFacesScl);
        }
        return false;
    }
    g_detect_error_logged = false;
    g_auto_probe          = false;
    FirmwareImage image;
    const uint8_t forced_target    = g_iap_pending ? g_iap_pending_target : 0;
    const VersionDecision decision = select_image(g_model, g_fw, forced_target, image);
    const uint8_t target           = latest_version(g_model);
    const char* variant            = latest_variant(g_model);
    log_pt("FV_DETECT model=%s id=0x%02X fw=0x%02X addr=0x%02X decision=%d pending=%d", model_name(), g_model, g_fw,
           g_device_addr, static_cast<int>(decision), g_iap_pending ? 1 : 0);
    if (decision == VersionDecision::unsupported) {
        set_status("%s firmware not found for %s", variant, model_name());
        log_pt("FV_ERROR stage=version_branch model=0x%02X fw=0x%02X", g_model, g_fw);
        return false;
    }
    const uint8_t previous = g_fw;
    const bool recovery    = g_iap_pending;
    if (decision == VersionDecision::up_to_date) {
        log_pt("FV_VERSION model=%s current=0x%02X latest=0x%02X result=LATEST action=SKIP", model_name(), g_fw,
               target);
        set_status("Firmware 0x%02X is current; update skipped", target);
    } else if (g_iap_resume_verify) {
        log_pt("IAP_VERIFY_RESUME model=%s target=0x%02X addr=0x%02X", model_name(), image.target, g_device_addr);
        if (!verify_device(image.target)) {
            set_status("IAP post-write verification failed");
            log_pt("FV_ERROR stage=post_verify_resume model=%s target=0x%02X", model_name(), image.target);
            return false;
        }
    } else {
        log_pt("FV_VERSION model=%s current=0x%02X latest=0x%02X result=OUTDATED action=UPDATE", model_name(), g_fw,
               target);
        if (!g_iap_pending) {
            if (!save_iap_pending(g_model, image.target, g_device_addr)) {
                set_status("IAP record save failed; update blocked");
                log_pt("FV_ERROR stage=iap_pending_save model=%s target=0x%02X addr=0x%02X", model_name(), image.target,
                       g_device_addr);
                return false;
            }
            g_iap_pending        = true;
            g_iap_pending_target = image.target;
        }
        log_pt("IAP_UPDATE mode=%s model=%s before=0x%02X target=0x%02X image=%s", recovery ? "recovery" : "fresh",
               model_name(), previous, image.target, image.variant);
        if (!upgrade_firmware(image) || !verify_device(image.target)) {
            set_status("IAP write/verify failed; power-cycle to recover");
            log_pt("FV_ERROR stage=post_verify model=%s before=0x%02X target=0x%02X recovery=%d", model_name(),
                   previous, image.target, recovery ? 1 : 0);
            return false;
        }
    }
    if (!save_iap_identity(g_model, g_device_addr)) {
        set_status("IAP verified; device identity save failed");
        log_pt("FV_ERROR stage=iap_identity_save model=%s addr=0x%02X", model_name(), g_device_addr);
        return false;
    }
    if (g_iap_pending && !clear_iap_pending()) {
        set_status("IAP verified; recovery record clear failed");
        log_pt("FV_ERROR stage=iap_pending_clear model=%s target=0x%02X", model_name(), image.target);
        return false;
    }
    g_iap_pending        = false;
    g_iap_resume_verify  = false;
    g_iap_pending_target = 0;
    if (decision == VersionDecision::update) {
        log_pt("IAP_PASS model=%s before=0x%02X after=0x%02X image=%s recovery=%d", model_name(), previous, g_fw,
               image.variant, recovery ? 1 : 0);
    }
    if (!init_driver()) {
        set_status("%s driver initialization failed", model_name());
        log_pt("FV_ERROR stage=driver_begin model=%s", model_name());
        return false;
    }
    if (!run_common_api_validation()) {
        log_pt("FV_ERROR stage=api_validation model=%s passed=%u total=%u", model_name(),
               static_cast<unsigned>(g_api_passed), static_cast<unsigned>(g_api_total));
        return false;
    }
    reset_key_test();
    g_state = RunState::testing;
    return true;
}

void handle_calculator(uint8_t raw)
{
    memset(g_pressed, 0, sizeof(g_pressed));
    if (raw == 0) {
        runtime_check("calculator_release_state",
                      !g_calculator.isPressed() && g_calculator.getChar() == '\0' && g_calculator.keyChanged());
        if (g_last_calc_key) log_pt("FV_RELEASE model=Calculator3 raw=0x%02X", g_last_calc_key);
        g_last_calc_key = 0;
    } else {
        const char value   = g_calculator.getChar();
        const char* parsed = M5Faces_Calculator3::calc3_code_parse(raw);
        runtime_check("calculator_key_state",
                      g_calculator.isPressed() && g_calculator.keyChanged() && (parsed != nullptr || value != '\0'));
        g_last_calc_key = raw;
        for (size_t i = 0; i < 20; ++i) {
            if (kCalcCodes[i] == raw) {
                g_pressed[i] = true;
                g_tested[i]  = true;
                set_status("Key: %s  Output: %s", kCalcLabels[i], parsed ? parsed : kCalcLabels[i]);
                log_pt("FV_KEY model=Calculator3 key=%s raw=0x%02X char=0x%02X parsed=%s progress=%u/20",
                       kCalcLabels[i], raw, static_cast<unsigned char>(value), parsed ? parsed : "printable",
                       static_cast<unsigned>(tested_count()));
                break;
            }
        }
    }
    check_pass();
    draw_test_screen();
}

void handle_gamepad(uint8_t raw)
{
    char parsed[64]        = {};
    const int parsed_count = M5Faces_Gamepad3::gamepad3_code_parse(raw, parsed, sizeof(parsed));
    memset(g_pressed, 0, sizeof(g_pressed));
    size_t active         = 0;
    size_t active_index   = 0;
    bool state_consistent = g_gamepad.getKey() == raw && g_gamepad.keyChanged();
    for (size_t i = 0; i < 8; ++i) {
        g_pressed[i]      = (raw & kGamepadMasks[i]) == 0;
        const auto button = static_cast<gamepad3_btn_t>(kGamepadMasks[i]);
        state_consistent  = state_consistent && g_gamepad.isButtonPressed(button) == g_pressed[i];
        if (g_pressed[i]) {
            ++active;
            active_index = i;
        }
    }
    runtime_check("gamepad_state_parser", state_consistent && parsed_count == static_cast<int>(active));
    if (active == 0) {
        bool released_edge = false;
        for (size_t i = 0; i < 8; ++i) {
            released_edge =
                released_edge || g_gamepad.isButtonJustReleased(static_cast<gamepad3_btn_t>(kGamepadMasks[i]));
        }
        runtime_check("gamepad_release_edge", released_edge);
        log_pt("FV_RELEASE model=Gamepad3");
    } else if (active == 1) {
        runtime_check("gamepad_press_edge",
                      g_gamepad.isButtonJustPressed(static_cast<gamepad3_btn_t>(kGamepadMasks[active_index])));
        g_tested[active_index] = true;
        set_status("Key: %s  Parsed: %s", kGamepadLabels[active_index], parsed);
        log_pt("FV_KEY model=Gamepad3 key=%s raw=0x%02X parsed=%s progress=%u/8", kGamepadLabels[active_index], raw,
               parsed, static_cast<unsigned>(tested_count()));
    } else {
        set_status("Key combo ignored for progress");
        log_pt("FV_MULTI model=Gamepad3 raw=0x%02X active=%u", raw, static_cast<unsigned>(active));
    }
    check_pass();
    draw_test_screen();
}

int keyboard_index(const char* label)
{
    for (size_t i = 0; i < 35; ++i)
        if (strcmp(label, kKeyboardLabels[i]) == 0) return i;
    return -1;
}

void set_keyboard_pressed(const char* label, size_t& active)
{
    const int index = keyboard_index(label);
    if (index >= 0 && !g_pressed[index]) {
        g_pressed[index] = true;
        ++active;
    }
}

const char* keyboard_output_name(uint8_t code, char* buffer, size_t size)
{
    switch (code) {
        case 8:
            return "Backspace";
        case 9:
            return "Tab";
        case 13:
            return "Enter";
        case 127:
            return "Delete";
        case KEYBOARD3_FN_180:
            return "Reserved function 180";
        case KEYBOARD3_FN_181:
            return "Reserved function 181";
        case KEYBOARD3_FN_182:
            return "Reserved function 182";
        case KEYBOARD3_FN_UP:
            return "Arrow Up";
        case KEYBOARD3_FN_INSERT:
            return "Insert";
        case KEYBOARD3_FN_TAB:
            return "Tab";
        case KEYBOARD3_FN_HOME:
            return "Home";
        case KEYBOARD3_FN_END:
            return "End";
        case KEYBOARD3_FN_PAGE_UP:
            return "Page Up";
        case KEYBOARD3_FN_PAGE_DOWN:
            return "Page Down";
        case KEYBOARD3_FN_LEFT:
            return "Arrow Left";
        case KEYBOARD3_FN_DOWN:
            return "Arrow Down";
        case KEYBOARD3_FN_RIGHT:
            return "Arrow Right";
        case 255:
            return "Unmapped";
        default:
            break;
    }
    if (code == ' ') return "Space";
    if (code >= 0x20 && code < 0x7F) {
        snprintf(buffer, size, "%c", static_cast<char>(code));
        return buffer;
    }
    const char* parsed = M5Faces_Keyboard3::keyboard3_code_parse(code);
    if (parsed) return parsed;
    snprintf(buffer, size, "Code 0x%02X", code);
    return buffer;
}

const char* keyboard_modifier_name(const m5faces_direct_data_t& frame)
{
    if (frame.modifier.aA) return "Aa";
    if (frame.modifier.sym) return "SYM";
    if (frame.modifier.fn) return "FN";
    if (frame.modifier.alt) return "ALT";
    return nullptr;
}

void handle_keyboard_normal(uint8_t raw)
{
    memset(g_pressed, 0, sizeof(g_pressed));
    if (raw == KEYBOARD3_KEY_NONE) {
        runtime_check("keyboard_normal_release",
                      !g_keyboard.isPressed() && g_keyboard.getChar() == '\0' && g_keyboard.keyChanged());
        log_pt("FV_RELEASE model=Keyboard3 mode=normal");
    } else {
        char output_buffer[20] = {};
        const char* output     = keyboard_output_name(raw, output_buffer, sizeof(output_buffer));
        const bool printable   = raw >= 0x20 && raw < 0x7F;
        const char value       = g_keyboard.getChar();
        runtime_check("keyboard_normal_state", g_keyboard.isPressed() && g_keyboard.keyChanged() &&
                                                   g_keyboard.isPrintable() == printable &&
                                                   (printable ? value == static_cast<char>(raw) : true));
        g_keyboard_normal_seen = true;
        set_status("Normal mode: %s (0x%02X)", output, raw);
        log_pt("FV_KEY model=Keyboard3 mode=normal raw=0x%02X char=0x%02X printable=%d output=%s", raw,
               static_cast<unsigned char>(value), printable ? 1 : 0, output);
    }
    check_pass();
    draw_test_screen();
}

void handle_keyboard_direct(m5faces_direct_event_t event, const m5faces_direct_data_t& frame)
{
    memset(g_pressed, 0, sizeof(g_pressed));
    if (event == M5FACES_DIRECT_RELEASED) {
        g_keyboard_aa_down                    = false;
        const m5faces_direct_data_t& previous = g_keyboard.getDirectPrevData();
        runtime_check("keyboard_direct_release", previous.raw[0] == 0x0A);
        log_pt("FV_RELEASE model=Keyboard3 mode=direct held=%u",
               static_cast<unsigned>(g_keyboard.getDirectHoldCount()));
        draw_test_screen();
        return;
    }
    size_t active       = 0;
    size_t matrix_count = 0;
    int matrix_index    = -1;
    for (int row = 0; row < 3; ++row) {
        for (int bit = 0; bit < 10; ++bit) {
            if ((frame.rows[row].key_bits & (1U << bit)) == 0U) {
                const char* label = M5Faces_Keyboard3::keyboard3_key_map(row, bit);
                set_keyboard_pressed(label, active);
                matrix_index = keyboard_index(label);
                ++matrix_count;
            }
        }
    }
    if (frame.modifier.aA) set_keyboard_pressed("aA", active);
    if (frame.modifier.alt) set_keyboard_pressed("ALT", active);
    if (frame.modifier.enter) set_keyboard_pressed("ENT", active);
    if (frame.modifier.sym) set_keyboard_pressed("SYM", active);
    if (frame.modifier.fn) set_keyboard_pressed("FN", active);

    char direct_keys[96] = {};
    M5Faces_Keyboard3::keyboard3_direct_parse(frame.raw, direct_keys, sizeof(direct_keys));
    runtime_check("keyboard_direct_parser", active == 0 || direct_keys[0] != '\0');

    const size_t function_modifiers = static_cast<size_t>(frame.modifier.aA) + static_cast<size_t>(frame.modifier.alt) +
                                      static_cast<size_t>(frame.modifier.sym) + static_cast<size_t>(frame.modifier.fn);
    const size_t action_keys = matrix_count + static_cast<size_t>(frame.modifier.enter);
    const bool valid         = function_modifiers <= 1 && action_keys <= 1;
    const bool aa_rising     = frame.modifier.aA && !g_keyboard_aa_down;
    if (valid && aa_rising) g_keyboard_uppercase = !g_keyboard_uppercase;
    g_keyboard_aa_down = frame.modifier.aA;

    if (valid && active > 0) {
        for (size_t i = 0; i < 35; ++i) {
            if (g_pressed[i]) g_tested[i] = true;
        }

        if (matrix_count == 1 && matrix_index >= 0) {
            M5Faces_Keyboard3::KeymapCol column = M5Faces_Keyboard3::COL_DEFAULT;
            if (frame.modifier.alt)
                column = M5Faces_Keyboard3::COL_ALT;
            else if (frame.modifier.fn)
                column = M5Faces_Keyboard3::COL_FN;
            else if (frame.modifier.sym)
                column = M5Faces_Keyboard3::COL_SYM;
            else if (frame.modifier.aA || g_keyboard_uppercase)
                column = M5Faces_Keyboard3::COL_AA;

            const uint8_t code = M5Faces_Keyboard3::KEYMAP[matrix_index][column];
            if (code != 255) g_keyboard_layers[static_cast<size_t>(column)] = true;
            char output_buffer[20];
            const char* output   = keyboard_output_name(code, output_buffer, sizeof(output_buffer));
            const char* modifier = keyboard_modifier_name(frame);
            if (modifier) {
                set_status("Function: %s+%s -> %s", modifier, kKeyboardLabels[matrix_index], output);
                log_pt("FV_COMBO model=Keyboard3 combo=%s+%s output=%s code=0x%02X progress=%u/35", modifier,
                       kKeyboardLabels[matrix_index], output, code, static_cast<unsigned>(tested_count()));
            } else {
                set_status("Key: %s -> %s", kKeyboardLabels[matrix_index], output);
                log_pt("FV_KEY model=Keyboard3 key=%s output=%s code=0x%02X progress=%u/35",
                       kKeyboardLabels[matrix_index], output, code, static_cast<unsigned>(tested_count()));
            }
        } else if (frame.modifier.enter) {
            const char* modifier = keyboard_modifier_name(frame);
            if (modifier) {
                set_status("Function: %s+ENT -> Enter", modifier);
                log_pt("FV_COMBO model=Keyboard3 combo=%s+ENT output=Enter code=0x0D progress=%u/35", modifier,
                       static_cast<unsigned>(tested_count()));
            } else {
                set_status("Function: Enter");
                log_pt("FV_KEY model=Keyboard3 key=ENT output=Enter progress=%u/35",
                       static_cast<unsigned>(tested_count()));
            }
        } else if (frame.modifier.aA) {
            set_status("Aa case toggle: %s", g_keyboard_uppercase ? "Uppercase" : "Lowercase");
            log_pt("FV_FUNCTION model=Keyboard3 key=aA function=case_toggle state=%s progress=%u/35",
                   g_keyboard_uppercase ? "uppercase" : "lowercase", static_cast<unsigned>(tested_count()));
        } else {
            const char* modifier = keyboard_modifier_name(frame);
            set_status("Function layer: %s", modifier ? modifier : "Unknown");
            log_pt("FV_FUNCTION model=Keyboard3 key=%s function=layer progress=%u/35", modifier ? modifier : "unknown",
                   static_cast<unsigned>(tested_count()));
        }
    } else if (!valid) {
        set_status("Conflicting combinations do not count");
        log_pt("FV_MULTI model=Keyboard3 active=%u modifiers=%u actions=%u", static_cast<unsigned>(active),
               static_cast<unsigned>(function_modifiers), static_cast<unsigned>(action_keys));
    }
    check_pass();
    draw_test_screen();
}

void normal_key_callback(M5FacesBase*, uint8_t raw, void*)
{
    if (g_model == M5Faces_Calculator3::MODEL_ID)
        handle_calculator(raw);
    else if (g_model == M5Faces_Gamepad3::MODEL_ID)
        handle_gamepad(raw);
    else
        handle_keyboard_normal(raw);
}

void direct_event_callback(m5faces_direct_event_t event, const m5faces_direct_data_t* frame, uint32_t, void*)
{
    if (g_model == M5Faces_Keyboard3::MODEL_ID && frame) {
        handle_keyboard_direct(event, *frame);
    }
}

bool toggle_keyboard_mode()
{
    if (g_model != M5Faces_Keyboard3::MODEL_ID) return false;
    const m5faces_mode_t requested = g_input_mode == M5FACES_MODE_DIRECT ? M5FACES_MODE_NORMAL : M5FACES_MODE_DIRECT;
    m5faces_mode_t readback        = g_input_mode;
    if (g_keyboard.setMode(requested) != M5FACES_OK || g_keyboard.getMode(&readback) != M5FACES_OK ||
        readback != requested) {
        g_api_ready = false;
        set_status("Keyboard3 mode switch failed");
        log_pt("FV_ERROR stage=mode_toggle requested=%u readback=%u", static_cast<unsigned>(requested),
               static_cast<unsigned>(readback));
        return false;
    }
    g_input_mode       = requested;
    g_keyboard_aa_down = false;
    memset(g_pressed, 0, sizeof(g_pressed));
    set_status("Switched to %s mode", requested == M5FACES_MODE_DIRECT ? "Direct" : "Normal");
    log_pt("FV_MODE model=Keyboard3 mode=%s result=PASS", requested == M5FACES_MODE_DIRECT ? "direct" : "normal");
    draw_test_screen();
    return true;
}

struct HostControls {
    bool a = false;
    bool b = false;
    bool c = false;
};

HostControls read_host_controls()
{
    M5.update();
    HostControls controls;
#if defined(M5FACES_HOST_CORES3)
    const int touch_y = g_state == RunState::testing ? kHostTouchY : kErrorTouchY;
    for (uint8_t i = 0; i < M5.Touch.getCount(); ++i) {
        const auto& touch = M5.Touch.getDetail(i);
        if (!touch.wasPressed() || touch.y < touch_y) continue;
        const char zone = touch.x < 107 ? 'A' : (touch.x < 214 ? 'B' : 'C');
        controls.a      = zone == 'A';
        controls.b      = zone == 'B';
        controls.c      = zone == 'C';
        log_pt("FV_HOST_TOUCH x=%d y=%d zone=%c", touch.x, touch.y, zone);
        break;
    }
#else
    controls.a = M5.BtnA.wasPressed();
    controls.b = M5.BtnB.wasPressed();
    controls.c = M5.BtnC.wasPressed();
#endif
    return controls;
}

bool host_button_pressed()
{
    const HostControls controls = read_host_controls();
    if (controls.a || controls.b || controls.c) {
        log_pt("FV_HOST_BUTTON key=%c action=retry", controls.a ? 'A' : (controls.b ? 'B' : 'C'));
    }
    return controls.a || controls.b || controls.c;
}

void process_host_controls()
{
    const HostControls controls = read_host_controls();
    const bool button_a         = controls.a;
    const bool button_b         = controls.b;
    const bool button_c         = controls.c;
    if (button_a) {
        log_pt("FV_HOST_BUTTON key=A action=reset");
        reset_key_test();
    } else if (button_b && g_model == M5Faces_Keyboard3::MODEL_ID) {
        log_pt("FV_HOST_BUTTON key=B action=toggle_keyboard_mode");
        toggle_keyboard_mode();
    } else if (button_b) {
        log_pt("FV_HOST_BUTTON key=B action=none model=%s", model_name());
        set_status("Mode switch is Keyboard3 only");
        draw_test_header();
    } else if (button_c) {
        log_pt("FV_HOST_BUTTON key=C action=api_retest_with_address_cycle");
        if (!run_common_api_validation(true)) {
            g_state = RunState::error;
            draw_error(g_status);
            return;
        }
        set_status("API and address retest passed");
        log_pt("FV_API_RETEST result=PASS");
        draw_test_footer();
        draw_test_screen();
    }
}

bool force_host_board_cache()
{
#if defined(M5FACES_HOST_CORE2)
    constexpr m5::board_t host_board = m5::board_t::board_M5StackCore2;
#elif defined(M5FACES_HOST_CORES3)
    constexpr m5::board_t host_board = m5::board_t::board_M5StackCoreS3;
#else
    constexpr m5::board_t host_board = m5::board_t::board_M5Stack;
#endif
    nvs_handle_t handle = 0;
    if (nvs_open("M5GFX", NVS_READWRITE, &handle) != ESP_OK) return false;
    const esp_err_t set_result    = nvs_set_u32(handle, "AUTODETECT", static_cast<uint32_t>(host_board));
    const esp_err_t commit_result = set_result == ESP_OK ? nvs_commit(handle) : set_result;
    nvs_close(handle);
    return set_result == ESP_OK && commit_result == ESP_OK;
}

}  // namespace

void setup()
{
    const bool board_cache_ready = force_host_board_cache();
    auto config                  = M5.config();
    config.clear_display         = true;
    config.output_power          = true;
    config.internal_imu          = false;
    config.internal_rtc          = false;
    config.internal_mic          = false;
    config.internal_spk          = false;
    config.external_imu          = false;
    config.external_rtc          = false;
    config.external_spk          = false;
#if defined(M5FACES_HOST_CORE2)
    config.fallback_board = m5::board_t::board_M5StackCore2;
#elif defined(M5FACES_HOST_CORES3)
    config.fallback_board = m5::board_t::board_M5StackCoreS3;
#else
    config.fallback_board = m5::board_t::board_M5Stack;
#endif
    M5.begin(config);

    // Preserve M5Unified's native controller. Core2 and CoreS3 use their
    // internal bus for both host devices and Faces.
    bool i2c_ready = M5.In_I2C.isEnabled() && M5.In_I2C.getSDA() == kFacesSda && M5.In_I2C.getSCL() == kFacesScl;
    if (!i2c_ready) {
        if (M5.In_I2C.isEnabled()) M5.In_I2C.release();
#if defined(M5FACES_HOST_CORE2) || defined(M5FACES_HOST_CORES3)
        constexpr i2c_port_t faces_i2c_port = I2C_NUM_1;
#else
        constexpr i2c_port_t faces_i2c_port = I2C_NUM_0;
#endif
        i2c_ready = M5.In_I2C.begin(faces_i2c_port, kFacesSda, kFacesScl);
    }
    M5.Display.setRotation(1);
    M5.Display.setBrightness(128);
#ifdef ARDUINO
    Serial.begin(115200);
#endif
    g_poller.setPollingMode(true);
    g_poller.setNormalKeyCb(normal_key_callback);
    g_poller.setDirectEventCb(direct_event_callback);
#if defined(M5FACES_HOST_CORE2)
    const char* host_name = "Core2";
#elif defined(M5FACES_HOST_CORES3)
    const char* host_name = "CoreS3";
#else
    const char* host_name = "CoreBasic";
#endif
    log_pt("FV_BOOT board=%s targets=Calculator3,Keyboard3,Gamepad3 testfw=%s", host_name, M5FACES_VALIDATION_FW);
    log_pt("FV_BOARD_CACHE target=%s result=%s", host_name, board_cache_ready ? "PASS" : "FAIL");
    log_pt("FV_I2C bus=IN port=%d sda=%d scl=%d init=%s board_id=%d", static_cast<int>(M5.In_I2C.getPort()),
           M5.In_I2C.getSDA(), M5.In_I2C.getSCL(), i2c_ready ? "PASS" : "FAIL", static_cast<int>(M5.getBoard()));
    log_pt("FV_HOST_INPUT touch=%s touch_button_height=%u", M5.Touch.isEnabled() ? "enabled" : "disabled",
           static_cast<unsigned>(M5.getTouchButtonHeight()));
#if defined(M5FACES_HOST_CORES3)
    log_pt("FV_HOST_CONTROL source=bottom_touch init=%s y=%d zones=0-106,107-213,214-319",
           M5.Touch.isEnabled() ? "PASS" : "FAIL", kHostTouchY);
#endif
    if (!i2c_ready) {
        g_auto_probe = false;
        g_state      = RunState::error;
        set_status("I2C initialization failed: SDA%d / SCL%d", kFacesSda, kFacesScl);
        draw_error(g_status);
        return;
    }
    draw_error("Searching for Faces3...");
    if (!prepare_device()) {
        g_state = RunState::error;
        draw_error(g_status);
    }
}

void loop()
{
    if (g_state == RunState::testing) {
        g_poller.tick(active_driver(), g_input_mode, g_model);
        process_host_controls();
    } else if (host_button_pressed() || (g_auto_probe && millis() - g_last_probe_ms > 1000)) {
        g_last_probe_ms = millis();
        if (!prepare_device()) {
            g_state = RunState::error;
            draw_error(g_status);
        }
    }
    delay(5);
}
