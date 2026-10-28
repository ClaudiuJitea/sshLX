# The exact list of PuTTY 0.85 source files sshLX compiles, all of them
# unchanged, straight from SSHLX_PUTTY_SOURCE_DIR (see ARCHITECTURE-NOTES.md
# section 3). If a file ever has to be overridden, put the modified copy at
# src/putty-overrides/<same relative path>; sshlx_putty_src() then picks the
# override automatically and the configure log lists it.
#
# When moving to a newer PuTTY: change SSHLX_PUTTY_SOURCE_DIR, compare this
# list with the new PuTTY's CMakeLists.txt files, regenerate the manifest
# (tools/putty-*.sha256) and re-check PUTTY-OVERRIDES.md.

set(SSHLX_PUTTY_OVERRIDES_DIR ${CMAKE_SOURCE_DIR}/src/putty-overrides)
set_property(GLOBAL PROPERTY SSHLX_PUTTY_OVERRIDES_USED "")

function(sshlx_putty_src outvar relpath)
  if(EXISTS ${SSHLX_PUTTY_OVERRIDES_DIR}/${relpath})
    set(${outvar} ${SSHLX_PUTTY_OVERRIDES_DIR}/${relpath} PARENT_SCOPE)
    set_property(GLOBAL APPEND PROPERTY SSHLX_PUTTY_OVERRIDES_USED ${relpath})
  else()
    if(NOT EXISTS ${SSHLX_PUTTY_SOURCE_DIR}/${relpath})
      message(FATAL_ERROR "PuTTY source not found: ${SSHLX_PUTTY_SOURCE_DIR}/${relpath}")
    endif()
    set(${outvar} ${SSHLX_PUTTY_SOURCE_DIR}/${relpath} PARENT_SCOPE)
  endif()
endfunction()

# Common compile settings for anything compiled against PuTTY headers.
# The override dir comes first so that a header override (if ever needed)
# is found by files outside the original header's directory; see the
# header-override caveat in ARCHITECTURE-NOTES.md section 2.
function(sshlx_putty_target_setup target)
  set_target_properties(${target} PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS ON
    POSITION_INDEPENDENT_CODE ON)
  target_compile_definitions(${target} PRIVATE HAVE_CMAKE_H)
  target_include_directories(${target} PRIVATE
    ${SSHLX_PUTTY_OVERRIDES_DIR}
    ${SSHLX_PUTTY_SOURCE_DIR}
    ${SSHLX_PUTTY_SOURCE_DIR}/unix
    ${SSHLX_PUTTY_SOURCE_DIR}/charset
    ${SSHLX_PUTTY_SOURCE_DIR}/terminal
    ${SSHLX_PUTTY_GENERATED_DIR})
  if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    # Same idea as PuTTY's map_pathname(): stable __FILE__ in assertions.
    target_compile_options(${target} PRIVATE
      -fmacro-prefix-map=${SSHLX_PUTTY_SOURCE_DIR}=/putty
      -fmacro-prefix-map=${CMAKE_SOURCE_DIR}=/sshLX)
  endif()
endfunction()

