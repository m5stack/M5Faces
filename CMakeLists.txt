idf_component_register(
    SRCS
        "src/M5FacesBase.cpp"
        "src/M5FacesPoller.cpp"
        "src/M5Faces_Calculator3.cpp"
        "src/M5Faces_Gamepad3.cpp"
        "src/M5Faces_Keyboard3.cpp"
        "src/iap/src/M5Faces_IAP.cpp"
        "src/iap/src/M5Faces_IAP_Calculator3.cpp"
        "src/iap/src/M5Faces_IAP_Gamepad3.cpp"
        "src/iap/src/M5Faces_IAP_Keyboard3.cpp"
    INCLUDE_DIRS
        "src"
        "src/iap/src"
        "src/iap/generated"
        "src/iap/M5Faces_Calculator3"
        "src/iap/M5Faces_Gamepad3"
        "src/iap/M5Faces_Keyboard3"
    REQUIRES
        driver
        esp_timer
        freertos
)

# Enable the M5Unified I2C backend when the application provides M5Unified,
# while keeping M5Faces usable as a standalone ESP-IDF component.
if(COMMAND idf_component_optional_requires)
    idf_component_optional_requires(
        PRIVATE
            m5stack__m5unified
            m5unified
            M5Unified
    )
endif()
