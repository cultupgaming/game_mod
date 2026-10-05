cmake_minimum_required(VERSION 3.21)

project(RedDeadKnifemare LANGUAGES CXX)

set(RDR_SDK_DIR "" CACHE PATH "Path to the RDR1 ScriptHook SDK root")
if(NOT RDR_SDK_DIR)
    message(FATAL_ERROR "RDR_SDK_DIR is required")
endif()

set(RDR_SDK_INCLUDE_DIR "${RDR_SDK_DIR}/inc")
set(RDR_SDK_LIBRARY "${RDR_SDK_DIR}/lib/ScriptHookRDR.lib")

if(NOT EXISTS "${RDR_SDK_INCLUDE_DIR}/main.h")
    message(FATAL_ERROR "RDR1 SDK headers not found under ${RDR_SDK_INCLUDE_DIR}")
endif()

if(NOT EXISTS "${RDR_SDK_LIBRARY}")
    message(FATAL_ERROR "ScriptHookRDR.lib not found at ${RDR_SDK_LIBRARY}")
endif()

add_library(RedDeadKnifemare SHARED
    src/keyboard.cpp
    src/keyboard.h
    src/main.cpp
    src/script.cpp
    src/script.h
    src/trace.cpp
    src/trace.h
)

target_compile_features(RedDeadKnifemare PRIVATE cxx_std_23)
target_include_directories(RedDeadKnifemare PRIVATE "${RDR_SDK_INCLUDE_DIR}")

# Knifemare's Assassin Knife WTDs live in update\game\mapres.rpf. A normal
# import makes Windows load the deployed Ultimate ASI Loader wininet.dll before
# this ASI initializes, matching Red Dead Possession's proven update-file path.
if(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Red Dead Knifemare requires the x64 MSVC toolchain")
endif()
set(UAL_IMPORT_LIBRARY "${CMAKE_CURRENT_BINARY_DIR}/knifemare-ual.lib")
execute_process(
    COMMAND "${CMAKE_AR}" /nologo /machine:x64
        "/def:${CMAKE_CURRENT_SOURCE_DIR}/src/ual-wininet.def"
        "/out:${UAL_IMPORT_LIBRARY}"
    RESULT_VARIABLE UAL_IMPORT_RESULT
)
if(NOT UAL_IMPORT_RESULT EQUAL 0)
    message(FATAL_ERROR "Could not create the UAL wininet import library")
endif()

target_link_libraries(RedDeadKnifemare PRIVATE
    "${RDR_SDK_LIBRARY}" Xinput9_1_0 "${UAL_IMPORT_LIBRARY}")

set_target_properties(RedDeadKnifemare PROPERTIES
    PREFIX ""
    SUFFIX ".asi"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    string(TOUPPER "${config}" config_upper)
    set_target_properties(RedDeadKnifemare PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/bin"
        LIBRARY_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/bin"
        ARCHIVE_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/bin"
    )
endforeach()
