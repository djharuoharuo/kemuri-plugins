cmake_minimum_required(VERSION 3.22)

project(KemuriPlugins VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ソースは UTF-8 (BOM なし)。日本語ロケールの MSVC は CP932 と誤認するため明示する
if(MSVC)
    add_compile_options(/utf-8)
endif()

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libs/JUCE/CMakeLists.txt")
    message(FATAL_ERROR
        "JUCE submodule not found. Run:\n"
        "  git submodule update --init --recursive")
endif()

add_subdirectory(libs/JUCE)

add_subdirectory(common/kemuri_core)
add_subdirectory(common/kemuri_ui)
add_subdirectory(plugins/KemuriBass)

enable_testing()
add_subdirectory(tests)
