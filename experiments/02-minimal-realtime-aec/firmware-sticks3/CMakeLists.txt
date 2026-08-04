cmake_minimum_required(VERSION 3.16)

# Use first-party checkouts from the normal Go-style source tree. CI or other
# machines can override either path without changing this project.
if(DEFINED ENV{M5UNIFIED_PATH})
    set(STACKCHAN_M5UNIFIED "$ENV{M5UNIFIED_PATH}")
else()
    set(STACKCHAN_M5UNIFIED
        "${CMAKE_CURRENT_LIST_DIR}/../../../../../m5stack/M5Unified")
endif()
if(DEFINED ENV{M5GFX_PATH})
    set(STACKCHAN_M5GFX "$ENV{M5GFX_PATH}")
else()
    set(STACKCHAN_M5GFX
        "${CMAKE_CURRENT_LIST_DIR}/../../../../../m5stack/M5GFX")
endif()

foreach(component_dir IN ITEMS "${STACKCHAN_M5UNIFIED}" "${STACKCHAN_M5GFX}")
    if(NOT EXISTS "${component_dir}/CMakeLists.txt")
        message(FATAL_ERROR
            "Missing first-party component checkout: ${component_dir}")
    endif()
    list(APPEND EXTRA_COMPONENT_DIRS "${component_dir}")
endforeach()

# Both libraries are supplied as inspected first-party checkouts above. Disable
# registry resolution so ESP-IDF does not compile a second managed M5GFX copy.
set(ENV{IDF_COMPONENT_MANAGER} 0)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(stackchan_sticks3_sprite)
