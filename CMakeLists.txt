cmake_minimum_required(VERSION 3.16)
project(RestCue VERSION 2.7 LANGUAGES CXX)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build configuration" FORCE)
endif()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>")

include(CTest)
if(BUILD_TESTING)
    add_executable(timer_tests tests/timer_tests.cpp timer.hpp)
    target_compile_features(timer_tests PRIVATE cxx_std_17)
    add_test(NAME timer_tests COMMAND timer_tests)
endif()

if(WIN32)
    enable_language(RC)

    function(restcue_add_win32_tool target source)
        add_executable(${target} ${source})
        target_compile_features(${target} PRIVATE cxx_std_17)
        target_compile_definitions(${target} PRIVATE UNICODE _UNICODE _WIN32_WINNT=0x0A00)
        if(ARGN)
            target_link_libraries(${target} PRIVATE ${ARGN})
        endif()
        if(MSVC)
            target_compile_options(${target} PRIVATE /W4 /utf-8 /permissive-)
        else()
            target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        endif()
    endfunction()

    if(BUILD_TESTING)
        restcue_add_win32_tool(settings_tests tests/settings_tests.cpp shell32 ole32)
        add_test(NAME settings_tests COMMAND settings_tests)

        restcue_add_win32_tool(startup_tests tests/startup_tests.cpp advapi32 shell32 ole32)
        add_test(NAME startup_tests COMMAND startup_tests)

        restcue_add_win32_tool(overlay_tests tests/overlay_tests.cpp gdiplus gdi32 user32)
        add_test(NAME overlay_tests COMMAND overlay_tests)

        restcue_add_win32_tool(render_preview tools/render_preview.cpp gdiplus gdi32 user32)
    endif()

    add_executable(RestCue WIN32
        main.cpp
        timer.hpp
        settings_store.hpp
        startup.hpp
        theme.hpp
        resource.h
        restcue.rc
    )
    target_include_directories(RestCue PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_features(RestCue PRIVATE cxx_std_17)
    target_compile_definitions(RestCue PRIVATE UNICODE _UNICODE _WIN32_WINNT=0x0A00)
    target_link_libraries(RestCue PRIVATE user32 gdi32 shell32 ole32 gdiplus comctl32 dwmapi advapi32)
    if(MSVC)
        target_compile_options(RestCue PRIVATE /W4 /utf-8 /permissive-)
        target_link_options(RestCue PRIVATE "/MANIFEST:NO")
        set_property(TARGET RestCue PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    else()
        target_compile_options(RestCue PRIVATE -Wall -Wextra -Wpedantic)
        target_link_options(RestCue PRIVATE -municode -static)
    endif()
else()
    message(STATUS "The GUI requires Windows; only scheduler tests will be built.")
endif()
