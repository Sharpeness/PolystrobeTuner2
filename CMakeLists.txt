cmake_minimum_required(VERSION 3.22)
project(StrobTuner VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1. Автоматическая загрузка JUCE 8 и ASIO SDK (для Windows Standalone)
include(FetchContent)
FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.4
)
FetchContent_MakeAvailable(JUCE)

if(WIN32)
    FetchContent_Declare(
            asiosdk
            GIT_REPOSITORY https://github.com/audiosdk/asio.git
            GIT_TAG        main
    )
    FetchContent_MakeAvailable(asiosdk)
endif()

# 2. Объявление плагина VST3, CLAP и автономного Standalone EXE
juce_add_plugin(StrobTuner
        COMPANY_NAME "RockmanAudio"
        PLUGIN_MANUFACTURER_CODE Strb
        PLUGIN_CODE StTu
        FORMATS VST3 CLAP Standalone
        PRODUCT_NAME "StrobeTuner"
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
)

# 3. Список исходных файлов
target_sources(StrobTuner PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
)

# 4. Пути поиска заголовочных файлов (включая ASIO SDK на Windows)
target_include_directories(StrobTuner PRIVATE
        Source
        Source/dsp
        Source/ui
)

if(WIN32)
    target_include_directories(StrobTuner PUBLIC
            "${asiosdk_SOURCE_DIR}/common"
            "${asiosdk_SOURCE_DIR}/host"
            "${asiosdk_SOURCE_DIR}/host/pc"
    )
    target_compile_definitions(StrobTuner PUBLIC
            JUCE_ASIO=1
            JUCE_WASAPI=1
            JUCE_DIRECTSOUND=1
    )
endif()

# 5. Подключение модулей JUCE
target_link_libraries(StrobTuner PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_basics
        juce::juce_graphics
)

# 6. Максимальная оптимизация и авто-векторизация AVX2/SIMD для Release
if(MSVC)
    target_compile_options(StrobTuner PRIVATE /utf-8 $<$<CONFIG:Release>:/O2 /Oi /Ot /arch:AVX2 /fp:fast>)
else()
    target_compile_options(StrobTuner PRIVATE $<$<CONFIG:Release>:-O3 -mavx2 -mfma -ffast-math>)
endif()

juce_generate_juce_header(StrobTuner)