cmake_minimum_required(VERSION 3.21)

project(CanonBulletTime LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Plataformas suportadas (ver ROADMAP.md)
#   - Windows x64
#   - macOS arm64 (Apple Silicon)
# ---------------------------------------------------------------------------
if(WIN32)
    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "Apenas Windows x64 é suportado (sem 32-bit).")
    endif()
elseif(APPLE)
    if(NOT CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
        message(FATAL_ERROR
            "Apenas macOS arm64 (Apple Silicon) é suportado. "
            "Use o preset: cmake --preset macos-arm64")
    endif()
else()
    message(FATAL_ERROR "Sistema não suportado. Apenas Windows x64 e macOS arm64.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Runtime estático (/MT), igual ao projeto Visual Studio anterior
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ---------------------------------------------------------------------------
# Canon EDSDK (não vem no repositório — ver README, "Canon SDK Setup")
# ---------------------------------------------------------------------------
set(EDSDK_ROOT "${CMAKE_SOURCE_DIR}/third_party/edsdk" CACHE PATH "Pasta do Canon EDSDK")
set(EDSDK_INCLUDE_DIR "${EDSDK_ROOT}/Header")

if(WIN32)
    set(EDSDK_LIB "${EDSDK_ROOT}/x64/Library/EDSDK.lib")
    set(EDSDK_DLLS
        "${EDSDK_ROOT}/x64/Dll/EDSDK.dll"
        "${EDSDK_ROOT}/x64/Dll/EdsImage.dll"
    )
    set(EDSDK_REQUIRED_FILES "${EDSDK_INCLUDE_DIR}/EDSDK.h" "${EDSDK_LIB}" ${EDSDK_DLLS})
    set(EDSDK_ZIP_HINT "EDSDK_v13.20.10_Windows.zip")
else()
    set(EDSDK_FRAMEWORK_DIR "${EDSDK_ROOT}/macos")
    set(EDSDK_FRAMEWORK "${EDSDK_FRAMEWORK_DIR}/EDSDK.framework")
    set(EDSDK_REQUIRED_FILES "${EDSDK_INCLUDE_DIR}/EDSDK.h" "${EDSDK_FRAMEWORK}/EDSDK")
    set(EDSDK_ZIP_HINT "EDSDK_v13.20.10_Macintosh.zip")
endif()

foreach(_f ${EDSDK_REQUIRED_FILES})
    if(NOT EXISTS "${_f}")
        message(FATAL_ERROR
            "Arquivo do Canon EDSDK não encontrado: ${_f}\n"
            "Instale o SDK com:\n"
            "  python3 scripts/setup_edsdk.py <caminho do ${EDSDK_ZIP_HINT}>")
    endif()
endforeach()

# ---------------------------------------------------------------------------
# Núcleo + plataforma (compartilhado pelo terminal e pela interface gráfica)
# ---------------------------------------------------------------------------
option(CBT_BUILD_GUI "Compilar também a interface gráfica" ON)

set(CBT_CORE_SOURCES
    src/core/canon_controller.cpp
    src/core/bullet_time.cpp
    src/core/sdk_diagnostics.cpp
    src/core/path_utils.cpp
)

# Código específico de cada sistema (ver include/platform/platform.h)
if(WIN32)
    list(APPEND CBT_CORE_SOURCES
        src/platform/windows/platform_windows.cpp
        src/platform/windows/audio_player_windows.cpp
    )
else()
    list(APPEND CBT_CORE_SOURCES
        src/platform/macos/platform_macos.cpp
        src/platform/macos/audio_player_macos.cpp
    )
endif()

add_library(cbt_core STATIC ${CBT_CORE_SOURCES})

target_include_directories(cbt_core PUBLIC
    include
    src/core
    "${EDSDK_INCLUDE_DIR}"
)

if(WIN32)
    target_compile_definitions(cbt_core PUBLIC UNICODE _UNICODE)

    if(MSVC)
        # Mesmas opções do projeto Visual Studio anterior
        target_compile_options(cbt_core PUBLIC /W3 /sdl /permissive-)
    endif()

    target_link_libraries(cbt_core PUBLIC
        "${EDSDK_LIB}"
        winmm
    )
else()
    # O SDK da Canon para Mac exige esta macro para escolher os tipos do macOS
    target_compile_definitions(cbt_core PUBLIC __MACOS__)

    target_compile_options(cbt_core PUBLIC "-F${EDSDK_FRAMEWORK_DIR}")
    target_link_options(cbt_core PUBLIC "-F${EDSDK_FRAMEWORK_DIR}")
    target_link_libraries(cbt_core PUBLIC
        "-framework EDSDK"
        "-framework CoreFoundation"
    )
endif()

# Junto de cada programa: SDK da Canon e pasta assets (áudios e interface)
function(cbt_copy_runtime_files target)
    if(WIN32)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${EDSDK_DLLS} "$<TARGET_FILE_DIR:${target}>"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:${target}>/assets"
            VERBATIM
        )
    else()
        # O programa procura o EDSDK.framework na pasta "Frameworks" ao lado dele
        set_target_properties(${target} PROPERTIES
            BUILD_RPATH "@executable_path/Frameworks"
            INSTALL_RPATH "@executable_path/Frameworks"
        )
        # ditto preserva a estrutura e a assinatura do framework
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E rm -rf "$<TARGET_FILE_DIR:${target}>/Frameworks/EDSDK.framework"
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/Frameworks"
            COMMAND ditto "${EDSDK_FRAMEWORK}" "$<TARGET_FILE_DIR:${target}>/Frameworks/EDSDK.framework"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:${target}>/assets"
            VERBATIM
        )
    endif()
endfunction()

# ---------------------------------------------------------------------------
# Programa de terminal
# ---------------------------------------------------------------------------
add_executable(CanonBulletTime src/cli/main.cpp)
set_target_properties(CanonBulletTime PROPERTIES OUTPUT_NAME "Canon Bullet Time")
target_link_libraries(CanonBulletTime PRIVATE cbt_core)
if(WIN32)
    target_compile_definitions(CanonBulletTime PRIVATE _CONSOLE)
endif()
cbt_copy_runtime_files(CanonBulletTime)

# ---------------------------------------------------------------------------
# Interface gráfica (janela nativa com HTML/CSS; ver assets/ui)
# ---------------------------------------------------------------------------
if(CBT_BUILD_GUI)
    include(FetchContent)
    FetchContent_Declare(
        webview
        GIT_REPOSITORY https://github.com/webview/webview
        GIT_TAG 0.12.0
    )
    FetchContent_MakeAvailable(webview)

    add_executable(CanonBulletTimeGui WIN32 src/gui/main_gui.cpp)
    set_target_properties(CanonBulletTimeGui PROPERTIES OUTPUT_NAME "Canon Bullet Time GUI")
    target_link_libraries(CanonBulletTimeGui PRIVATE cbt_core webview::core)
    cbt_copy_runtime_files(CanonBulletTimeGui)
endif()
