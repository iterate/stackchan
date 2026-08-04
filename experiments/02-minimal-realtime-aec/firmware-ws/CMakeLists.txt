cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(stackchan_realtime_ws)

# esp_asrc 1.0.1 logs a uint32_t with "%d". Keep warnings strict for our code
# while downgrading this one known warning in the published vendor component.
idf_component_get_property(stackchan_asrc_lib espressif__esp_asrc COMPONENT_LIB)
target_compile_options(${stackchan_asrc_lib} PRIVATE -Wno-error=format)
