# The Qt6 application. C++ only; talks to PuTTY exclusively through
# sshlx_engine.h (no PuTTY header is reachable from here).
find_package(Qt6 REQUIRED COMPONENTS Widgets Network)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Connection tree model: Qt Core only (unit-testable without the engine).
add_library(sshlx-model STATIC model/connectiontree.cpp model/serialrules.cpp
  model/configpush.cpp model/highlightrules.cpp model/sftpclient.cpp model/scpclient.cpp)
target_include_directories(sshlx-model PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/model)
target_link_libraries(sshlx-model PUBLIC Qt6::Core)

# Everything but main() is a library, linked into the sshLX executable.
add_library(sshlx-app STATIC
  qthostloop.cpp
  fontspec.cpp
  keymap.cpp
  shortcuts.cpp
  terminalview.cpp
  promptbar.cpp
  eventlogdialog.cpp
  settingsdialog.cpp
  sessionwidget.cpp
  terminallook.cpp
  documentarea.h
  documentarea_ads.cpp
  connectionstore.cpp
  connectiontreemodel.cpp
  connectionspanel.cpp
  serialportlist.cpp
  propertydefs.cpp
  propertygrid.cpp
  serialmanager.cpp
  configpushbar.cpp
  configpushdialog.cpp
  credentialsdialog.cpp
  filetransferpanel.cpp
  highlightruledialog.cpp
  macros.cpp
  findbar.cpp
  optionsdialog.cpp
  keyring.cpp
  icons.cpp
  startpage.cpp
  theme.cpp
  dialogkit.cpp
  mainwindow.cpp
  updatecheck.cpp
  ${CMAKE_SOURCE_DIR}/resources/sshlx.qrc)
target_compile_definitions(sshlx-app PUBLIC SSHLX_VERSION="${PROJECT_VERSION}")
target_include_directories(sshlx-app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sshlx-app PUBLIC sshlx-engine sshlx-model sshlx-serialdetect Qt6::Widgets Qt6::Network)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(sshlx-app PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

target_link_libraries(sshlx-app PUBLIC ${SSHLX_ADS_TARGET})
if(SSHLX_KEYCHAIN_TARGET)
  target_link_libraries(sshlx-app PUBLIC ${SSHLX_KEYCHAIN_TARGET})
  target_compile_definitions(sshlx-app PRIVATE SSHLX_HAVE_KEYCHAIN)
endif()

find_package(Qt6 COMPONENTS PrintSupport QUIET)
if(TARGET Qt6::PrintSupport)
  target_link_libraries(sshlx-app PUBLIC Qt6::PrintSupport)
  target_compile_definitions(sshlx-app PUBLIC SSHLX_HAVE_PRINT)
endif()

find_package(Qt6 COMPONENTS DBus QUIET)
if(TARGET Qt6::DBus)
  target_link_libraries(sshlx-app PUBLIC Qt6::DBus)
  target_compile_definitions(sshlx-app PRIVATE SSHLX_HAVE_DBUS)
endif()

add_executable(sshlx main.cpp)
target_link_libraries(sshlx PRIVATE sshlx-app)
set_target_properties(sshlx PROPERTIES OUTPUT_NAME sshLX
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
# Everything sshLX ships is in the "sshlx" install component; packaging
# installs only that, so the fetched libraries' headers and CMake files stay out.
include(GNUInstallDirs)
set_target_properties(sshlx PROPERTIES INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}/sshlx")
install(TARGETS sshlx RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT sshlx)

# Fetched LGPL libraries go into a private directory (no clash with distro
# packages of the same libraries); system-provided ones are left alone.
foreach(_lib ${SSHLX_ADS_TARGET} ${SSHLX_KEYCHAIN_TARGET})
  if(TARGET ${_lib})
    get_target_property(_imported ${_lib} IMPORTED)
    if(NOT _imported)
      install(TARGETS ${_lib} LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/sshlx
              COMPONENT sshlx NAMELINK_SKIP)
    endif()
  endif()
endforeach()

# Desktop integration: launcher entry and hicolor icons.
install(FILES ${CMAKE_SOURCE_DIR}/resources/sshlx.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications COMPONENT sshlx)
foreach(_s 16 22 24 32 48 64 128 256 512)
  install(FILES ${CMAKE_SOURCE_DIR}/resources/appicon/sshlx-${_s}.png
          DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${_s}x${_s}/apps
          RENAME sshlx.png COMPONENT sshlx)
endforeach()
install(FILES ${CMAKE_SOURCE_DIR}/LICENSE
        DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/sshlx RENAME copyright COMPONENT sshlx)
install(FILES ${CMAKE_SOURCE_DIR}/third_party/putty/LICENCE
        DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/sshlx RENAME PUTTY-LICENCE
        COMPONENT sshlx)

find_package(Qt6 COMPONENTS Test QUIET)
if(TARGET Qt6::Test)
  add_executable(configpush-test ${CMAKE_SOURCE_DIR}/tests/configpush_test.cpp)
  target_link_libraries(configpush-test PRIVATE sshlx-model Qt6::Test)
  add_test(NAME configpush COMMAND configpush-test)
  add_executable(highlightrules-test ${CMAKE_SOURCE_DIR}/tests/highlightrules_test.cpp)
  target_link_libraries(highlightrules-test PRIVATE sshlx-model Qt6::Test)
  add_test(NAME highlightrules COMMAND highlightrules-test)
  add_executable(credentials-test ${CMAKE_SOURCE_DIR}/tests/credentials_test.cpp)
  target_link_libraries(credentials-test PRIVATE sshlx-model Qt6::Test)
  add_test(NAME credentials COMMAND credentials-test)
  add_executable(transfer-test ${CMAKE_SOURCE_DIR}/tests/transfer_test.cpp)
  target_link_libraries(transfer-test PRIVATE sshlx-model Qt6::Test)
  target_compile_definitions(transfer-test PRIVATE
    SSHLX_FAKE_SFTP="${CMAKE_SOURCE_DIR}/tests/fake_sftp.py")
  add_test(NAME transfer COMMAND transfer-test)
  add_executable(session-e2e-test ${CMAKE_SOURCE_DIR}/tests/session_e2e_test.cpp)
  target_link_libraries(session-e2e-test PRIVATE sshlx-app Qt6::Test)
  target_compile_definitions(session-e2e-test PRIVATE
    SSHLX_FAKE_IOS="${CMAKE_SOURCE_DIR}/tests/fake_ios.py")
  add_test(NAME session-e2e COMMAND session-e2e-test)
  set_tests_properties(session-e2e PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)
endif()
