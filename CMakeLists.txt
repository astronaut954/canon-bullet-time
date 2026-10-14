cmake_minimum_required(VERSION 3.21)

# Versão do app: arquivo VERSION (o mesmo número da tag da versão, sem o "v").
# scripts/build_release.py confere que a tag publicada é igual a ele.
file(READ "${CMAKE_SOURCE_DIR}/VERSION" STILLPOINT_VERSION)
string(STRIP "${STILLPOINT_VERSION}" STILLPOINT_VERSION)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/VERSION")

project(Stillpoint VERSION ${STILLPOINT_VERSION} LANGUAGES CXX)
file(WRITE "${CMAKE_BINARY_DIR}/stillpoint-version.txt" "${STILLPOINT_VERSION}")

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

# Windows: compilador de recursos, para embutir o ícone no .exe
if(WIN32)
    enable_language(RC)
    set(STILLPOINT_ICON_FILE "${CMAKE_SOURCE_DIR}/resources/icon/app-icon.ico")
    configure_file(resources/windows/app.rc.in "${CMAKE_BINARY_DIR}/app.rc" @ONLY)
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
option(STILLPOINT_BUILD_GUI "Compilar também a interface gráfica" ON)

set(STILLPOINT_CORE_SOURCES
    src/core/canon_controller.cpp
    src/core/bullet_time.cpp
    src/core/sdk_diagnostics.cpp
    src/core/path_utils.cpp
)

# Código específico de cada sistema (ver include/platform/platform.h)
if(WIN32)
    list(APPEND STILLPOINT_CORE_SOURCES
        src/platform/windows/platform_windows.cpp
        src/platform/windows/audio_player_windows.cpp
    )
else()
    list(APPEND STILLPOINT_CORE_SOURCES
        src/platform/macos/platform_macos.cpp
        src/platform/macos/audio_player_macos.cpp
    )
endif()

add_library(stillpoint_core STATIC ${STILLPOINT_CORE_SOURCES})

target_include_directories(stillpoint_core PUBLIC
    include
    src/core
    "${EDSDK_INCLUDE_DIR}"
)

if(WIN32)
    target_compile_definitions(stillpoint_core PUBLIC UNICODE _UNICODE)

    if(MSVC)
        # Mesmas opções do projeto Visual Studio anterior
        target_compile_options(stillpoint_core PUBLIC /W3 /sdl /permissive-)
    endif()

    target_link_libraries(stillpoint_core PUBLIC
        "${EDSDK_LIB}"
        winmm
        setupapi
        bcrypt
    )
else()
    # O SDK da Canon para Mac exige esta macro para escolher os tipos do macOS
    target_compile_definitions(stillpoint_core PUBLIC __MACOS__)

    target_compile_options(stillpoint_core PUBLIC "-F${EDSDK_FRAMEWORK_DIR}")
    target_link_options(stillpoint_core PUBLIC "-F${EDSDK_FRAMEWORK_DIR}")
    target_link_libraries(stillpoint_core PUBLIC
        "-framework EDSDK"
        "-framework CoreFoundation"
        "-framework IOKit"
    )
endif()

# Junto de cada programa: SDK da Canon e pasta assets (áudios e interface)
function(stillpoint_copy_runtime_files target)
    if(WIN32)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${EDSDK_DLLS} "$<TARGET_FILE_DIR:${target}>"
            VERBATIM
        )
    else()
        # O programa procura o EDSDK.framework na pasta "Frameworks" ao lado dele ou,
        # dentro do aplicativo (.app), em Contents/Frameworks
        set_target_properties(${target} PROPERTIES
            BUILD_RPATH "@executable_path/Frameworks;@executable_path/../Frameworks"
            INSTALL_RPATH "@executable_path/Frameworks;@executable_path/../Frameworks"
        )
        # ditto preserva a estrutura e a assinatura do framework
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E rm -rf "$<TARGET_FILE_DIR:${target}>/Frameworks/EDSDK.framework"
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/Frameworks"
            COMMAND ditto "${EDSDK_FRAMEWORK}" "$<TARGET_FILE_DIR:${target}>/Frameworks/EDSDK.framework"
            VERBATIM
        )
    endif()

    # assets/ (sons e a página da interface) é copiada em TODA compilação, não só
    # quando o programa é refeito: assim uma mudança só em assets/ui (HTML/CSS/JS)
    # também chega à pasta do app.
    add_custom_target(${target}_assets ALL
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:${target}>/assets"
        COMMENT "Copying assets for ${target}"
        VERBATIM
    )
    add_dependencies(${target}_assets ${target})
endfunction()

# ---------------------------------------------------------------------------
# Programa de terminal
# ---------------------------------------------------------------------------
add_executable(StillpointTerminal src/cli/main.cpp)
set_target_properties(StillpointTerminal PROPERTIES OUTPUT_NAME "Stillpoint Terminal App")
target_link_libraries(StillpointTerminal PRIVATE stillpoint_core)
if(WIN32)
    target_compile_definitions(StillpointTerminal PRIVATE _CONSOLE)
    target_sources(StillpointTerminal PRIVATE "${CMAKE_BINARY_DIR}/app.rc")
