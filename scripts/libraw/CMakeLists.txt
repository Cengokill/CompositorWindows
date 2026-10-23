# Static LibRaw 0.22.2 build from the pinned checkout. LibRaw ships no maintained CMake
# project; this file lists the sources of its Visual Studio library project (buildfiles/libraw.vcxproj)
# so the static build stays reproducible from the pinned tag. Optional decoder dependencies
# (LCMS, OpenMP, DNG SDK, RawSpeed, GoPro) are not compiled, so only LibRaw itself is linked.
cmake_minimum_required(VERSION 3.21)
project(libraw CXX)

set(LIBRAW_SOURCE "" CACHE PATH "Pinned LibRaw 0.22.2 checkout")
if(NOT EXISTS "${LIBRAW_SOURCE}/libraw/libraw.h")
    message(FATAL_ERROR "LIBRAW_SOURCE must point to the LibRaw 0.22.2 checkout")
endif()

add_library(libraw STATIC
    ${LIBRAW_SOURCE}/src/demosaic/aahd_demosaic.cpp
    ${LIBRAW_SOURCE}/src/metadata/adobepano.cpp
    ${LIBRAW_SOURCE}/src/demosaic/ahd_demosaic.cpp
    ${LIBRAW_SOURCE}/src/write/apply_profile.cpp
    ${LIBRAW_SOURCE}/src/postprocessing/aspect_ratio.cpp
    ${LIBRAW_SOURCE}/src/tables/cameralist.cpp
    ${LIBRAW_SOURCE}/src/metadata/canon.cpp
    ${LIBRAW_SOURCE}/src/decoders/canon_600.cpp
    ${LIBRAW_SOURCE}/src/metadata/ciff.cpp
    ${LIBRAW_SOURCE}/src/tables/colorconst.cpp
    ${LIBRAW_SOURCE}/src/tables/colordata.cpp
    ${LIBRAW_SOURCE}/src/metadata/cr3_parser.cpp
    ${LIBRAW_SOURCE}/src/decoders/crx.cpp
    ${LIBRAW_SOURCE}/src/decoders/pana8.cpp
    ${LIBRAW_SOURCE}/src/decoders/olympus14.cpp
    ${LIBRAW_SOURCE}/src/decoders/sonycc.cpp
    ${LIBRAW_SOURCE}/src/decompressors/losslessjpeg.cpp
    ${LIBRAW_SOURCE}/src/utils/curves.cpp
    ${LIBRAW_SOURCE}/src/demosaic/dcb_demosaic.cpp
    ${LIBRAW_SOURCE}/src/postprocessing/dcraw_process.cpp
    ${LIBRAW_SOURCE}/src/utils/decoder_info.cpp
    ${LIBRAW_SOURCE}/src/decoders/decoders_dcraw.cpp
    ${LIBRAW_SOURCE}/src/decoders/decoders_libraw.cpp
    ${LIBRAW_SOURCE}/src/decoders/decoders_libraw_dcrdefs.cpp
    ${LIBRAW_SOURCE}/src/demosaic/dht_demosaic.cpp
    ${LIBRAW_SOURCE}/src/decoders/dng.cpp
    ${LIBRAW_SOURCE}/src/integration/dngsdk_glue.cpp
    ${LIBRAW_SOURCE}/src/metadata/epson.cpp
    ${LIBRAW_SOURCE}/src/metadata/exif_gps.cpp
    ${LIBRAW_SOURCE}/src/preprocessing/ext_preprocess.cpp
    ${LIBRAW_SOURCE}/src/write/file_write.cpp
    ${LIBRAW_SOURCE}/src/decoders/fp_dng.cpp
    ${LIBRAW_SOURCE}/src/metadata/fuji.cpp
    ${LIBRAW_SOURCE}/src/decoders/fuji_compressed.cpp
    ${LIBRAW_SOURCE}/src/decoders/generic.cpp
    ${LIBRAW_SOURCE}/src/metadata/hasselblad_model.cpp
    ${LIBRAW_SOURCE}/src/metadata/identify.cpp
    ${LIBRAW_SOURCE}/src/metadata/identify_tools.cpp
    ${LIBRAW_SOURCE}/src/utils/init_close_utils.cpp
    ${LIBRAW_SOURCE}/src/metadata/kodak.cpp
    ${LIBRAW_SOURCE}/src/decoders/kodak_decoders.cpp
    ${LIBRAW_SOURCE}/src/metadata/leica.cpp
    ${LIBRAW_SOURCE}/src/libraw_c_api.cpp
    ${LIBRAW_SOURCE}/src/libraw_datastream.cpp
    ${LIBRAW_SOURCE}/src/decoders/load_mfbacks.cpp
    ${LIBRAW_SOURCE}/src/metadata/makernotes.cpp
    ${LIBRAW_SOURCE}/src/metadata/mediumformat.cpp
    ${LIBRAW_SOURCE}/src/postprocessing/mem_image.cpp
    ${LIBRAW_SOURCE}/src/metadata/minolta.cpp
    ${LIBRAW_SOURCE}/src/demosaic/misc_demosaic.cpp
    ${LIBRAW_SOURCE}/src/metadata/misc_parsers.cpp
    ${LIBRAW_SOURCE}/src/metadata/nikon.cpp
    ${LIBRAW_SOURCE}/src/metadata/normalize_model.cpp
    ${LIBRAW_SOURCE}/src/metadata/olympus.cpp
    ${LIBRAW_SOURCE}/src/utils/open.cpp
    ${LIBRAW_SOURCE}/src/metadata/p1.cpp
    ${LIBRAW_SOURCE}/src/metadata/pentax.cpp
    ${LIBRAW_SOURCE}/src/utils/phaseone_processing.cpp
    ${LIBRAW_SOURCE}/src/postprocessing/postprocessing_aux.cpp
    ${LIBRAW_SOURCE}/src/postprocessing/postprocessing_utils.cpp
    ${LIBRAW_SOURCE}/src/postprocessing/postprocessing_utils_dcrdefs.cpp
    ${LIBRAW_SOURCE}/src/preprocessing/raw2image.cpp
    ${LIBRAW_SOURCE}/src/integration/rawspeed_glue.cpp
    ${LIBRAW_SOURCE}/src/utils/read_utils.cpp
    ${LIBRAW_SOURCE}/src/metadata/samsung.cpp
    ${LIBRAW_SOURCE}/src/decoders/smal.cpp
    ${LIBRAW_SOURCE}/src/metadata/sony.cpp
    ${LIBRAW_SOURCE}/src/preprocessing/subtract_black.cpp
    ${LIBRAW_SOURCE}/src/utils/thumb_utils.cpp
    ${LIBRAW_SOURCE}/src/metadata/tiff.cpp
    ${LIBRAW_SOURCE}/src/write/tiff_writer.cpp
    ${LIBRAW_SOURCE}/src/decoders/unpack.cpp
    ${LIBRAW_SOURCE}/src/decoders/unpack_thumb.cpp
    ${LIBRAW_SOURCE}/src/utils/utils_dcraw.cpp
    ${LIBRAW_SOURCE}/src/utils/utils_libraw.cpp
    ${LIBRAW_SOURCE}/src/tables/wblists.cpp
    ${LIBRAW_SOURCE}/src/x3f/x3f_parse_process.cpp
    ${LIBRAW_SOURCE}/src/x3f/x3f_utils_patched.cpp
    ${LIBRAW_SOURCE}/src/demosaic/xtrans_demosaic.cpp

)
target_include_directories(libraw PUBLIC "${LIBRAW_SOURCE}")
target_include_directories(libraw PRIVATE "${LIBRAW_SOURCE}/buildfiles")
target_compile_definitions(libraw PUBLIC LIBRAW_NODLL)
target_compile_definitions(libraw PRIVATE _WINDOWS UNICODE _UNICODE WIN32 WIN64 _ENABLE_EXTENDED_ALIGNED_STORAGE)
target_compile_options(libraw PRIVATE /EHsc /utf-8 /Zc:__cplusplus /Zc:inline /Zc:throwingNew /wd4996)

install(TARGETS libraw ARCHIVE DESTINATION lib)
install(FILES ${LIBRAW_SOURCE}/libraw/libraw.h ${LIBRAW_SOURCE}/libraw/libraw_types.h ${LIBRAW_SOURCE}/libraw/libraw_const.h ${LIBRAW_SOURCE}/libraw/libraw_version.h ${LIBRAW_SOURCE}/libraw/libraw_datastream.h ${LIBRAW_SOURCE}/libraw/libraw_alloc.h ${LIBRAW_SOURCE}/libraw/libraw_internal.h DESTINATION include/libraw)
install(FILES ${LIBRAW_SOURCE}/LICENSE.CDDL ${LIBRAW_SOURCE}/LICENSE.LGPL DESTINATION licenses/libraw)
