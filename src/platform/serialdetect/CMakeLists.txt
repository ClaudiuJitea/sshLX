# Serial device detection (Linux). Plain C, no engine/Qt dependency.
# libudev is loaded at runtime (dlopen), so there is no build-time
# dependency on libudev headers; SSHLX_UDEV=OFF compiles that out entirely.
add_library(sshlx-serialdetect STATIC serialdetect.c)
target_include_directories(sshlx-serialdetect PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(sshlx-serialdetect PROPERTIES C_STANDARD 99)
target_link_libraries(sshlx-serialdetect PUBLIC ${CMAKE_DL_LIBS})
if(NOT SSHLX_UDEV)
  target_compile_definitions(sshlx-serialdetect PRIVATE SD_NO_UDEV)
endif()
if(CMAKE_C_COMPILER_ID MATCHES "GNU")
  # Bounded snprintf truncation of sysfs paths/strings is intentional.
  target_compile_options(sshlx-serialdetect PRIVATE -Wall -Wno-format-truncation)
endif()
