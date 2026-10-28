# Re-implementation of the configure-time probes from PuTTY's
# cmake/setup.cmake and cmake/platforms/unix.cmake, so that sshLX can
# generate PuTTY's cmake.h (and the other generated sources PuTTY needs)
# inside sshLX's own build tree. Nothing here writes to
# SSHLX_PUTTY_SOURCE_DIR; the PuTTY templates/scripts are only read.

include(CheckCSourceCompiles)
include(CheckIncludeFile)
include(CheckSymbolExists)
include(CheckLibraryExists)
include(CheckCCompilerFlag)

set(SSHLX_PUTTY_GENERATED_DIR ${CMAKE_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${SSHLX_PUTTY_GENERATED_DIR})

set(_P ${SSHLX_PUTTY_SOURCE_DIR})

# --- Same probes as PuTTY's setup.cmake -----------------------------------
check_c_source_compiles("
#define _ISOC11_SOURCE
#include <stdlib.h>
int main(int argc, char **argv) {
    void *p = aligned_alloc(128, 12345);
    free(p);
}" HAVE_ALIGNED_ALLOC)

check_c_source_compiles("
int main(int argc, char **argv) {
    int a[3];
    return _Countof(a);
}" HAVE_COUNTOF)

# --- Same probes as PuTTY's cmake/platforms/unix.cmake --------------------
set(CMAKE_REQUIRED_DEFINITIONS ${CMAKE_REQUIRED_DEFINITIONS}
  -D_DEFAULT_SOURCE -D_GNU_SOURCE)

check_include_file(sys/auxv.h HAVE_SYS_AUXV_H)
check_include_file(asm/hwcap.h HAVE_ASM_HWCAP_H)
check_include_file(sys/sysctl.h HAVE_SYS_SYSCTL_H)
check_include_file(sys/types.h HAVE_SYS_TYPES_H)
check_include_file(glob.h HAVE_GLOB_H)
check_include_file(utmp.h HAVE_UTMP_H)
check_include_file(utmpx.h HAVE_UTMPX_H)

check_symbol_exists(futimes "sys/time.h" HAVE_FUTIMES)
check_symbol_exists(getaddrinfo "sys/types.h;sys/socket.h;netdb.h"
  HAVE_GETADDRINFO)
check_symbol_exists(posix_openpt "stdlib.h;fcntl.h" HAVE_POSIX_OPENPT)
check_symbol_exists(ptsname "stdlib.h" HAVE_PTSNAME)
check_symbol_exists(setresuid "unistd.h" HAVE_SETRESUID)
check_symbol_exists(setresgid "unistd.h" HAVE_SETRESGID)
check_symbol_exists(strsignal "string.h" HAVE_STRSIGNAL)
check_symbol_exists(updwtmpx "utmpx.h" HAVE_UPDWTMPX)
check_symbol_exists(fstatat "sys/types.h;sys/stat.h;unistd.h" HAVE_FSTATAT)
check_symbol_exists(dirfd "sys/types.h;dirent.h" HAVE_DIRFD)
check_symbol_exists(setpwent "sys/types.h;pwd.h" HAVE_SETPWENT)
check_symbol_exists(endpwent "sys/types.h;pwd.h" HAVE_ENDPWENT)
check_symbol_exists(getauxval "sys/auxv.h" HAVE_GETAUXVAL)
check_symbol_exists(elf_aux_info "sys/auxv.h" HAVE_ELF_AUX_INFO)
check_symbol_exists(sysctlbyname "sys/types.h;sys/sysctl.h" HAVE_SYSCTLBYNAME)
check_symbol_exists(CLOCK_MONOTONIC "time.h" HAVE_CLOCK_MONOTONIC)
check_symbol_exists(clock_gettime "time.h" HAVE_CLOCK_GETTIME)

check_c_source_compiles("
#define _GNU_SOURCE
#include <features.h>
#include <sys/socket.h>
int main(int argc, char **argv) {
    struct ucred cr;
    socklen_t crlen = sizeof(cr);
    return getsockopt(0, SOL_SOCKET, SO_PEERCRED, &cr, &crlen) +
           cr.pid + cr.uid + cr.gid;
}" HAVE_SO_PEERCRED)

check_c_source_compiles("
#include <sys/types.h>
#include <unistd.h>
int main(int argc, char **argv) {
    setpgrp();
}" HAVE_NULLARY_SETPGRP)
check_c_source_compiles("
#include <sys/types.h>
#include <unistd.h>
int main(int argc, char **argv) {
    setpgrp(0, 0);
}" HAVE_BINARY_SETPGRP)

set(SSHLX_IPV6 ON CACHE BOOL "Build the PuTTY core with IPv6 support")
if(HAVE_GETADDRINFO AND SSHLX_IPV6)
  set(NO_IPV6 OFF)
else()
  set(NO_IPV6 ON)
endif()
if(HAVE_UTMPX_H)
  set(OMIT_UTMP OFF)
else()
  set(OMIT_UTMP ON)
endif()

# sshLX has no GTK front end: none of the GTK/X11-dependent PuTTY files
# are compiled, so we configure the core as PuTTY does when GTK is absent.
set(NOT_X_WINDOWS ON)
set(HAVE_PANGO_FONT_FAMILY_IS_MONOSPACE 0)
set(HAVE_PANGO_FONT_MAP_LIST_FAMILIES 0)
set(HAVE_G_APPLICATION_DEFAULT_FLAGS 0)

# GSSAPI: PuTTY's DYNAMIC mode (dlopen at runtime; no krb5 headers needed).
set(SSHLX_GSSAPI DYNAMIC CACHE STRING "GSSAPI support: DYNAMIC or OFF")
set_property(CACHE SSHLX_GSSAPI PROPERTY STRINGS DYNAMIC OFF)
set(SSHLX_PUTTY_LIBS)
check_library_exists(m pow "" HAVE_LIBm)
if(HAVE_LIBm)
  list(APPEND SSHLX_PUTTY_LIBS m)
endif()
check_library_exists(rt clock_gettime "" HAVE_LIBrt)
if(HAVE_LIBrt)
  list(APPEND SSHLX_PUTTY_LIBS rt)
endif()
if(SSHLX_GSSAPI STREQUAL DYNAMIC)
  check_library_exists(dl dlopen "" HAVE_LIBdl)
  if(HAVE_LIBdl)
    list(APPEND SSHLX_PUTTY_LIBS dl)
  else()
    set(NO_GSSAPI ON)
  endif()
else()
  set(NO_GSSAPI ON)
endif()

set(SSHLX_COMPRESS_SCROLLBACK ON CACHE BOOL
  "Store terminal scrollback compressed (PuTTY default)")
if(NOT SSHLX_COMPRESS_SCROLLBACK)
  set(NO_SCROLLBACK_COMPRESSION ON)
endif()

# --- Crypto acceleration probes (from PuTTY's crypto/CMakeLists.txt) -------
# Each successful probe yields an object library with the extra flags, which
# putty-sources.cmake attaches to the putty-core library.
set(SSHLX_PUTTY_ACCEL_OBJLIBS)
function(sshlx_test_compile_with_flags outvar)
  cmake_parse_arguments(OPT "" ""
    "GNU_FLAGS;ADD_SOURCES_IF_SUCCESSFUL;TEST_SOURCE" "${ARGN}")
  set(flags)
  if(CMAKE_C_COMPILER_ID MATCHES "GNU" OR CMAKE_C_COMPILER_ID MATCHES "Clang")
    set(flags ${OPT_GNU_FLAGS})
  endif()
  foreach(i ${flags})
    set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} ${i}")
  endforeach()
  check_c_source_compiles("${OPT_TEST_SOURCE}" "${outvar}")
  if(${outvar} AND OPT_ADD_SOURCES_IF_SUCCESSFUL)
    set(srcs)
    foreach(s ${OPT_ADD_SOURCES_IF_SUCCESSFUL})
      sshlx_putty_src(resolved crypto/${s})
      list(APPEND srcs ${resolved})
    endforeach()
    set(libname sshlx_putty_accel_${outvar})
    add_library(${libname} OBJECT ${srcs})
    target_compile_options(${libname} PRIVATE ${flags})
    sshlx_putty_target_setup(${libname})
    set(SSHLX_PUTTY_ACCEL_OBJLIBS ${SSHLX_PUTTY_ACCEL_OBJLIBS} ${libname}
      PARENT_SCOPE)
  endif()
  set(${outvar} ${${outvar}} PARENT_SCOPE)
endfunction()

macro(sshlx_putty_crypto_probes)
  sshlx_test_compile_with_flags(HAVE_WMMINTRIN_H
    GNU_FLAGS -msse4.1
    TEST_SOURCE "
      #include <wmmintrin.h>
      #include <smmintrin.h>
      volatile __m128i r, a, b;
      int main(void) { r = _mm_xor_si128(a, b); }")
  if(HAVE_WMMINTRIN_H)
    sshlx_test_compile_with_flags(HAVE_AES_NI
      GNU_FLAGS -msse4.1 -maes
      TEST_SOURCE "
        #include <wmmintrin.h>
        #include <smmintrin.h>
        volatile __m128i r, a, b;
        int main(void) { r = _mm_aesenc_si128(a, b); }"
      ADD_SOURCES_IF_SUCCESSFUL aes-ni.c)
    sshlx_test_compile_with_flags(HAVE_SHAINTRIN_H
      GNU_FLAGS -msse4.1 -msha
      TEST_SOURCE "
        #include <wmmintrin.h>
        #include <smmintrin.h>
        #include <immintrin.h>
        #include <shaintrin.h>
        volatile __m128i r, a, b;
        int main(void) { r = _mm_xor_si128(a, b); }")
    if(HAVE_SHAINTRIN_H)
      set(include_shaintrin "#include <shaintrin.h>")
    else()
      set(include_shaintrin "")
    endif()
    sshlx_test_compile_with_flags(HAVE_SHA_NI
      GNU_FLAGS -msse4.1 -msha
      TEST_SOURCE "
        #include <wmmintrin.h>
        #include <smmintrin.h>
        #include <immintrin.h>
        ${include_shaintrin}
        volatile __m128i r, a, b, c;
        int main(void) { r = _mm_sha256rnds2_epu32(a, b, c); }"
      ADD_SOURCES_IF_SUCCESSFUL sha256-ni.c sha1-ni.c)
    sshlx_test_compile_with_flags(HAVE_CLMUL
      GNU_FLAGS -msse4.1 -mpclmul
      TEST_SOURCE "
        #include <wmmintrin.h>
        #include <tmmintrin.h>
        volatile __m128i r, a, b;
        int main(void) { r = _mm_clmulepi64_si128(a, b, 5);
                         r = _mm_shuffle_epi8(r, a); }"
      ADD_SOURCES_IF_SUCCESSFUL aesgcm-clmul.c)
  endif()

  sshlx_test_compile_with_flags(HAVE_ARM_NEON_H
    TEST_SOURCE "
      #include <arm_neon.h>
      volatile uint8x16_t r, a, b;
      int main(void) { r = veorq_u8(a, b); }")
  if(HAVE_ARM_NEON_H)
    sshlx_test_compile_with_flags(HAVE_NEON_CRYPTO
      GNU_FLAGS -march=armv8-a+crypto
      TEST_SOURCE "
        #include <arm_neon.h>
        volatile uint8x16_t r, a, b;
        volatile uint32x4_t s, x, y, z;
        int main(void) { r = vaeseq_u8(a, b); s = vsha256hq_u32(x, y, z); }"
      ADD_SOURCES_IF_SUCCESSFUL aes-neon.c sha256-neon.c sha1-neon.c)
    sshlx_test_compile_with_flags(HAVE_NEON_PMULL
      GNU_FLAGS -march=armv8-a+crypto
      TEST_SOURCE "
        #include <arm_neon.h>
        volatile poly128_t r;
        volatile poly64_t a, b;
        volatile poly64x2_t u, v;
        int main(void) { r = vmull_p64(a, b); r = vmull_high_p64(u, v); }"
      ADD_SOURCES_IF_SUCCESSFUL aesgcm-neon.c)
    sshlx_test_compile_with_flags(HAVE_NEON_VADDQ_P128
      GNU_FLAGS -march=armv8-a+crypto
      TEST_SOURCE "
        #include <arm_neon.h>
        volatile poly128_t r;
        int main(void) { r = vaddq_p128(r, r); }")
    sshlx_test_compile_with_flags(HAVE_NEON_SHA512_INTRINSICS
      GNU_FLAGS -march=armv8.2-a+crypto+sha3
      TEST_SOURCE "
        #include <arm_neon.h>
        volatile uint64x2_t r, a, b;
        int main(void) { r = vsha512su0q_u64(a, b); }"
      ADD_SOURCES_IF_SUCCESSFUL sha512-neon.c)
    if(HAVE_NEON_SHA512_INTRINSICS)
      set(HAVE_NEON_SHA512 ON)
    endif()
  endif()

  sshlx_test_compile_with_flags(HAVE_ARM_DIT
    TEST_SOURCE "
      #ifndef __aarch64__
      #error make sure this only even tries to work on AArch64
      #endif
      #include <stdint.h>
      int main(void) {
        register uint64_t one asm(\"x8\");
        one = 1;
        asm volatile(\".inst 0xd51b42a8\" :: \"r\"(one));
      }"
    ADD_SOURCES_IF_SUCCESSFUL enable_dit.c)
endmacro()

# --- Generated sources -----------------------------------------------------
# cmake.h is produced from PuTTY's own template, written into our build tree.
macro(sshlx_putty_generate_cmake_h)
  configure_file(${_P}/cmake/cmake.h.in ${SSHLX_PUTTY_GENERATED_DIR}/cmake.h)
endmacro()

# sbcsdat.c: PuTTY's charset tables, generated by its own Perl script.
find_package(Perl REQUIRED)
set(SSHLX_PUTTY_SBCSDAT_C ${SSHLX_PUTTY_GENERATED_DIR}/sbcsdat.c)
add_custom_command(OUTPUT ${SSHLX_PUTTY_SBCSDAT_C}
  COMMAND ${PERL_EXECUTABLE} ${_P}/charset/sbcsgen.pl
    -o ${SSHLX_PUTTY_SBCSDAT_C}
  DEPENDS ${_P}/charset/sbcsgen.pl ${_P}/charset/sbcs.dat
  WORKING_DIRECTORY ${SSHLX_PUTTY_GENERATED_DIR}
  COMMENT "Generating PuTTY sbcsdat.c into the sshLX build tree")

# cmake_commit.c: PuTTY expects a 'commitid' string. sshLX builds from a
# release tarball, so we record that instead of running git in PuTTY's tree.
set(SSHLX_PUTTY_COMMIT_C ${SSHLX_PUTTY_GENERATED_DIR}/cmake_commit.c)
file(WRITE ${SSHLX_PUTTY_COMMIT_C}.tmp
  "/* Generated by sshLX. */\nconst char commitid[] = \"unavailable (PuTTY release source, built by sshLX ${PROJECT_VERSION})\";\n")
configure_file(${SSHLX_PUTTY_COMMIT_C}.tmp ${SSHLX_PUTTY_COMMIT_C} COPYONLY)
