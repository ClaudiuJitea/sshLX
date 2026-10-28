include(ExternalProject)

# ---------------------------------------------------------------------------
# Test fixtures from PuTTY itself: 'uppity' (PuTTY's SSH test server) and
# 'puttygen' (to make its host key). Built with PuTTY's own CMake, out of
# tree in build/_putty_tools, building only these two targets (no GTK front
# end); nothing is written into SSHLX_PUTTY_SOURCE_DIR. The
# putty-untouched test below proves it.
# ---------------------------------------------------------------------------
set(SSHLX_PUTTY_TOOLS_DIR ${CMAKE_BINARY_DIR}/_putty_tools)
ExternalProject_Add(putty_test_tools
  SOURCE_DIR ${SSHLX_PUTTY_SOURCE_DIR}
  BINARY_DIR ${SSHLX_PUTTY_TOOLS_DIR}
  CMAKE_ARGS -DCMAKE_BUILD_TYPE=Release -DPUTTY_GSSAPI=OFF
  BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --target uppity puttygen
  INSTALL_COMMAND ""
  UPDATE_COMMAND ""
  DOWNLOAD_COMMAND ""
  BUILD_ALWAYS OFF
  BUILD_BYPRODUCTS ${SSHLX_PUTTY_TOOLS_DIR}/uppity ${SSHLX_PUTTY_TOOLS_DIR}/puttygen)

# ---------------------------------------------------------------------------
# Engine tests (C, through sshlx_engine.h only)
# ---------------------------------------------------------------------------
add_library(sshlx-testloop STATIC testloop.c)
target_link_libraries(sshlx-testloop PUBLIC sshlx-engine)
set_target_properties(sshlx-testloop PROPERTIES C_STANDARD 99)

add_executable(engine_api_test engine_api_test.c)
target_link_libraries(engine_api_test sshlx-testloop util)
add_test(NAME engine-api COMMAND engine_api_test)

add_executable(engine_sessions_test engine_sessions_test.c)
target_link_libraries(engine_sessions_test sshlx-testloop util)
add_dependencies(engine_sessions_test putty_test_tools)
target_compile_definitions(engine_sessions_test PRIVATE
  UPPITY_PATH="${SSHLX_PUTTY_TOOLS_DIR}/uppity"
  PUTTYGEN_PATH="${SSHLX_PUTTY_TOOLS_DIR}/puttygen")
add_test(NAME engine-sessions COMMAND engine_sessions_test)
set_tests_properties(engine-sessions PROPERTIES TIMEOUT 120)

if(TARGET sshlx-app)
  add_subdirectory(app)
endif()
if(TARGET sshlx-serialdetect)
  add_subdirectory(serialdetect)
endif()
if(TARGET sshlx-model)
  add_subdirectory(model)
endif()

add_test(NAME putty-untouched
  COMMAND ${CMAKE_SOURCE_DIR}/tools/verify-putty-untouched.sh
          ${SSHLX_PUTTY_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/tools/putty-0.85.sha256)