set(SSHLX_PUTTY_RELSOURCES
  # --- core -----------------------------------------------------------
  callback.c timing.c config.c dialog.c settings.c ldisc.c logging.c
  pinger.c sshpubk.c sshrand.c errsock.c x11disp.c
  # --- terminal -------------------------------------------------------
  terminal/terminal.c terminal/bidi.c terminal/lineedit.c
  # --- proxy ----------------------------------------------------------
  proxy/cproxy.c proxy/sshproxy.c proxy/proxy.c proxy/http.c
  proxy/socks4.c proxy/socks5.c proxy/telnet.c proxy/newmain.c
  proxy/local.c proxy/interactor.c
  # --- SSH client (ssh/CMakeLists.txt: sshcommon + sshclient) ------------
  ssh/bpp1.c ssh/bpp2.c ssh/bpp-bare.c ssh/ca-config.c ssh/censor1.c
  ssh/censor2.c ssh/common.c ssh/connection1.c ssh/connection2.c
  ssh/crc-attack-detector.c ssh/gssc.c ssh/login1.c ssh/nocompress.c
  ssh/pgssapi.c ssh/portfwd.c ssh/transient-hostkey-cache.c
  ssh/transport2.c ssh/verstring.c ssh/x11fwd.c ssh/zlib.c
  ssh/agentf.c ssh/connection1-client.c ssh/connection2-client.c
  ssh/kex2-client.c ssh/mainchan.c ssh/sharing.c ssh/ssh.c
  ssh/userauth2-client.c
  pinger.c
  # --- other backends -------------------------------------------------
  otherbackends/raw.c otherbackends/rlogin.c otherbackends/supdup.c
  otherbackends/telnet.c
  # --- charset (sbcsdat.c is generated) ---------------------------------
  charset/fromucs.c charset/localenc.c charset/macenc.c charset/mimeenc.c
  charset/sbcs.c charset/slookup.c charset/toucs.c charset/utf8.c
  charset/xenc.c
  # --- crypto (crypto/CMakeLists.txt; accelerated variants probed) -------
  crypto/aes-common.c crypto/aes-select.c crypto/aes-sw.c
  crypto/aesgcm-common.c crypto/aesgcm-select.c crypto/aesgcm-sw.c
  crypto/aesgcm-ref-poly.c crypto/arcfour.c crypto/argon2.c crypto/bcrypt.c
  crypto/blake2.c crypto/blowfish.c crypto/chacha20-poly1305.c
  crypto/crc32.c crypto/des.c crypto/diffie-hellman.c crypto/dsa.c
  crypto/ecc-arithmetic.c crypto/ecc-ssh.c crypto/hash_simple.c
  crypto/hmac.c crypto/kex-hybrid.c crypto/mac.c crypto/mac_simple.c
  crypto/md5.c crypto/mlkem.c crypto/mpint.c crypto/ntru.c
  crypto/openssh-certs.c crypto/prng.c crypto/pubkey-pem.c
  crypto/pubkey-ppk.c crypto/pubkey-ssh1.c crypto/rfc6979.c crypto/rsa.c
  crypto/sha256-common.c crypto/sha256-select.c crypto/sha256-sw.c
  crypto/sha512-common.c crypto/sha512-select.c crypto/sha512-sw.c
  crypto/sha3.c crypto/sha1-common.c crypto/sha1-select.c crypto/sha1-sw.c
  crypto/xdmauth.c
  # --- utils (utils/CMakeLists.txt + unix extras) -------------------------
  utils/antispoof.c utils/backend_socket_log.c utils/base64_decode_atom.c
  utils/base64_decode.c utils/base64_encode_atom.c utils/base64_encode.c
  utils/base64_valid.c utils/bufchain.c utils/buildinfo.c utils/burnstr.c
  utils/burnwcs.c utils/cert-expr.c utils/chomp.c
  utils/cmdline_get_passwd_input_state_new.c utils/conf.c utils/conf_data.c
  utils/conf_dest.c utils/conf_debug.c utils/conf_launchable.c
  utils/ctrlparse.c utils/ctrlset_normalise.c utils/debug.c
  utils/decode_utf8.c utils/decode_utf8_to_wchar.c
  utils/decode_utf8_to_wide_string.c utils/default_description.c
  utils/dupcat.c utils/dupprintf.c utils/dupstr.c utils/dupwcs.c
  utils/dupwcscat.c utils/dup_mb_to_wc.c utils/dup_wc_to_mb.c
  utils/encode_utf8.c utils/encode_wide_string_as_utf8.c utils/fgetline.c
  utils/format_connection_setup_command.c utils/host_ca_new_free.c
  utils/host_strchr.c utils/host_strchr_internal.c utils/host_strcspn.c
  utils/host_strduptrim.c utils/host_strrchr.c utils/key_components.c
  utils/log_proxy_stderr.c utils/make_spr_sw_abort_static.c
  utils/marshal.c utils/memory.c utils/memxor.c utils/nullstrcmp.c
  utils/out_of_memory.c utils/parse_blocksize.c utils/percent_decode.c
  utils/percent_encode.c utils/prompts.c utils/ptrlen.c
  utils/read_file_into.c utils/seat_connection_fatal.c
  utils/seat_dialog_text.c utils/seat_nonfatal.c utils/sessprep.c
  utils/sk_free_peer_info.c utils/smemclr.c utils/smemeq.c
  utils/spr_get_error_message.c utils/ssh_key_clone.c
  utils/ssh2_pick_fingerprint.c utils/sshutils.c utils/strbuf.c
  utils/string_length_for_printf.c utils/stripctrl.c utils/tempseat.c
  utils/tree234.c utils/unicode-known.c utils/unicode-norm.c
  utils/validate_manual_hostkey.c utils/version.c utils/wcwidth.c
  utils/wildcard.c utils/wordwrap.c utils/write_c_string_literal.c
  utils/x11authfile.c utils/x11authnames.c utils/x11_dehexify.c
  utils/x11_identify_auth_proto.c utils/x11_make_greeting.c
  utils/x11_parse_ip.c utils/logeventf.c utils/ltime.c
  # --- stubs: null-* go into utils in PuTTY; no-cmdline as in puttyapp ----
  stubs/null-lp.c stubs/null-cipher.c stubs/null-key.c stubs/null-mac.c
  stubs/null-opener.c stubs/null-plug.c stubs/null-seat.c
  stubs/null-socket.c stubs/no-cmdline.c
  # --- unix platform, non-GTK ----------------------------------------------
  unix/uxsel.c unix/network.c unix/fd-socket.c unix/agent-socket.c
  unix/agent-client.c unix/peerinfo.c unix/local-proxy.c unix/x11.c
  unix/noise.c unix/gss.c unix/sharing.c unix/serial.c unix/storage.c
  unix/unicode.c unix/printing.c unix/config-unix.c unix/config-gtk.c
  unix/utils/arm_arch_queries.c unix/utils/block_signal.c
  unix/utils/cloexec.c unix/utils/cmdline_arg.c unix/utils/dputs.c
  unix/utils/filename.c unix/utils/fontspec.c unix/utils/getticks.c
  unix/utils/get_username.c unix/utils/keysym_to_unicode.c
  unix/utils/make_dir_and_check_ours.c unix/utils/make_dir_path.c
  unix/utils/make_spr_sw_abort_errno.c unix/utils/nonblock.c
  unix/utils/open_for_write_would_lose_data.c
  unix/utils/pgp_fingerprints.c unix/utils/pollwrap.c unix/utils/signal.c
  unix/utils/subprocess_waiter.c
)
list(REMOVE_DUPLICATES SSHLX_PUTTY_RELSOURCES)

