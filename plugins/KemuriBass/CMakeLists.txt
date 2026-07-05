juce_add_plugin(KemuriBass
    COMPANY_NAME "kemuriBeat"
    BUNDLE_ID "com.kemuribeat.kemuribass"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE Kmri
    PLUGIN_CODE Kbas
    FORMATS VST3
    VST3_CATEGORIES "Instrument"
    PRODUCT_NAME "kemuriBass")

target_sources(KemuriBass PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_include_directories(KemuriBass PRIVATE Source)

target_compile_definitions(KemuriBass PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(KemuriBass
    PRIVATE
        kemuri_core
        kemuri_ui
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
