# Teste da exportação no Linux (o app só roda no Windows e no Mac): gera GIF, WEBP,
# AVIF, MP4, MOV e fotos convertidas a partir de uma sessão, sem câmeras.
#   cmake -S tests/export -B build/export-test -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/export-test -j
#   ./build/export-test/stillpoint_export_test docs/test-session/2026-10-14_16-30-00 /tmp/saida
cmake_minimum_required(VERSION 3.24)
project(StillpointExportTest LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(FetchContent)
get_filename_component(STILLPOINT_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb
    GIT_TAG 2c980bb59875b0d32144a71867fbdebb2f77cd20
)
FetchContent_MakeAvailable(stb)

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


include("${STILLPOINT_SOURCE_ROOT}/cmake/export.cmake")
add_executable(stillpoint_export_test export_test.cpp)
target_link_libraries(stillpoint_export_test PRIVATE stillpoint_export)