set(SSHLX_PUTTY_SOURCES)
foreach(rel ${SSHLX_PUTTY_RELSOURCES})
  sshlx_putty_src(abs ${rel})
  list(APPEND SSHLX_PUTTY_SOURCES ${abs})
endforeach()

sshlx_putty_crypto_probes()
sshlx_putty_generate_cmake_h()

# enable_dit(): the real one only exists on AArch64 with DIT; elsewhere PuTTY
# links stubs/no-dit.c, and so do we.
if(NOT HAVE_ARM_DIT)
  sshlx_putty_src(nodit stubs/no-dit.c)
  list(APPEND SSHLX_PUTTY_SOURCES ${nodit})
endif()

# be_list.c compiled with sshLX's backend set and application name. It is a
# separate object library because PuTTY configures it per program.
sshlx_putty_src(_belist be_list.c)
add_library(sshlx_putty_be_list OBJECT ${_belist})
sshlx_putty_target_setup(sshlx_putty_be_list)
target_compile_definitions(sshlx_putty_be_list PRIVATE
  SSH=1 SERIAL=1 OTHERBACKENDS=1 APPNAME=sshLX)

set(_accel_objs)
foreach(lib ${SSHLX_PUTTY_ACCEL_OBJLIBS})
  list(APPEND _accel_objs $<TARGET_OBJECTS:${lib}>)
endforeach()

# An OBJECT library: PuTTY's core calls back into the front-end functions
# provided by src/engine, so both are archived together as sshlx-engine.
add_library(putty-core OBJECT
  ${SSHLX_PUTTY_SOURCES}
  ${SSHLX_PUTTY_SBCSDAT_C}
  ${SSHLX_PUTTY_COMMIT_C})
# Object files from the per-flag object libraries; they must be added to
# the final archive alongside putty-core's own objects.
set(SSHLX_PUTTY_EXTRA_OBJECTS $<TARGET_OBJECTS:sshlx_putty_be_list> ${_accel_objs})
sshlx_putty_target_setup(putty-core)
set(SSHLX_PUTTY_LINK_LIBS ${SSHLX_PUTTY_LIBS})
# PuTTY's own sources are third-party: don't drown real warnings in theirs.
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(putty-core PRIVATE -w)
endif()

get_property(_ovr GLOBAL PROPERTY SSHLX_PUTTY_OVERRIDES_USED)
if(_ovr)
  message(STATUS "sshLX: PuTTY overrides in use: ${_ovr}")
else()
  message(STATUS "sshLX: PuTTY overrides in use: none")
endif()
list(LENGTH SSHLX_PUTTY_SOURCES _n)
message(STATUS "sshLX: compiling ${_n} PuTTY sources unchanged from ${SSHLX_PUTTY_SOURCE_DIR}")
