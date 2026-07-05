# kemuri_ui — 共有 LookAndFeel (ダークテーマ)・共通ウィジェット
# ヘッダオンリー。JUCE モジュールへのリンクは利用側プラグインが行う
add_library(kemuri_ui INTERFACE)

target_include_directories(kemuri_ui INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_compile_features(kemuri_ui INTERFACE cxx_std_20)
