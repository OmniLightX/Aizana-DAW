# Unmodified upstream source archive; build only the LGPL encoder library.
include(ExternalProject)
set(AIZANA_LAME_ROOT "${CMAKE_BINARY_DIR}/_deps/aizana-lame")
set(AIZANA_LAME_CFLAGS "-O2 -mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}")
foreach(arch IN LISTS CMAKE_OSX_ARCHITECTURES)
    string(APPEND AIZANA_LAME_CFLAGS " -arch ${arch}")
endforeach()
ExternalProject_Add(aizana_lame_build
    URL "${CMAKE_SOURCE_DIR}/extern/lame/lame-3.100.tar.gz"
    URL_HASH SHA256=ddfe36cab873794038ae2c1210557ad34857a4b6bdc515785d1da9e175b1da1e
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_DIR "${AIZANA_LAME_ROOT}/src"
    BINARY_DIR "${AIZANA_LAME_ROOT}/build"
    CONFIGURE_COMMAND ${CMAKE_COMMAND} -E env
        "CC=${CMAKE_C_COMPILER}" "CFLAGS=${AIZANA_LAME_CFLAGS}"
        ../src/configure --srcdir=../src --disable-shared --enable-static
        --disable-frontend --disable-decoder --disable-nasm --disable-dependency-tracking
    BUILD_COMMAND /usr/bin/make -C libmp3lame
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS "${AIZANA_LAME_ROOT}/build/libmp3lame/.libs/libmp3lame.a")
file(MAKE_DIRECTORY "${AIZANA_LAME_ROOT}/src/include")
add_library(aizana_lame STATIC IMPORTED GLOBAL)
set_target_properties(aizana_lame PROPERTIES
    IMPORTED_LOCATION "${AIZANA_LAME_ROOT}/build/libmp3lame/.libs/libmp3lame.a"
    INTERFACE_INCLUDE_DIRECTORIES "${AIZANA_LAME_ROOT}/src/include")
add_dependencies(aizana_lame aizana_lame_build)
