# kemuri_core — JUCE 非依存の音楽理論/生成エンジン
# ロジックは header-only (inline)。大きなパターンデータのみ .cpp に置く。
add_library(kemuri_core STATIC
    src/PatternLibrary.cpp)

target_include_directories(kemuri_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_compile_features(kemuri_core PUBLIC cxx_std_20)
