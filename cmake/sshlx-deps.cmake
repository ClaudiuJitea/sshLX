# Third-party Qt libraries (LGPL, linked dynamically): Qt Advanced Docking
# System and QtKeychain. System packages are used when available, otherwise
# they are fetched into build/_deps.
include(FetchContent)

# ---- Qt Advanced Docking System ----------------------------------------------
set(SSHLX_ADS_TARGET "")
if(SSHLX_ADS_FROM_SYSTEM)
  find_package(qtadvanceddocking-qt6 QUIET)
  if(TARGET ads::qtadvanceddocking-qt6)
    set(SSHLX_ADS_TARGET ads::qtadvanceddocking-qt6)
    message(STATUS "sshLX: using system Qt Advanced Docking System")
  endif()
endif()
if(NOT SSHLX_ADS_TARGET)
  set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(BUILD_STATIC OFF CACHE BOOL "" FORCE)      # LGPL: shared library
  set(ADS_VERSION 4.4.1 CACHE STRING "" FORCE)
  set(QT_VERSION_MAJOR 6 CACHE STRING "" FORCE)
  FetchContent_Declare(qtads
    GIT_REPOSITORY https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System.git
    GIT_TAG 4.4.1
    GIT_SHALLOW TRUE
    # Build without Qt's private headers; see the patch script.
    PATCH_COMMAND ${CMAKE_COMMAND} -P ${CMAKE_SOURCE_DIR}/cmake/patches/qtads-public-x11.cmake)
  FetchContent_MakeAvailable(qtads)
  if(TARGET qtadvanceddocking-qt6)
    set(SSHLX_ADS_TARGET qtadvanceddocking-qt6)
  elseif(TARGET qt6advanceddocking)
    set(SSHLX_ADS_TARGET qt6advanceddocking)
  else()
    message(FATAL_ERROR "Qt Advanced Docking System target not found after fetch")
  endif()
  message(STATUS "sshLX: using fetched Qt Advanced Docking System 4.4.1 (${SSHLX_ADS_TARGET})")
endif()

# ---- QtKeychain (optional: opt-in password storage) ------------------------
set(SSHLX_KEYCHAIN_TARGET "")
if(SSHLX_KEYCHAIN)
  find_package(Qt6Keychain QUIET)
  if(TARGET Qt6Keychain::Qt6Keychain)
    set(SSHLX_KEYCHAIN_TARGET Qt6Keychain::Qt6Keychain)
    message(STATUS "sshLX: using system QtKeychain")
  else()
    set(BUILD_WITH_QT6 ON CACHE BOOL "" FORCE)
    set(BUILD_TEST_APPLICATION OFF CACHE BOOL "" FORCE)
    set(BUILD_TRANSLATIONS OFF CACHE BOOL "" FORCE)
    set(QTKEYCHAIN_STATIC OFF CACHE BOOL "" FORCE)    # LGPL: shared library
    # libsecret (GNOME Keyring / Secret Service) needs libsecret-1-dev at
    # build time; without it QtKeychain still supports KWallet over D-Bus.
    # sshLX never enables QtKeychain's insecure plaintext fallback.
    find_package(PkgConfig QUIET)
    set(_have_libsecret OFF)
    if(PKG_CONFIG_FOUND)
      pkg_check_modules(SSHLX_LIBSECRET QUIET libsecret-1)
      if(SSHLX_LIBSECRET_FOUND)
        set(_have_libsecret ON)
      endif()
    endif()
    set(LIBSECRET_SUPPORT ${_have_libsecret} CACHE BOOL "" FORCE)
    if(NOT _have_libsecret)
      message(STATUS "sshLX: libsecret-1 not found; keyring support limited to KWallet "
                     "(install libsecret-1-dev for GNOME Keyring / Secret Service)")
    endif()
    set(BUILD_SHARED_LIBS_SAVED ${BUILD_SHARED_LIBS})
    set(BUILD_SHARED_LIBS ON)
    FetchContent_Declare(qtkeychain
      GIT_REPOSITORY https://github.com/frankosterfeld/qtkeychain.git
      GIT_TAG 0.15.0
      GIT_SHALLOW TRUE)
    # Don't build/register QtKeychain's own autotests with sshLX's ctest.
    set(_sshlx_bt ${BUILD_TESTING})
    set(BUILD_TESTING OFF)
    FetchContent_MakeAvailable(qtkeychain)
    set(BUILD_TESTING ${_sshlx_bt})
    set(BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS_SAVED})
    if(TARGET qt6keychain)
      set(SSHLX_KEYCHAIN_TARGET qt6keychain)
      message(STATUS "sshLX: using fetched QtKeychain 0.15.0")
    endif()
  endif()
endif()
