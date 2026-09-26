# Mask and image composition contracts linked to actual production targets.
# There is deliberately no libraries/, frozen/, pending44/ or copied current
# GrowingBrushSession.cpp on this executable's source or library search paths.
foreach(required_target compositor_graphics compositor_core Qt6::Core)
  if(NOT TARGET ${required_target})
    message(FATAL_ERROR "Define ${required_target} before mask contracts")
  endif()
endforeach()
if(NOT Python3_EXECUTABLE)
  message(FATAL_ERROR "Find Python3 Interpreter before mask contracts")
endif()
get_target_property(mask_graphics_imported compositor_graphics IMPORTED)
if(mask_graphics_imported)
  message(FATAL_ERROR "Mask contracts must link the built production graphics target")
endif()
set(mask_contract_dir "${CMAKE_SOURCE_DIR}/tests/mask_composition44")
set(mask_observed_dir "${CMAKE_CURRENT_BINARY_DIR}/mask-composition-observer")
set(mask_observed_cpp "${mask_observed_dir}/Document-observed.cpp")
set(mask_observed_manifest "${mask_observed_dir}/source-manifest.json")
add_custom_command(OUTPUT "${mask_observed_cpp}" "${mask_observed_manifest}"
  COMMAND "${Python3_EXECUTABLE}" "${mask_contract_dir}/observe-document.py"
    --source-root "${CMAKE_SOURCE_DIR}" --output-cpp "${mask_observed_cpp}"
    --output-manifest "${mask_observed_manifest}"
  DEPENDS "${mask_contract_dir}/observe-document.py"
    src/core/Document.cpp src/core/Document.h
    src/graphics/GrowingBrushSession.cpp src/graphics/GrowingBrushSession.h
    src/graphics/SamplingSource.cpp src/graphics/SamplingSource.h
    src/graphics/BrushCoverage.cpp src/graphics/BrushCoverage.h
    src/graphics/ParallelBatch.cpp src/graphics/ParallelBatch.h
  VERBATIM)
add_library(mask_composition_observed_document OBJECT "${mask_observed_cpp}")
target_include_directories(mask_composition_observed_document PRIVATE src src/core)
target_compile_definitions(mask_composition_observed_document PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(mask_composition_observed_document PRIVATE /W4 /WX /permissive- /utf-8)
add_executable(mask_composition_tests
  tests/mask_composition44/MaskContracts.cpp
  tests/mask_composition44/Reference.cpp
  $<TARGET_OBJECTS:mask_composition_observed_document>)
target_include_directories(mask_composition_tests PRIVATE src src/graphics "${mask_contract_dir}")
target_compile_definitions(mask_composition_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(mask_composition_tests PRIVATE /W4 /WX /utf-8)
# Direct object definitions override Document.cpp from the static core archive.
# Every actual brush method comes from the production graphics target. The
# separate reference namespace compiles only the frozen original42 brush cpp.
target_link_libraries(mask_composition_tests PRIVATE compositor_graphics Qt6::Core ole32)
foreach(case same_grid_mask_work same_grid_row_work row_overlay_exterior
    odd_mask_selection_phase placed_rotated_flipped_mask uniform_mask_fallback
    different_grid_mask_fallback sparse_mask_precondition callback_storage_fallback
    small_placed_mask_budget custom_mask_exception bulk_original_reads opaque
    transparent translucent negative_growth_attached_mask transformed_selection_phase
    erase_partial_selection mask_different_grid_placement blank empty_selection
    original_tile_sharing budget_failure custom_compositor_exception)
  add_test(NAME mask_composition.${case} COMMAND "${CMAKE_COMMAND}"
    "-DEXECUTABLE=$<TARGET_FILE:mask_composition_tests>" "-DCASE=${case}"
    "-DSOURCE_ROOT=${CMAKE_SOURCE_DIR}"
    "-DOBSERVED_MANIFEST=${mask_observed_manifest}"
    "-DOBSERVED_CPP=${mask_observed_cpp}"
    "-DOBSERVED_OBJECT=$<TARGET_OBJECTS:mask_composition_observed_document>"
    "-DGRAPHICS_LIBRARY=$<TARGET_FILE:compositor_graphics>"
    "-DCORE_LIBRARY=$<TARGET_FILE:compositor_core>"
    "-DEVIDENCE_ROOT=${CMAKE_SOURCE_DIR}/evidence/graphics/mask-composition/integrated"
    -P "${mask_contract_dir}/run-integrated-case.cmake")
  set_tests_properties(mask_composition.${case} PROPERTIES TIMEOUT 70)
endforeach()