endif()
stillpoint_copy_runtime_files(StillpointTerminal)

# ---------------------------------------------------------------------------
# Interface gráfica (janela nativa com HTML/CSS; ver assets/ui)
# ---------------------------------------------------------------------------
if(STILLPOINT_BUILD_GUI)
    include(FetchContent)
    FetchContent_Declare(
        webview
        GIT_REPOSITORY https://github.com/webview/webview
        GIT_TAG 0.12.0
    )
    # stb: leitura/redução/gravação de JPEG para as prévias das fotos (domínio público / MIT)
    FetchContent_Declare(
        stb
        GIT_REPOSITORY https://github.com/nothings/stb
        GIT_TAG 2c980bb59875b0d32144a71867fbdebb2f77cd20
    )
    FetchContent_MakeAvailable(webview stb)

    # LibRaw: prévia de fotos RAW (CR2, CR3, NEF, ARW, DNG e centenas de outros
    # formatos), usando o JPEG que a câmera embute em cada arquivo RAW.
    # Licença: LGPL 2.1 ou CDDL 1.0, à escolha (usamos a CDDL; ver THIRD_PARTY_NOTICES.md).
    # O LibRaw-cmake só fornece a receita de compilação do LibRaw.
    FetchContent_Declare(
        libraw_src
        GIT_REPOSITORY https://github.com/LibRaw/LibRaw
        GIT_TAG 0.21.4
    )
    FetchContent_Declare(
        libraw_cmake
        GIT_REPOSITORY https://github.com/LibRaw/LibRaw-cmake
        GIT_TAG eb98e4325aef2ce85d2eb031c2ff18640ca616d3
        SOURCE_SUBDIR stillpoint-do-not-add  # adicionado abaixo, só com o necessário
    )
    FetchContent_MakeAvailable(libraw_src libraw_cmake)

    set(LIBRAW_PATH "${libraw_src_SOURCE_DIR}" CACHE STRING "" FORCE)
    foreach(_opt ENABLE_OPENMP ENABLE_LCMS ENABLE_JASPER ENABLE_EXAMPLES ENABLE_RAWSPEED
                 ENABLE_X3FTOOLS ENABLE_6BY9RPI LIBRAW_INSTALL LIBRAW_UNINSTALL_TARGET)
        set(${_opt} OFF CACHE BOOL "" FORCE)
    endforeach()
    # Sem bibliotecas do sistema (zlib/libjpeg só servem a DNGs raros): o app
    # continua sem depender de nada instalado na máquina
    set(CMAKE_DISABLE_FIND_PACKAGE_ZLIB TRUE)
    set(CMAKE_DISABLE_FIND_PACKAGE_JPEG TRUE)
    set(_stillpoint_shared ${BUILD_SHARED_LIBS})
    set(BUILD_SHARED_LIBS OFF)
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)  # o LibRaw-cmake respeita o BUILD_SHARED_LIBS acima
    add_subdirectory("${libraw_cmake_SOURCE_DIR}" "${libraw_cmake_BINARY_DIR}" EXCLUDE_FROM_ALL)
    set(BUILD_SHARED_LIBS ${_stillpoint_shared})
    unset(CMAKE_POLICY_DEFAULT_CMP0077)
    unset(CMAKE_DISABLE_FIND_PACKAGE_ZLIB)
    unset(CMAKE_DISABLE_FIND_PACKAGE_JPEG)

    set(STILLPOINT_GUI_SOURCES
        src/gui/main_gui.cpp
        src/gui/photo_store.cpp
    )
    if(WIN32)
        list(APPEND STILLPOINT_GUI_SOURCES src/gui/native_window_win.cpp "${CMAKE_BINARY_DIR}/app.rc")
    else()
        list(APPEND STILLPOINT_GUI_SOURCES src/gui/native_window_mac.cpp)
    endif()

    add_executable(Stillpoint WIN32 ${STILLPOINT_GUI_SOURCES})
    set_target_properties(Stillpoint PROPERTIES OUTPUT_NAME "Stillpoint")
    target_include_directories(Stillpoint PRIVATE src/gui "${stb_SOURCE_DIR}")
    target_link_libraries(Stillpoint PRIVATE stillpoint_core webview::core libraw::libraw)
    target_compile_definitions(Stillpoint PRIVATE STILLPOINT_VERSION="${STILLPOINT_VERSION}")
    if(WIN32)
        target_link_libraries(Stillpoint PRIVATE comctl32 ole32)
    else()
        target_link_libraries(Stillpoint PRIVATE "-framework AppKit")
    endif()
    stillpoint_copy_runtime_files(Stillpoint)
endif()
