if(CONFIG_SHIELD_ERGODOX_DONGLE_DISPLAY)
  if(NOT DEFINED ZEPHYR_DONGLE_SCREEN_MODULE_DIR)
    set(ZEPHYR_DONGLE_SCREEN_MODULE_DIR ${ZEPHYR_BASE}/../zmk-dongle-screen)
  endif()

  set(DONGLE_SCREEN_SRC_DIR ${ZEPHYR_DONGLE_SCREEN_MODULE_DIR}/boards/shields/dongle_screen)

  zephyr_library()
  zephyr_library_sources(${ZEPHYR_BASE}/misc/empty_file.c)
  zephyr_library_include_directories(src)
  zephyr_library_include_directories(src/widgets)
  zephyr_library_include_directories(src/menu)
  zephyr_library_include_directories(${ZEPHYR_LVGL_MODULE_DIR})
  zephyr_library_include_directories(${ZEPHYR_BASE}/lib/gui/lvgl/)
  zephyr_library_include_directories(${ZEPHYR_BASE}/drivers)
  zephyr_library_include_directories(${CMAKE_SOURCE_DIR}/include)
  zephyr_library_include_directories(${ZEPHYR_BASE}/../zmk/app/include)
  zephyr_library_include_directories(${ZEPHYR_DONGLE_SCREEN_MODULE_DIR}/include)
  zephyr_library_include_directories(${DONGLE_SCREEN_SRC_DIR})
  zephyr_library_include_directories(${DONGLE_SCREEN_SRC_DIR}/include)
  zephyr_library_include_directories(${DONGLE_SCREEN_SRC_DIR}/src)

  zephyr_library_sources(
    ${DONGLE_SCREEN_SRC_DIR}/src/brightness.c
    src/custom_status_screen.c
    src/menu/menu_controller.c
    src/menu/layer_inspector.c
    ${DONGLE_SCREEN_SRC_DIR}/src/screen_rotate_init.c
    ${DONGLE_SCREEN_SRC_DIR}/src/widgets/output_status.c
    src/widgets/battery_status.c
    src/widgets/lock_status.c
    src/widgets/layer_badge.c
    src/widgets/bongo_cat.c
    src/widgets/wpm_gauge.c
    ${DONGLE_SCREEN_SRC_DIR}/src/widgets/layer_status.c
    ${DONGLE_SCREEN_SRC_DIR}/src/widgets/wpm_status.c
    ${DONGLE_SCREEN_SRC_DIR}/src/widgets/mod_status.c
    ${DONGLE_SCREEN_SRC_DIR}/src/fonts/NerdFonts_Regular_20.c
    ${DONGLE_SCREEN_SRC_DIR}/src/fonts/NerdFonts_Regular_40.c
  )
endif()
