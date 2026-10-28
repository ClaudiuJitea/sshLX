# sshlx-engine: the PuTTY core plus sshLX's own front-end glue, behind the
# plain-C API in sshlx_engine.h. Nothing outside this directory includes
# PuTTY headers.
add_library(sshlx-engine STATIC
  engine_session.c
  engine_conf.c
  frontend/fe_hostloop.c
  frontend/fe_platform.c
  frontend/fe_seat.c
  frontend/fe_termwin.c
  frontend/fe_logpolicy.c
  frontend/fe_keyboard.c
  frontend/fe_dlg.c
  $<TARGET_OBJECTS:putty-core>
  ${SSHLX_PUTTY_EXTRA_OBJECTS})
sshlx_putty_target_setup(sshlx-engine)
target_include_directories(sshlx-engine
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/frontend)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(sshlx-engine PRIVATE -Wall -Wno-unused-parameter)
endif()
target_link_libraries(sshlx-engine PUBLIC ${SSHLX_PUTTY_LINK_LIBS})
