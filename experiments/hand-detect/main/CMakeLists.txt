idf_component_register(SRCS "app_main.cpp" INCLUDE_DIRS "." REQUIRES esp_timer esp_driver_gpio esp_driver_usb_serial_jtag)

# Optional local RGB888 fixture (240x320) for repeatable rotation comparisons.
# Images stay outside source control; CI builds without this option.
set(HAND_EVAL_IMAGE "" CACHE FILEPATH "Local 240x320 RGB888 fixture")
if(HAND_EVAL_IMAGE)
    file(SIZE "${HAND_EVAL_IMAGE}" fixture_size)
    if(NOT fixture_size EQUAL 230400)
        message(FATAL_ERROR "HAND_EVAL_IMAGE must be 240x320 RGB888 (230400 bytes)")
    endif()
    target_add_binary_data(${COMPONENT_LIB} "${HAND_EVAL_IMAGE}" BINARY RENAME_TO hand_fixture)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE HAND_EVAL_FIXTURE=1)
endif()
