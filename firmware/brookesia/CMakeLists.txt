# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp-brookesia)

# chmorgan/esp-audio-player triggers -Wstringop-truncation with newer GCC; don't fail the build on it
idf_component_get_property(audio_player_lib chmorgan__esp-audio-player COMPONENT_LIB)
target_compile_options(${audio_player_lib} PRIVATE -Wno-error=stringop-truncation)
