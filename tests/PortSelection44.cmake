# Pending integration: include before test-process diagnostics are attached.
add_executable(selection_source_tests tests/selection_source/pending44/SelectionSourceTests.cpp)
target_link_libraries(selection_source_tests PRIVATE compositor_ui Qt6::Test)
target_compile_options(selection_source_tests PRIVATE /W4 /WX /utf-8)
foreach(case combine modifiers_clip empty_distinct click_undo polygon antialias inverse_tool cursor
    move_undo offcanvas nudge resize_outline resize_empty mask_black mask_transform layer_alpha
    marquee_rectangle marquee_center marquee_ellipse option_event m_key fresh_shift l_key draft_press_guard)
  add_test(NAME source_selection.${case} COMMAND "${CMAKE_COMMAND}"
    "-DEXECUTABLE=$<TARGET_FILE:selection_source_tests>" "-DCASE=${case}"
    "-DEVIDENCE_ROOT=${CMAKE_BINARY_DIR}/selection-source-evidence"
    -P "${CMAKE_SOURCE_DIR}/tests/selection_source/pending44/run-case.cmake")
  set_tests_properties(source_selection.${case} PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 95)
endforeach()

add_executable(palette_source_tests tests/palette_source/pending44/PaletteSourceTests.cpp)
target_link_libraries(palette_source_tests PRIVATE compositor_ui Qt6::Test)
target_compile_options(palette_source_tests PRIVATE /W4 /WX /utf-8)
foreach(case sampling position layout)
  add_test(NAME source_palette.${case} COMMAND "${CMAKE_COMMAND}"
    "-DEXECUTABLE=$<TARGET_FILE:palette_source_tests>" "-DCASE=${case}"
    "-DEVIDENCE_ROOT=${CMAKE_BINARY_DIR}/palette-source-evidence"
    -P "${CMAKE_SOURCE_DIR}/tests/palette_source/pending44/run-case.cmake")
  set_tests_properties(source_palette.${case} PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=windows;QT_SCALE_FACTOR=1" RUN_SERIAL TRUE TIMEOUT 35)
endforeach()

add_executable(selection_cursor_tests tests/selection_cursor/SelectionCursorCoverageTests.cpp)
target_link_libraries(selection_cursor_tests PRIVATE compositor_ui Qt6::Test)
target_compile_options(selection_cursor_tests PRIVATE /W4 /WX /utf-8)
foreach(dpr 1 2)
  foreach(case artwork_0_0 artwork_0_1 artwork_0_2 artwork_1_0 artwork_1_1 artwork_1_2
      artwork_2_0 artwork_2_1 artwork_2_2 artwork_3_0 artwork_3_1 artwork_3_2
      icon_distinct modifier_modes remembered_choice draft_locks_mode cache_reuse hand_override
      immutable_state move_override)
    add_test(NAME selection_cursor.dpr${dpr}_${case} COMMAND "${CMAKE_COMMAND}"
      "-DEXECUTABLE=$<TARGET_FILE:selection_cursor_tests>" "-DCASE=${case}"
      "-DEVIDENCE_ROOT=${CMAKE_BINARY_DIR}/selection-cursor-evidence/dpr${dpr}"
      "-DWITNESS_TIMEOUT_SECONDS=30" -P "${CMAKE_SOURCE_DIR}/tests/run-evidence-case.cmake")
    set_tests_properties(selection_cursor.dpr${dpr}_${case} PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=windows;QT_SCALE_FACTOR=${dpr}" RUN_SERIAL TRUE TIMEOUT 35)
  endforeach()
endforeach()

add_executable(native_cursor_witness tests/selection_cursor/NativeCursorWitness.cpp)
target_link_libraries(native_cursor_witness PRIVATE compositor_ui Qt6::Test user32 gdi32)
target_compile_options(native_cursor_witness PRIVATE /W4 /WX /utf-8)
# The physical HCURSOR observer has its own visible, exclusive-input capture;
# do not run it concurrently with unrelated desktop interaction.
