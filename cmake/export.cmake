# Exportação (Save sessions): incluído pelo CMakeLists.txt principal e pelo teste em tests/export.
# Precisa de stb (stb_SOURCE_DIR) e do alvo libraw::libraw já disponíveis.
# Exportação ("Save sessions" como fotos convertidas ou sequência):
# WEBP (libwebp, BSD), AVIF (libavif + libaom, BSD) e o arquivo MP4/MOV (minimp4,
# CC0). O vídeo H.264 e o HEIC usam o que o sistema já tem (platform/media.h).
FetchContent_Declare(
    libwebp
    GIT_REPOSITORY https://github.com/webmproject/libwebp
    GIT_TAG v1.6.0
)
FetchContent_Declare(
    minimp4
    GIT_REPOSITORY https://github.com/lieff/minimp4
    GIT_TAG 2a3014a7753cfcba4442adeb469431967238fe6a
    SOURCE_SUBDIR stillpoint-do-not-add  # só o cabeçalho minimp4.h
)
FetchContent_Declare(
    libavif
    GIT_REPOSITORY https://github.com/AOMediaCodec/libavif
    GIT_TAG v1.3.0
)
set(_stillpoint_shared ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
foreach(_opt WEBP_BUILD_ANIM_UTILS WEBP_BUILD_CWEBP WEBP_BUILD_DWEBP WEBP_BUILD_GIF2WEBP
             WEBP_BUILD_IMG2WEBP WEBP_BUILD_VWEBP WEBP_BUILD_WEBPINFO WEBP_BUILD_WEBPMUX
             WEBP_BUILD_EXTRAS WEBP_BUILD_FUZZTEST AVIF_BUILD_APPS AVIF_BUILD_TESTS AVIF_BUILD_EXAMPLES)
    set(${_opt} OFF CACHE BOOL "" FORCE)
endforeach()
set(WEBP_BUILD_LIBWEBPMUX ON CACHE BOOL "" FORCE)
set(AVIF_CODEC_AOM LOCAL CACHE STRING "" FORCE)
set(AVIF_CODEC_AOM_DECODE OFF CACHE BOOL "" FORCE)
set(AVIF_CODEC_AOM_ENCODE ON CACHE BOOL "" FORCE)
set(AVIF_LIBYUV OFF CACHE STRING "" FORCE)
set(AVIF_LIBSHARPYUV OFF CACHE STRING "" FORCE)
set(AVIF_LIBXML2 OFF CACHE STRING "" FORCE)
set(AVIF_JPEG OFF CACHE STRING "" FORCE)
set(AVIF_ZLIBPNG OFF CACHE STRING "" FORCE)
# libaom no Windows: precisa de Perl (vem com o Git para Windows) e, para ficar
# rápido, do NASM (winget install NASM.NASM). Sem NASM, compila a versão sem
# otimizações: funciona, só gera AVIF mais devagar.
if(WIN32)
    find_program(STILLPOINT_PERL perl HINTS "$ENV{ProgramFiles}/Git/usr/bin")
    if(STILLPOINT_PERL)
        set(PERL_EXECUTABLE "${STILLPOINT_PERL}" CACHE FILEPATH "" FORCE)
    endif()
    find_program(STILLPOINT_NASM nasm HINTS "$ENV{ProgramFiles}/NASM" "$ENV{LOCALAPPDATA}/bin/NASM")
    if(NOT STILLPOINT_NASM)
        message(STATUS "NASM not found: AVIF export will work, but slower (winget install NASM.NASM)")
        set(AOM_TARGET_CPU generic CACHE STRING "" FORCE)
    else()
        set(CMAKE_ASM_NASM_COMPILER "${STILLPOINT_NASM}" CACHE FILEPATH "" FORCE)
    endif()
endif()
FetchContent_MakeAvailable(libwebp minimp4 libavif)
set(BUILD_SHARED_LIBS ${_stillpoint_shared})
unset(CMAKE_POLICY_DEFAULT_CMP0077)

add_library(stillpoint_export STATIC "${STILLPOINT_SOURCE_ROOT}/src/gui/exporter.cpp" "${STILLPOINT_SOURCE_ROOT}/src/gui/photo_store.cpp")
target_include_directories(stillpoint_export PUBLIC "${STILLPOINT_SOURCE_ROOT}/src/gui" "${STILLPOINT_SOURCE_ROOT}/include" "${stb_SOURCE_DIR}"
                           PRIVATE "${libwebp_SOURCE_DIR}/src" "${minimp4_SOURCE_DIR}")
target_link_libraries(stillpoint_export PUBLIC libraw::libraw PRIVATE webp libwebpmux avif)
if(WIN32)
    target_sources(stillpoint_export PRIVATE "${STILLPOINT_SOURCE_ROOT}/src/platform/windows/media_windows.cpp")
    target_link_libraries(stillpoint_export PRIVATE mfplat mfuuid windowscodecs ole32)
elseif(APPLE)
    target_sources(stillpoint_export PRIVATE "${STILLPOINT_SOURCE_ROOT}/src/platform/macos/media_macos.cpp")
    target_link_libraries(stillpoint_export PRIVATE "-framework VideoToolbox" "-framework CoreMedia"
                          "-framework CoreVideo" "-framework CoreFoundation" "-framework ImageIO"
                          "-framework CoreGraphics")
else()
    # Só para testar a exportação no Linux (o app não roda no Linux)
    target_sources(stillpoint_export PRIVATE "${STILLPOINT_SOURCE_ROOT}/src/platform/linux_test/media_linux_test.cpp")
endif()

