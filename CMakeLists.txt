cmake_minimum_required(VERSION 3.21)

project(CanonBulletTime LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Plataformas suportadas (ver ROADMAP.md)
#   - Windows x64
#   - macOS arm64 (etapa 3 — ainda não implementado)
# ---------------------------------------------------------------------------
if(NOT WIN32)
    message(FATAL_ERROR
        "Por enquanto só o Windows x64 compila. "
        "O suporte a macOS (arm64) entra na etapa 3 do ROADMAP.md.")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Apenas Windows x64 é suportado (sem 32-bit).")
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
set(EDSDK_LIB         "${EDSDK_ROOT}/x64/Library/EDSDK.lib")
set(EDSDK_DLLS
    "${EDSDK_ROOT}/x64/Dll/EDSDK.dll"
    "${EDSDK_ROOT}/x64/Dll/EdsImage.dll"
)

foreach(_f "${EDSDK_INCLUDE_DIR}/EDSDK.h" "${EDSDK_LIB}" ${EDSDK_DLLS})
    if(NOT EXISTS "${_f}")
        message(FATAL_ERROR
            "Arquivo do Canon EDSDK não encontrado: ${_f}\n"
            "Instale o SDK com:\n"
            "  python scripts/setup_edsdk.py <caminho do EDSDK_v13.20.10_Windows.zip>")
    endif()
endforeach()

# ---------------------------------------------------------------------------
# Programa
# ---------------------------------------------------------------------------
add_executable(CanonBulletTime
    src/main.cpp
    src/canon_controller.cpp
    src/audio_player.cpp
    src/path_utils.cpp
)

set_target_properties(CanonBulletTime PROPERTIES
    OUTPUT_NAME "Canon Bullet Time"
)

target_include_directories(CanonBulletTime PRIVATE
    include
    "${EDSDK_INCLUDE_DIR}"
)

target_compile_definitions(CanonBulletTime PRIVATE
    _CONSOLE
    UNICODE
    _UNICODE
)

if(MSVC)
    # Mesmas opções do projeto Visual Studio anterior
    target_compile_options(CanonBulletTime PRIVATE /W3 /sdl /permissive-)
endif()

target_link_libraries(CanonBulletTime PRIVATE
    "${EDSDK_LIB}"
    winmm
)

# Copia DLLs do SDK e os áudios para junto do .exe, para rodar direto da pasta de build
add_custom_command(TARGET CanonBulletTime POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${EDSDK_DLLS} "$<TARGET_FILE_DIR:CanonBulletTime>"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:CanonBulletTime>/assets"
    VERBATIM
)
