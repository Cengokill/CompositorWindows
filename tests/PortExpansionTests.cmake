# Additional source scenarios and Windows interaction contracts.

add_executable(palette_accessibility_tests tests/palette_accessibility/PaletteAccessibilityTests.cpp)
add_executable(mask_target_route_tests tests/mask_target_routes/MaskTargetRouteTests.cpp)
add_executable(brush_target_route_tests tests/brush_target_routes/BrushTargetRouteTests.cpp)
foreach(target palette_accessibility_tests mask_target_route_tests brush_target_route_tests)
  target_link_libraries(${target} PRIVATE compositor_ui Qt6::Test uiautomationcore ole32 oleaut32)
  target_compile_options(${target} PRIVATE /W4 /WX /utf-8)
endforeach()
function(add_owned_native_case target prefix case)
  add_test(NAME ${prefix}.${case} COMMAND "${CMAKE_COMMAND}"
    "-DEXECUTABLE=$<TARGET_FILE:${target}>" "-DCASE=${case}"
    "-DEVIDENCE_ROOT=${CMAKE_BINARY_DIR}/${prefix}-evidence" "-DWITNESS_TIMEOUT_SECONDS=30"
    -P "${CMAKE_SOURCE_DIR}/tests/run-evidence-case.cmake")
  set_tests_properties(${prefix}.${case} PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=windows;QT_SCALE_FACTOR=1" RUN_SERIAL TRUE TIMEOUT 35)
endfunction()
foreach(case hue_arrows plane_arrows hsb_clamp rgb_shift_steps hue_retained_gray_black
    hex_keyboard_normalization tab_traversal escape_cancel ok_commit_palette_only
    background_independence mask_target_closes_picker mouse_routes_unchanged
    hue_role_value hue_range_set plane_axes_values plane_range_set disabled_rejects_set
    nonfinite_rejects_set removed_lifetime focus_and_keyboard)
  add_owned_native_case(palette_accessibility_tests palette_accessibility ${case})
endforeach()
foreach(case layer_gradient_mask layer_crop_mask layer_polygon_mask layer_transform_mask
    layer_hue_mask layer_filter_mask layer_levels_mask layer_busy_guard layer_import_guard
    layer_brush_guard_synchronous layer_selected_sync menu_polygon_mask menu_polygon_image
    menu_transform_disabled menu_crop_disabled)
  add_owned_native_case(mask_target_route_tests mask_target_route ${case})
endforeach()
foreach(case brush_roundtrip eraser_roundtrip busy_callback import_callback modal_callback
    running_brush_callback missing_mask hue_editor filter_editor levels_editor
    gradient_hidden_callback crop_hidden_callback polygon_hidden_callback transform_hidden_callback)
  add_owned_native_case(brush_target_route_tests brush_target_route ${case})
endforeach()

add_executable(hue_saturation_source_tests tests/hue_saturation_source/pending43/HueSaturationSourceTests.cpp)
target_link_libraries(hue_saturation_source_tests PRIVATE compositor_ui Qt6::Test)
target_compile_options(hue_saturation_source_tests PRIVATE /W4 /WX /utf-8)
foreach(case defaults_noop hue_ranges colorize_alpha selection_undo preview_original
    band_weights independent_ranges invert_range range_settings eyedroppers targeted_drag sampling_guards)
  add_test(NAME source_hue_saturation.${case} COMMAND "${CMAKE_COMMAND}"
    "-DEXECUTABLE=$<TARGET_FILE:hue_saturation_source_tests>" "-DCASE=${case}"
    "-DEVIDENCE_ROOT=${CMAKE_BINARY_DIR}/hue-source-evidence"
    -P "${CMAKE_SOURCE_DIR}/tests/hue_saturation_source/pending43/run-case.cmake")
  set_tests_properties(source_hue_saturation.${case} PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 105)
endforeach()
add_executable(document_size_source_tests tests/document_size_source/pending43/DocumentSizeSourceTests.cpp)
target_link_libraries(document_size_source_tests PRIVATE compositor_editing compositor_render
  compositor_persistence compositor_imaging compositor_transform Qt6::Core windowscodecs ole32)
target_compile_options(document_size_source_tests PRIVATE /W4 /WX /utf-8)
foreach(case anchors units colored_extension allocation_free image_identity resolution_export rotated_hidden)
  add_test(NAME source_document_size.${case} COMMAND "${CMAKE_COMMAND}"
    "-DEXECUTABLE=$<TARGET_FILE:document_size_source_tests>" "-DCASE=${case}"
    "-DEVIDENCE_ROOT=${CMAKE_BINARY_DIR}/document-size-source-evidence"
    -P "${CMAKE_SOURCE_DIR}/tests/document_size_source/pending43/run-case.cmake")
  set_tests_properties(source_document_size.${case} PROPERTIES TIMEOUT 105)
endforeach()

add_executable(sampling_subregion_tests tests/sampling_reuse43/SubregionContracts.cpp
  tests/sampling_reuse43/Reference.cpp)
target_link_libraries(sampling_subregion_tests PRIVATE compositor_graphics Qt6::Core)
target_include_directories(sampling_subregion_tests PRIVATE src/graphics)
target_compile_options(sampling_subregion_tests PRIVATE /W4 /WX /utf-8)
foreach(case dirty_block_work latest_candidate odd_gray_alignment thin_high_levels recursive_halo
    multiple_dirty_cells candidate_isolation unchanged_identity cache_eviction immutable_lifetime
    callback_exception_recovery)
  add_test(NAME graphics.sampling_subregion_${case} COMMAND "${CMAKE_COMMAND}"
    "-DEXECUTABLE=$<TARGET_FILE:sampling_subregion_tests>" "-DCASE=${case}"
    "-DEVIDENCE_ROOT=${CMAKE_BINARY_DIR}/sampling-subregion-evidence" -DOUTPUT_DIRECTORY=ON
    -P "${CMAKE_SOURCE_DIR}/tests/run-evidence-case.cmake")
  set_tests_properties(graphics.sampling_subregion_${case} PROPERTIES TIMEOUT 75)
endforeach()

include("${CMAKE_SOURCE_DIR}/tests/mask_composition44/Contracts.cmake")

include("${CMAKE_SOURCE_DIR}/tests/PortSelection44.cmake")

add_executable(sampling_prefetch_tests
  tests/sampling_prefetch47/PrefetchContracts.cpp
  tests/sampling_prefetch47/Reference.cpp)
target_link_libraries(sampling_prefetch_tests PRIVATE compositor_graphics compositor_core Qt6::Core)
target_include_directories(sampling_prefetch_tests PRIVATE src src/graphics)
target_compile_features(sampling_prefetch_tests PRIVATE cxx_std_20)
target_compile_options(sampling_prefetch_tests PRIVATE /W4 /WX /permissive- /utf-8)
target_compile_definitions(sampling_prefetch_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
foreach(case IN ITEMS prefetch_color_work prefetch_gray_work prefetch_exterior_alignment
    prefetch_callback_recovery prefetch_complete_eviction_pixels)
  add_test(NAME graphics.sampling_${case} COMMAND ${CMAKE_COMMAND}
    -DEXECUTABLE=$<TARGET_FILE:sampling_prefetch_tests>
    -DCASE=${case}
    -DEVIDENCE_ROOT=${CMAKE_BINARY_DIR}/sampling-prefetch-evidence
    -P ${CMAKE_SOURCE_DIR}/tests/sampling_prefetch47/run-integrated-case.cmake)
  set_tests_properties(graphics.sampling_${case} PROPERTIES TIMEOUT 65)
endforeach()

# Apply at the end, after every executable has been declared. The three shipped
# programs retain their own application error handling.
get_property(port_test_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(target IN LISTS port_test_targets)
  get_target_property(target_type ${target} TYPE)
  if(target_type STREQUAL "EXECUTABLE" AND NOT target MATCHES "^(Compositor|CompositorLauncher|CompositorUpdater)$")
    target_sources(${target} PRIVATE tests/support/TestProcessDiagnostics.cpp)
    add_dependencies(${target} deploy_imaging_runtime)
  endif()
endforeach()

