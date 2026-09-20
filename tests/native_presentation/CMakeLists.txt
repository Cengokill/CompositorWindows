cmake_minimum_required(VERSION 3.31.6)
project(NativePresentationChecks LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Core Gui Widgets Test)
add_executable(native_display_profile_tests ../display_profile/NativeDisplayProfileTests.cpp ../../src/ui/NativeCanvas.cpp ../../src/ui/CanvasPresentation.cpp)
target_include_directories(native_display_profile_tests PRIVATE ../../src)
target_compile_definitions(native_display_profile_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(native_display_profile_tests PRIVATE /W4 /utf-8)
target_link_libraries(native_display_profile_tests PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/../../build/release/Release/compositor_core.lib"
  "${CMAKE_CURRENT_LIST_DIR}/../../build/release/Release/compositor_transform.lib"
  "${CMAKE_CURRENT_LIST_DIR}/../../build/release/Release/compositor_platform.lib"
  Qt6::Widgets Qt6::Test ole32 d2d1 d3d11 dxgi dxguid mscms bcrypt gdi32 user32 advapi32)
