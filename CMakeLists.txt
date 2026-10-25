cmake_minimum_required(VERSION 3.22)

# macOS: compatível com Macs antigos (10.13, Intel) e novos (Apple Silicon) — binário universal
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "macOS mínimo")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Arquiteturas")
endif()
# Windows: runtime do Visual C++ embutido (não precisa do "VC++ Redistributable")
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(LABLOUD VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE: cópia local (-DJUCE_PATH=...) ou versão fixa descarregada
if(DEFINED JUCE_PATH)
    add_subdirectory(${JUCE_PATH} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.15 GIT_SHALLOW ON)
    FetchContent_MakeAvailable(JUCE)
endif()

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Source/Secrets.h")
    message(FATAL_ERROR "Falta Source/Secrets.h. Corra:  python3 tools/gen_secrets.py   (ou, no CI, defina o segredo LABLOUD_SECRETS_B64)")
endif()

# Formatos. AAX só quando o SDK da Avid estiver disponível (-DAAX_SDK_PATH=...) e o plugin for assinado com PACE/iLok.
if(APPLE)
    set(LX_FORMATS VST3 AU Standalone)
elseif(WIN32)
    set(LX_FORMATS VST3 Standalone)
else()
    set(LX_FORMATS VST3 Standalone)   # Linux: só desenvolvimento/testes
endif()
if(DEFINED AAX_SDK_PATH AND EXISTS "${AAX_SDK_PATH}")
    juce_set_aax_sdk_path("${AAX_SDK_PATH}")
    list(APPEND LX_FORMATS AAX)
    message(STATUS "AAX ativado (SDK em ${AAX_SDK_PATH})")
endif()

juce_add_plugin(Loudnex
    COMPANY_NAME "Piradex"
    BUNDLE_ID "com.piradex.labloud"
    PLUGIN_MANUFACTURER_CODE Prdx
    PLUGIN_CODE PxLB
    FORMATS ${LX_FORMATS}
    PRODUCT_NAME "LABLOUD by Piradex"
    DESCRIPTION "LABLOUD - laboratorio de loudness: limitador e clipper hibrido"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Dynamics
    AU_MAIN_TYPE kAudioUnitType_Effect
    AAX_CATEGORY AAX_ePlugInCategory_Dynamics
    MICROPHONE_PERMISSION_ENABLED FALSE)

target_sources(Loudnex PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Access.cpp)
target_include_directories(Loudnex PRIVATE Source)

target_compile_definitions(Loudnex PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_MODAL_LOOPS_PERMITTED=0
    LX_VERSION_STRING="${PROJECT_VERSION}")

if(MSVC)
    target_compile_options(Loudnex PRIVATE /utf-8)
endif()

target_link_libraries(Loudnex
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags)
option(LX_ENABLE_LTO "Otimização em tempo de ligação (CI)" ON)
if(LX_ENABLE_LTO)
    target_link_libraries(Loudnex PUBLIC juce::juce_recommended_lto_flags)
endif()

# ---- testes do núcleo (C++ puro, sem JUCE) --------------------------------------------------
option(LX_BUILD_TESTS "Compilar os testes do núcleo" OFF)
if(LX_BUILD_TESTS)
    foreach(t test_dsp test_pdf test_engine test_assist test_licensing)
        add_executable(${t} tests/${t}.cpp)
        target_include_directories(${t} PRIVATE Source tests)
    endforeach()
endif()

# ---- teste de fumo do processador (JUCE, sem interface) -------------------------------------
option(LX_BUILD_SMOKE "Compilar o teste de fumo do processador" OFF)
if(LX_BUILD_SMOKE)
    juce_add_console_app(LoudnexSmoke PRODUCT_NAME "loudnex_smoke")
    target_sources(LoudnexSmoke PRIVATE tests/smoke.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/Access.cpp)
    target_include_directories(LoudnexSmoke PRIVATE Source)
    target_compile_definitions(LoudnexSmoke PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_STANDALONE_APPLICATION=1 JUCE_PLUGINHOST_AU=0 LX_VERSION_STRING="${PROJECT_VERSION}")
    target_link_libraries(LoudnexSmoke PRIVATE juce::juce_audio_utils juce::juce_audio_processors juce::juce_dsp juce::juce_recommended_config_flags)
endif()
