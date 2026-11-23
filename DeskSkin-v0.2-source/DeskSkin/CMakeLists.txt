cmake_minimum_required(VERSION 3.24)
project(DeskSkin VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Portable core: INI parser + its tests (builds and runs on any OS) -------------------------
add_library(deskskin_ini STATIC src/config/IniParser.cpp src/config/Settings.cpp)
target_include_directories(deskskin_ini PUBLIC src)

enable_testing()
add_executable(ini_tests tests/ini_tests.cpp)
target_link_libraries(ini_tests PRIVATE deskskin_ini)
add_test(NAME ini_tests COMMAND ini_tests)
add_executable(settings_tests tests/settings_tests.cpp)
target_link_libraries(settings_tests PRIVATE deskskin_ini)
add_test(NAME settings_tests COMMAND settings_tests)

# --- Windows engine ---------------------------------------------------------------------------
if(WIN32)
  enable_language(RC)
  add_executable(deskskin WIN32
    resources/DeskSkin.rc
    src/platform/Autostart.cpp
    src/input/MouseRouter.cpp
    src/main.cpp
    src/platform/DesktopHost.cpp
    src/render/GraphicsDevice.cpp
    src/render/SkinWindow.cpp
    src/skin/BasicSkin.cpp)
  target_include_directories(deskskin PRIVATE src)
  target_compile_definitions(deskskin PRIVATE UNICODE _UNICODE WINVER=0x0A00 _WIN32_WINNT=0x0A00)
  target_link_libraries(deskskin PRIVATE deskskin_ini
    d3d11 dxgi d2d1 dwrite dcomp user32 gdi32 shell32 shcore ole32 advapi32)
  set_target_properties(deskskin PROPERTIES OUTPUT_NAME "DeskSkin")

  if(MSVC)
    # Static CRT: no vcruntime DLLs to load or page in. Size-optimised, dead-code-stripped.
    set_property(TARGET deskskin deskskin_ini PROPERTY
      MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(deskskin PRIVATE /W4 /utf-8 /permissive- /Zc:__cplusplus /Gy /Gw
      $<$<CONFIG:Release>:/O1 /GL>)
    # The manifest (DPI awareness, Win10/11 compatibility) is embedded by DeskSkin.rc.
    target_link_options(deskskin PRIVATE /MANIFEST:NO
      $<$<CONFIG:Release>:/LTCG /OPT:REF /OPT:ICF>)
  endif()

  # Ship the demo skin next to the executable.
  add_custom_command(TARGET deskskin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/skins $<TARGET_FILE_DIR:deskskin>/Skins)
endif()
