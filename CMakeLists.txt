cmake_minimum_required(VERSION 3.21)

project(sshLX VERSION 0.6.0
  DESCRIPTION "Tabbed, session-managed terminal client built on the PuTTY core"
  LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
set(SSHLX_PUTTY_SOURCE_DIR "${CMAKE_SOURCE_DIR}/../putty-0.85" CACHE PATH
  "PuTTY source tree (read-only; compiled out of tree)")
option(SSHLX_UDEV "Use libudev (loaded at runtime) for serial hot-plug detection" ON)
option(SSHLX_BUILD_APP "Build the Qt6 application" ON)
option(SSHLX_KEYCHAIN "Optional password storage in the system keyring (QtKeychain)" ON)
option(SSHLX_ADS_FROM_SYSTEM "Prefer a system Qt Advanced Docking System package" ON)

get_filename_component(SSHLX_PUTTY_SOURCE_DIR "${SSHLX_PUTTY_SOURCE_DIR}" ABSOLUTE)
if(NOT EXISTS "${SSHLX_PUTTY_SOURCE_DIR}/putty.h")
  message(FATAL_ERROR "SSHLX_PUTTY_SOURCE_DIR does not look like a PuTTY tree: ${SSHLX_PUTTY_SOURCE_DIR}")
endif()

# Rule 1/2: nothing may ever be written inside the PuTTY tree.
file(RELATIVE_PATH _rel "${SSHLX_PUTTY_SOURCE_DIR}" "${CMAKE_BINARY_DIR}")
if(NOT _rel MATCHES "^\\.\\./" AND NOT IS_ABSOLUTE "${_rel}")
  message(FATAL_ERROR "The build directory must not be inside the PuTTY tree "
    "(${SSHLX_PUTTY_SOURCE_DIR}). Use e.g. -B ${CMAKE_SOURCE_DIR}/build")
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# PuTTY forcibly keeps assertions on in release builds (they can enforce
# security-critical constraints, and defs.h has a backstop #error). Do the
# same for all C code here.
foreach(_cfg CMAKE_C_FLAGS_RELEASE CMAKE_C_FLAGS_RELWITHDEBINFO CMAKE_C_FLAGS_MINSIZEREL)
  string(REPLACE "-DNDEBUG" "" ${_cfg} "${${_cfg}}")
endforeach()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

# ---------------------------------------------------------------------------
# PuTTY core, compiled unchanged from SSHLX_PUTTY_SOURCE_DIR
# ---------------------------------------------------------------------------
include(putty-config-checks)
include(putty-sources)

# ---------------------------------------------------------------------------
# sshLX
# ---------------------------------------------------------------------------
if(SSHLX_BUILD_APP)
  find_package(Qt6 REQUIRED COMPONENTS Widgets Network)
  include(sshlx-deps)
endif()

add_subdirectory(src/engine)
add_subdirectory(src/platform/serialdetect)
if(SSHLX_BUILD_APP)
  add_subdirectory(src/app)
endif()

# Checks that the PuTTY tree is untouched: cmake --build build --target verify-putty
add_custom_target(verify-putty
  COMMAND ${CMAKE_SOURCE_DIR}/tools/verify-putty-untouched.sh
          ${SSHLX_PUTTY_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/tools/putty-0.85.sha256
  COMMENT "Verifying the PuTTY tree is byte-for-byte untouched"
  VERBATIM)
