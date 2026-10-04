# kemuri_core 単体テスト。
# G3: tests/reference/*.json（JS 正本から抽出）との一致を検証する。
# JSON パースに juce_core を使うため juce_add_console_app で構成する。
juce_add_console_app(kemuri_core_tests
    PRODUCT_NAME "kemuri_core_tests")

target_sources(kemuri_core_tests PRIVATE CoreTests.cpp)

target_compile_definitions(kemuri_core_tests PRIVATE
    KEMURI_REFERENCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/reference"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1)

target_link_libraries(kemuri_core_tests PRIVATE
    kemuri_core
    juce::juce_core
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

add_test(NAME kemuri_core_tests COMMAND kemuri_core_tests)

# R3: SMF エクスポート（Processor と共有する writeSequenceToSmf）の往復テスト。
juce_add_console_app(kemuri_smf_tests
    PRODUCT_NAME "kemuri_smf_tests")

target_sources(kemuri_smf_tests PRIVATE SmfTests.cpp)

target_include_directories(kemuri_smf_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/KemuriBass/Source)

target_compile_definitions(kemuri_smf_tests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1)

target_link_libraries(kemuri_smf_tests PRIVATE
    kemuri_core
    juce::juce_audio_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

add_test(NAME kemuri_smf_tests COMMAND kemuri_smf_tests)

# v2: オーディオ解析（合成信号）+ キック同期生成のテスト（JUCE 非依存）。
add_executable(kemuri_v2_tests V2Tests.cpp)
target_link_libraries(kemuri_v2_tests PRIVATE kemuri_core)
if(MSVC)
    target_compile_options(kemuri_v2_tests PRIVATE /W3)
endif()
add_test(NAME kemuri_v2_tests COMMAND kemuri_v2_tests)

# v2 統合テスト: プラグイン本体のソースをそのままコンパイルして、サイドチェイン取り込み →
# 解析 → 生成までを processBlock 経由で検査する。
juce_add_console_app(kemuri_e2e_tests
    PRODUCT_NAME "kemuri_e2e_tests")

target_sources(kemuri_e2e_tests PRIVATE
    E2ETests.cpp
    ../plugins/KemuriBass/Source/PluginProcessor.cpp
    ../plugins/KemuriBass/Source/PluginEditor.cpp)

target_include_directories(kemuri_e2e_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/KemuriBass/Source)

target_compile_definitions(kemuri_e2e_tests PRIVATE
    JucePlugin_Name="kemuriBass"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1)

target_link_libraries(kemuri_e2e_tests PRIVATE
    kemuri_core
    kemuri_ui
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

add_test(NAME kemuri_e2e_tests COMMAND kemuri_e2e_tests)

# R6/R9: patterns.json ロード・マージ（Processor と共有する PatternsJson.h）。
juce_add_console_app(kemuri_patterns_tests
    PRODUCT_NAME "kemuri_patterns_tests")

target_sources(kemuri_patterns_tests PRIVATE PatternsTests.cpp)

target_include_directories(kemuri_patterns_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/KemuriBass/Source)

target_compile_definitions(kemuri_patterns_tests PRIVATE
    KEMURI_SAMPLE_JSON="${CMAKE_CURRENT_SOURCE_DIR}/../docs/patterns.sample.json"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1)

target_link_libraries(kemuri_patterns_tests PRIVATE
    kemuri_core
    juce::juce_core
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

add_test(NAME kemuri_patterns_tests COMMAND kemuri_patterns_tests)

# G3 (KemuriStream): 測定 DSP（Biquad / GatedLoudness / TruePeak）が
# Python 正本（codec_eq.py / pyloudnorm / analyzer.py）と一致することを検証。
juce_add_console_app(kemuri_stream_tests
    PRODUCT_NAME "kemuri_stream_tests")

target_sources(kemuri_stream_tests PRIVATE StreamTests.cpp)

target_include_directories(kemuri_stream_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/KemuriStream/Source)

target_compile_definitions(kemuri_stream_tests PRIVATE
    KEMURI_REFERENCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/reference"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1)

target_link_libraries(kemuri_stream_tests PRIVATE
    juce::juce_core
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

add_test(NAME kemuri_stream_tests COMMAND kemuri_stream_tests)
