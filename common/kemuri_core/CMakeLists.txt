# kemuri_core — JUCE 非依存・ヘッダ中心の音楽理論/生成エンジン
# (M1 で PatternEngine / Markov / Groove / KeyDetect / ChordDetect を追加)
add_library(kemuri_core INTERFACE)

target_include_directories(kemuri_core INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_compile_features(kemuri_core INTERFACE cxx_std_20)
