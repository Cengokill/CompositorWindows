include(FetchContent)
include(ExternalProject)
file(READ "${CMAKE_CURRENT_LIST_DIR}/../dependencies.lock.json" jpeg_lock)
string(JSON jpeg_url GET "${jpeg_lock}" libjpeg_turbo url)
string(JSON jpeg_hash GET "${jpeg_lock}" libjpeg_turbo sha256)
set(jpeg_archive "${CMAKE_CURRENT_LIST_DIR}/../dependencies/downloads/libjpeg-turbo-3.2.1.tar.gz")
if(NOT EXISTS "${jpeg_archive}")
  file(DOWNLOAD "${jpeg_url}" "${jpeg_archive}" EXPECTED_HASH "SHA256=${jpeg_hash}" TLS_VERIFY ON STATUS jpeg_download)
  list(GET jpeg_download 0 jpeg_status)
  if(NOT jpeg_status EQUAL 0)
    file(REMOVE "${jpeg_archive}")
    message(FATAL_ERROR "libjpeg-turbo source download failed: ${jpeg_download}")
  endif()
endif()
file(SHA256 "${jpeg_archive}" jpeg_actual)
if(NOT jpeg_actual STREQUAL jpeg_hash)
  message(FATAL_ERROR "libjpeg-turbo source checksum mismatch")
endif()
FetchContent_Declare(jpeg_turbo URL "${jpeg_archive}" URL_HASH "SHA256=${jpeg_hash}" DOWNLOAD_EXTRACT_TIMESTAMP TRUE SOURCE_SUBDIR no-subdirectory)
FetchContent_MakeAvailable(jpeg_turbo)
set(jpeg_turbo_BINARY_DIR "${CMAKE_BINARY_DIR}/jpeg-turbo-build")
file(MAKE_DIRECTORY "${jpeg_turbo_BINARY_DIR}")
ExternalProject_Add(jpeg_turbo_build SOURCE_DIR "${jpeg_turbo_SOURCE_DIR}" BINARY_DIR "${jpeg_turbo_BINARY_DIR}"
  CMAKE_ARGS -DENABLE_SHARED=OFF -DENABLE_STATIC=ON -DWITH_TURBOJPEG=OFF -DWITH_TOOLS=OFF -DWITH_TESTS=OFF -DWITH_SIMD=OFF -DWITH_CRT_DLL=ON -DCMAKE_SYSTEM_VERSION=${CMAKE_SYSTEM_VERSION}
  BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config $<CONFIG> --target jpeg-static --parallel 4
  INSTALL_COMMAND "" BUILD_BYPRODUCTS "${jpeg_turbo_BINARY_DIR}/$<CONFIG>/jpeg-static.lib")
add_library(jpeg-static STATIC IMPORTED GLOBAL)
set_target_properties(jpeg-static PROPERTIES
  IMPORTED_LOCATION_RELEASE "${jpeg_turbo_BINARY_DIR}/Release/jpeg-static.lib"
  IMPORTED_LOCATION_DEBUG "${jpeg_turbo_BINARY_DIR}/Debug/jpeg-static.lib"
  IMPORTED_LOCATION_RELWITHDEBINFO "${jpeg_turbo_BINARY_DIR}/RelWithDebInfo/jpeg-static.lib"
  IMPORTED_LOCATION_MINSIZEREL "${jpeg_turbo_BINARY_DIR}/MinSizeRel/jpeg-static.lib")
add_dependencies(jpeg-static jpeg_turbo_build)
