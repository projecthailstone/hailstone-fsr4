# AMD FSR 4 as a Hailstone upscaler add-on (README.md). Windows x64 only: AMD ships the FSR runtime for Windows alone.
cmake_minimum_required(VERSION 3.24)
project(hailstone-fsr4 VERSION 1.0.0 LANGUAGES CXX)

set(HAILSTONE_CLIENT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../hailstone-client" CACHE PATH
    "a Hailstone client checkout: sdk/hailstone_upscaler.h is taken from it")
set(FFX_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffx" CACHE PATH
    "what this needs of the FidelityFX SDK (tools/fetch_sdk.sh)")
if(NOT EXISTS "${HAILSTONE_CLIENT_DIR}/sdk/hailstone_upscaler.h")
  message(FATAL_ERROR "no sdk/hailstone_upscaler.h in ${HAILSTONE_CLIENT_DIR} (set HAILSTONE_CLIENT_DIR)")
endif()
if(NOT EXISTS "${FFX_DIR}/upscalers/include/ffx_upscale.h")
  message(FATAL_ERROR "no FidelityFX SDK files in ${FFX_DIR}: run tools/fetch_sdk.sh (or set FFX_DIR)")
endif()

include(FetchContent)
FetchContent_Declare(VulkanHeaders
  GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
  GIT_TAG b5c8f996196ba4aa6d8f97e52b5d3b6e70f7e4e2 # vulkan-sdk-1.4.341.0, the client's
  SYSTEM)
FetchContent_MakeAvailable(VulkanHeaders)

add_library(fsr4 MODULE src/fsr4_upscaler.cpp)
set_target_properties(fsr4 PROPERTIES PREFIX "" CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_VISIBILITY_PRESET hidden)
target_include_directories(fsr4 PRIVATE "${HAILSTONE_CLIENT_DIR}/sdk" "${FFX_DIR}/api/include" "${FFX_DIR}/upscalers/include")
target_link_libraries(fsr4 PRIVATE Vulkan::Headers)
if(MSVC)
  target_compile_options(fsr4 PRIVATE /W4 /permissive-)
  target_compile_definitions(fsr4 PRIVATE NOMINMAX)
else()
  target_compile_options(fsr4 PRIVATE -Wall -Wextra -Wpedantic -Wno-missing-field-initializers)
endif()

# dist/: what the add-on's package holds: the provider, AMD's loader and upscaler beside it, AMD's licence file.
set(FSR4_DIST "${CMAKE_BINARY_DIR}/dist")
add_custom_command(TARGET fsr4 POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "${FSR4_DIST}"
  COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:fsr4>" "${FSR4_DIST}/"
  COMMAND ${CMAKE_COMMAND} -E copy "${FFX_DIR}/signedbin/amd_fidelityfx_loader_vk.dll" "${FSR4_DIST}/"
  COMMAND ${CMAKE_COMMAND} -E copy "${FFX_DIR}/signedbin/amd_fidelityfx_upscaler_vk.dll" "${FSR4_DIST}/"
  COMMAND ${CMAKE_COMMAND} -E copy "${FFX_DIR}/docs/license.md" "${FSR4_DIST}/FidelityFX-LICENSE.md"
  COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE" "${FSR4_DIST}/LICENSE.txt"
  VERBATIM)
