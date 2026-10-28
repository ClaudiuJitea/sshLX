find_package(Qt6 REQUIRED COMPONENTS Test)
set(CMAKE_AUTOMOC ON)
add_executable(app_gui_test app_gui_test.cpp)
target_link_libraries(app_gui_test PRIVATE sshlx-app Qt6::Test util)
add_dependencies(app_gui_test putty_test_tools)
target_compile_definitions(app_gui_test PRIVATE
  UPPITY_PATH="${SSHLX_PUTTY_TOOLS_DIR}/uppity"
  PUTTYGEN_PATH="${SSHLX_PUTTY_TOOLS_DIR}/puttygen")
add_test(NAME app-gui COMMAND app_gui_test)
set_tests_properties(app-gui PROPERTIES TIMEOUT 180
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
