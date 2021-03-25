cmake_minimum_required(VERSION 3.13.4)

project(
  nthm VERSION 0.7.5
  DESCRIPTION "non-preemptive thread hierarchy manager"
  HOMEPAGE_URL https://github.com/gueststar/nthm
  LANGUAGES C)

set (MAINTAINER_EMAIL milonga@delayinsensitive.com)

include(GNUInstallDirs)

# ------------- markdown documents -----------

install(
  FILES ${CMAKE_CURRENT_SOURCE_DIR}/README.md CONTRIBUTING.md
  DESTINATION ${CMAKE_INSTALL_DOCDIR})

# ------------- header file ------------------

install(
  DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/nthm
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

#---------------- manual pages ---------------

# Manual pages are compressed and configured with current dates
# etc. by a separate cmake script so that editing them will enable a
# rebuild.

find_program(GZIP gzip)

if (GZIP)
  set (GZIP_MANPAGES ${GZIP} -rf man)
else ()
  set (GZIP_MANPAGES "")
  message (STATUS "gzip not found; manual pages not compressed")
endif ()

configure_file(
  cmake/configure_manpages.cmake.in configure_manpages.cmake
  @ONLY)

if (${CMAKE_VERSION} VERSION_LESS 3.17)      # may have to use less portable commands
  add_custom_command(
	 OUTPUT man
	 DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/man/*/*
	 COMMAND rm -rf man
	 COMMAND ${CMAKE_COMMAND} -P configure_manpages.cmake
	 COMMAND cp -r ${CMAKE_CURRENT_BINARY_DIR}/configured_man ${CMAKE_CURRENT_BINARY_DIR}/man
	 COMMAND ${GZIP_MANPAGES})
else ()
  add_custom_command(
	 OUTPUT man
	 DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/man/*/*
	 COMMAND ${CMAKE_COMMAND} -E rm -rf man
	 COMMAND ${CMAKE_COMMAND} -P configure_manpages.cmake
	 COMMAND ${CMAKE_COMMAND} -E copy_directory configured_man man
	 COMMAND ${GZIP_MANPAGES})
endif ()

add_custom_target(
  manpages
  DEPENDS man)
 
install(
  DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/man/man3 ${CMAKE_CURRENT_BINARY_DIR}/man/man7
  DESTINATION ${CMAKE_INSTALL_MANDIR})

# ------------- uninstall target -------------

if(NOT TARGET uninstall)
  configure_file(
    cmake/cmake_uninstall.cmake.in cmake_uninstall.cmake
    @ONLY)
  add_custom_target(
    uninstall
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake)
endif()

#-------------- shared library --------------

add_library(nthm SHARED 
  src/errs.c
  src/pipl.c
  src/scopes.c
  src/pipes.c
  src/sync.c
  src/pool.c
  src/plumbing.c
  src/context.c
  src/protocol.c
  src/api.c)

# It's standard practice on GNU/Linux systems to install a shared
# library with symbolic links like libnthm.so -> libnthm.so.X ->
# libnthm.so.X.Y, where X and Y are respectively the major and minor
# version numbers. The convention is that libraries differing only in
# Y are API compatible but an increment to X signifies a breaking
# change. To install these symbolic links, set the SOVERSION target
# property to X and VERSION target property to X.Y. CMake seems to
# require X in both places unless I'm missing something.

set_target_properties (
  nthm PROPERTIES SOVERSION ${nthm_VERSION_MAJOR} VERSION ${nthm_VERSION_MAJOR}.${nthm_VERSION_MINOR})

# The MEMTEST setting enables internal diagnostics to detect some
# memory leaks as a much faster alternative to valgrind. It's good for
# running huge numbers of random tests but should be disabled in
# production code.

# set (MEMTEST 1)

target_include_directories(
  nthm
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/nthm>
    $<INSTALL_INTERFACE:include/nthm>
  PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/src)

cmake_host_system_information(RESULT IS64BIT QUERY IS_64BIT)

if (NOT IS64BIT)
  message (STATUS "32-bit system detected; stack sizes will be limited")
  set (USE_SMALL_STACKS 1)
endif ()

configure_file (src/nthmconfig.h.in ${CMAKE_CURRENT_BINARY_DIR}/src/nthmconfig.h)

install(
  TARGETS nthm
  EXPORT nthmConfig
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})

install(
  EXPORT nthmConfig
  DESTINATION share/nthm/cmake)

add_dependencies(nthm manpages)

#-------------- linked libraries ------------

find_library(PTHREAD pthread)

if (NOT PTHREAD)
   message(FATAL_ERROR "pthread library not found")
endif ()

target_link_libraries(nthm pthread)

find_library(JEMALLOC jemalloc)
find_library(TCMALLOC tcmalloc_minimal)
find_library(MIMALLOC mimalloc)

# Mimalloc is the default choice because it's fastest on the examples
# tested, and tcmalloc is the second choice because jemalloc segfaults
# non-deterministically in resource-constrained environments.

if (MIMALLOC)
  message (STATUS "found mimalloc")
  target_link_libraries(nthm mimalloc)
elseif (TCMALLOC)
  message (STATUS "found tcmalloc")
  target_link_libraries(nthm tcmalloc_minimal)
elseif (JEMALLOC)
  message (STATUS "found jemalloc")
  target_link_libraries(nthm jemalloc)
else ()
  message (STATUS "mimalloc/tcmalloc/jemalloc not found; expect slowness")
endif()

#-------------- tests -----------------------

include(CTest)

find_library (RANDOM random)

configure_file (test/testconfig.h.in test/testconfig.h)

find_program(VALGRIND valgrind)

if (VALGRIND)
  set (MEMCHECK_COMMAND ${VALGRIND} --leak-check=full --show-leak-kinds=all --soname-synonyms=somalloc=NONE)
  set (DRDCHECK_COMMAND ${VALGRIND} --tool=drd)
  message (STATUS "found valgrind; memory and thread safety tests enabled")
else ()
  message (STATUS "valgrind not found; some tests omitted")
endif ()

function(testme testname)
  add_executable(${testname} test/${testname}.c)
  target_link_libraries(${testname} nthm)
  target_include_directories(
	 ${testname}
	 PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/nthm>
    PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/test)
  add_test (NAME ${testname} COMMAND ${testname})
  if (MEMTEST)
	 set_tests_properties(${testname} PROPERTIES FAIL_REGULAR_EXPRESSION "internal|unreclaimed")
  else ()
	 set_tests_properties(${testname} PROPERTIES FAIL_REGULAR_EXPRESSION "internal")
  endif ()
  if (VALGRIND)
	 add_test (NAME ${testname}_mem COMMAND ${MEMCHECK_COMMAND} ${CMAKE_CURRENT_BINARY_DIR}/${testname})
	 set_tests_properties(${testname}_mem PROPERTIES PASS_REGULAR_EXPRESSION "no leaks")
	 add_test (NAME ${testname}_drd COMMAND ${DRDCHECK_COMMAND} ${CMAKE_CURRENT_BINARY_DIR}/${testname})
	 set_tests_properties(${testname}_drd PROPERTIES FAIL_REGULAR_EXPRESSION "Acquired")
  endif ()
endfunction()

testme(pipein)
testme(pipeout)
testme(flatpool)
testme(deeppool)
testme(freepool)
testme(rubbish)
testme(killjoy)
testme(bizzyb)
testme(scopestrial)
testme(sendany)
