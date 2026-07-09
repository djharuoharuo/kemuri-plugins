juce_add_plugin(KemuriStream
    COMPANY_NAME "kemuriBeat"
    BUNDLE_ID "com.kemuribeat.kemuristream"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE Kmri
    PLUGIN_CODE Kstr
    FORMATS VST3
    VST3_CATEGORIES "Fx" "Analyzer"
    PRODUCT_NAME "KemuriStream")

target_sources(KemuriStream PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_include_directories(KemuriStream PRIVATE Source)

target_compile_definitions(KemuriStream PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(KemuriStream
    PRIVATE
        kemuri_ui
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
