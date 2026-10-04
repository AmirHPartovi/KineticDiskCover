cmake_minimum_required(VERSION 3.20)
project(kdc-solver VERSION 1.0.0 LANGUAGES CXX)

execute_process(
  COMMAND git rev-parse HEAD
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  OUTPUT_VARIABLE KDC_GIT_COMMIT
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)
if(NOT KDC_GIT_COMMIT)
  set(KDC_GIT_COMMIT "unknown")
endif()
execute_process(
  COMMAND git status --porcelain
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  OUTPUT_VARIABLE KDC_GIT_STATUS
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)
if(KDC_GIT_STATUS)
  set(KDC_GIT_DIRTY "true")
else()
  set(KDC_GIT_DIRTY "false")
endif()
string(TOUPPER "${CMAKE_BUILD_TYPE}" KDC_BUILD_TYPE_UPPER)
set(KDC_RECORDED_COMPILER_FLAGS
    "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${KDC_BUILD_TYPE_UPPER}}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_TESTS "Build tests" ON)
option(BUILD_BENCHMARK "Build benchmark" ON)
set(KONT_ROOT "" CACHE PATH "KONT installation root")

set(KONT_DETECTED_ROOT "${KONT_ROOT}")
set(KONT_ROOT_SOURCE "KONT_ROOT cache option")
if(NOT KONT_DETECTED_ROOT AND DEFINED ENV{KONT_ROOT} AND
   NOT "$ENV{KONT_ROOT}" STREQUAL "")
  set(KONT_DETECTED_ROOT "$ENV{KONT_ROOT}")
  set(KONT_ROOT_SOURCE "KONT_ROOT environment variable")
elseif(NOT KONT_DETECTED_ROOT AND DEFINED ENV{COPT_HOME} AND
       NOT "$ENV{COPT_HOME}" STREQUAL "")
  set(KONT_DETECTED_ROOT "$ENV{COPT_HOME}")
  set(KONT_ROOT_SOURCE "COPT_HOME environment variable")
elseif(NOT KONT_DETECTED_ROOT AND DEFINED ENV{KONT_HOME} AND
       NOT "$ENV{KONT_HOME}" STREQUAL "")
  set(KONT_DETECTED_ROOT "$ENV{KONT_HOME}")
  set(KONT_ROOT_SOURCE "KONT_HOME environment variable")
elseif(NOT KONT_DETECTED_ROOT)
  set(KONT_ROOT_SOURCE "not supplied")
endif()

set(KONT_ARCH_SUFFIXES lib lib64 lib/arm64-osx lib/x86_64-osx
                       lib/macos-arm64 lib/macos-x86_64)
find_path(COPT_INCLUDE_DIR NAMES copt.h
  HINTS "${KONT_DETECTED_ROOT}"
  PATH_SUFFIXES include)
find_library(COPT_LIBRARY NAMES copt
  HINTS "${KONT_DETECTED_ROOT}"
  PATH_SUFFIXES ${KONT_ARCH_SUFFIXES})
find_path(KONT_INCLUDE_DIR NAMES kont.h
  HINTS "${KONT_DETECTED_ROOT}"
  PATH_SUFFIXES include)
find_path(KONT_CPP_INCLUDE_DIR NAMES kontcpp_pch.h
  HINTS "${KONT_DETECTED_ROOT}"
  PATH_SUFFIXES include/kontcpp_inc include)
find_library(KONT_CPP_LIBRARY NAMES kont_cpp
  HINTS "${KONT_DETECTED_ROOT}"
  PATH_SUFFIXES ${KONT_ARCH_SUFFIXES})

set(KDC_KONT_AVAILABLE FALSE)
set(KDC_KONT_NATIVE_API "unavailable")
set(KDC_KONT_API_COMPILE_STATUS "NOT RUN")
set(KDC_KONT_API_FAILURE_REASON "")
set(KDC_KONT_INCLUDE_PATH "")
set(KDC_KONT_LIBRARY_PATH "")
set(KDC_KONT_VERSION "unknown")
include(CheckCXXSourceCompiles)
if(COPT_INCLUDE_DIR AND COPT_LIBRARY)
  set(CMAKE_REQUIRED_INCLUDES "${COPT_INCLUDE_DIR}")
  set(CMAKE_REQUIRED_LIBRARIES "${COPT_LIBRARY}")
  check_cxx_source_compiles([=[
    #include <copt.h>
    #include <string>
    int main() {
      static copt::Env env;
      copt::Model model = env.CreateModel("kdc_api_check");
      copt::Var x = model.AddVar(0.0, 1.0, 1.0, COPT_BINARY, "x");
      copt::Expr expr;
      expr += 1.0 * x;
      model.AddConstr(expr, COPT_GREATER_EQUAL, 1.0, "c");
      model.SetDblParam(COPT_DBLPARAM_TIMELIMIT, 1.0);
      model.SetDblParam(COPT_DBLPARAM_RELGAP, 0.0);
      model.SetIntParam(COPT_INTPARAM_LOGGING, 0);
      model.SetIntParam(COPT_INTPARAM_THREADS, 1);
      const int solve_code = model.Solve();
      if (solve_code == 0) {
        (void)x.Get(COPT_DBLINFO_VALUE);
        (void)model.GetDblAttr(COPT_DBLATTR_LPOBJVAL);
        (void)model.GetDblAttr(COPT_DBLATTR_BESTBND);
        (void)model.GetDblAttr(COPT_DBLATTR_SOLVINGTIME);
        (void)model.GetDblAttr(COPT_DBLATTR_MIPGAP);
        (void)model.GetIntAttr(COPT_INTATTR_MIPSTATUS);
        (void)model.GetMessage();
      }
      model.clear();
      return 0;
    }
  ]=] KDC_COPT_CPP_API_COMPILES)
  if(KDC_COPT_CPP_API_COMPILES)
    set(KDC_KONT_NATIVE_API "COPT")
    set(KDC_KONT_AVAILABLE TRUE)
    set(KDC_KONT_API_COMPILE_STATUS "PASS")
    set(KDC_KONT_INCLUDE_PATH "${COPT_INCLUDE_DIR}")
    set(KDC_KONT_LIBRARY_PATH "${COPT_LIBRARY}")
    set(KDC_KONT_VERSION "unknown")
  else()
    set(KDC_KONT_API_FAILURE_REASON
        "COPT headers and library were found, but the required C++ API compile/link check failed")
  endif()
endif()

if(NOT KDC_KONT_AVAILABLE AND KONT_INCLUDE_DIR AND KONT_CPP_INCLUDE_DIR AND
   KONT_CPP_LIBRARY)
  set(CMAKE_REQUIRED_INCLUDES "${KONT_INCLUDE_DIR};${KONT_CPP_INCLUDE_DIR}")
  set(CMAKE_REQUIRED_LIBRARIES "${KONT_CPP_LIBRARY}")
  check_cxx_source_compiles([=[
    #include <kontcpp_pch.h>
    #include <string>
    int main() {
      static_assert(KONT_VERSION_MAJOR > 0, "KONT version macros are required");
      Envr env;
      Model model = env.CreateModel("kdc_api_check");
      Var x = model.AddVar(0.0, 1.0, 1.0, KONT_BINARY, "x");
      Expr expr(x);
      expr.AddTerm(x, 0.0);
      model.AddConstr(expr, KONT_GREATER_EQUAL, 1.0, "c");
      model.SetObjective(expr, KONT_MINIMIZE);
      model.SetDblParam(KONT_DBLPARAM_TIMELIMIT, 1.0);
      model.SetDblParam(KONT_DBLPARAM_RELGAP, 0.0);
      model.SetIntParam(KONT_INTPARAM_LOGGING, 0);
      model.SetIntParam(KONT_INTPARAM_LOGTOCONSOLE, 0);
      model.SetIntParam(KONT_INTPARAM_THREADS, 1);
      model.Solve();
      (void)x.Get(KONT_DBLINFO_VALUE);
      (void)model.GetDblAttr(KONT_DBLATTR_BESTOBJ);
      (void)model.GetDblAttr(KONT_DBLATTR_BESTBND);
      (void)model.GetDblAttr(KONT_DBLATTR_SOLVINGTIME);
      (void)model.GetDblAttr(KONT_DBLATTR_BESTGAP);
      const int status = model.GetIntAttr(KONT_INTATTR_MIPSTATUS);
      (void)model.GetIntAttr(KONT_INTATTR_HASMIPSOL);
      (void)status;
      (void)x.GetName();
      return 0;
    }
  ]=] KDC_KONT_CPP_API_COMPILES)
  if(KDC_KONT_CPP_API_COMPILES)
    set(KDC_KONT_NATIVE_API "KONT")
    set(KDC_KONT_AVAILABLE TRUE)
    set(KDC_KONT_API_COMPILE_STATUS "PASS")
    set(KDC_KONT_INCLUDE_PATH "${KONT_INCLUDE_DIR};${KONT_CPP_INCLUDE_DIR}")
    set(KDC_KONT_LIBRARY_PATH "${KONT_CPP_LIBRARY}")
    file(STRINGS "${KONT_INCLUDE_DIR}/kont.h" KONT_VERSION_LINES
         REGEX "^#define KONT_VERSION_(MAJOR|MINOR|TECHNICAL)[ \t]+[0-9]+")
    foreach(VERSION_LINE IN LISTS KONT_VERSION_LINES)
      if(VERSION_LINE MATCHES "KONT_VERSION_MAJOR[ \t]+([0-9]+)")
        set(KONT_VERSION_MAJOR "${CMAKE_MATCH_1}")
      elseif(VERSION_LINE MATCHES "KONT_VERSION_MINOR[ \t]+([0-9]+)")
        set(KONT_VERSION_MINOR "${CMAKE_MATCH_1}")
      elseif(VERSION_LINE MATCHES "KONT_VERSION_TECHNICAL[ \t]+([0-9]+)")
        set(KONT_VERSION_TECHNICAL "${CMAKE_MATCH_1}")
      endif()
    endforeach()
    if(DEFINED KONT_VERSION_MAJOR AND DEFINED KONT_VERSION_MINOR AND
       DEFINED KONT_VERSION_TECHNICAL)
      set(KDC_KONT_VERSION
          "${KONT_VERSION_MAJOR}.${KONT_VERSION_MINOR}.${KONT_VERSION_TECHNICAL}")
    endif()
  else()
    set(KDC_KONT_API_FAILURE_REASON
        "KONT headers and libkont_cpp were found, but the required C++ API compile/link check failed")
  endif()
endif()
unset(CMAKE_REQUIRED_INCLUDES)
unset(CMAKE_REQUIRED_LIBRARIES)

if(NOT KDC_KONT_AVAILABLE AND NOT KDC_KONT_API_FAILURE_REASON)
  if(NOT KONT_DETECTED_ROOT)
    set(KDC_KONT_API_FAILURE_REASON
        "KONT_ROOT was not supplied and no supported KONT/COPT installation was found")
  elseif(NOT COPT_INCLUDE_DIR AND NOT KONT_INCLUDE_DIR)
    set(KDC_KONT_API_FAILURE_REASON
        "neither copt.h nor kont.h was found under the detected installation root")
  elseif((COPT_INCLUDE_DIR AND NOT COPT_LIBRARY) OR
         (KONT_INCLUDE_DIR AND NOT KONT_CPP_LIBRARY))
    set(KDC_KONT_API_FAILURE_REASON
        "a solver header was found but its required C++ library was not found")
  else()
    set(KDC_KONT_API_FAILURE_REASON
        "the detected installation is missing the C++ wrapper header or library")
  endif()
endif()

if(CMAKE_OSX_ARCHITECTURES)
  string(JOIN "," KDC_DETECTED_ARCHITECTURE ${CMAKE_OSX_ARCHITECTURES})
else()
  set(KDC_DETECTED_ARCHITECTURE "${CMAKE_SYSTEM_PROCESSOR}")
endif()
if(NOT KDC_DETECTED_ARCHITECTURE)
  set(KDC_DETECTED_ARCHITECTURE "unknown")
endif()

message(STATUS "KONT/COPT root: ${KONT_DETECTED_ROOT} (${KONT_ROOT_SOURCE})")
message(STATUS "COPT copt.h: ${COPT_INCLUDE_DIR}")
message(STATUS "COPT library: ${COPT_LIBRARY}")
message(STATUS "KONT C++ header: ${KONT_INCLUDE_DIR}; ${KONT_CPP_INCLUDE_DIR}")
message(STATUS "KONT C++ library: ${KONT_CPP_LIBRARY}")
message(STATUS "Detected architecture: ${KDC_DETECTED_ARCHITECTURE}")
message(STATUS "Native API compile/link check: ${KDC_KONT_API_COMPILE_STATUS}")
if(KDC_KONT_AVAILABLE)
  message(STATUS "Native backend enabled: ${KDC_KONT_NATIVE_API} (version ${KDC_KONT_VERSION})")
else()
  message(STATUS "Native backend disabled: ${KDC_KONT_API_FAILURE_REASON}")
endif()

function(kdc_cpp_string OUTPUT INPUT)
  string(REPLACE "\\" "\\\\" escaped "${INPUT}")
  string(REPLACE "\"" "\\\"" escaped "${escaped}")
  set(${OUTPUT} "\"${escaped}\"" PARENT_SCOPE)
endfunction()
kdc_cpp_string(KDC_KONT_ROOT_CPP "${KONT_DETECTED_ROOT}")
kdc_cpp_string(KDC_KONT_ROOT_SOURCE_CPP "${KONT_ROOT_SOURCE}")
kdc_cpp_string(KDC_COPT_HEADER_CPP "${COPT_INCLUDE_DIR}")
kdc_cpp_string(KDC_COPT_LIBRARY_CPP "${COPT_LIBRARY}")
kdc_cpp_string(KDC_KONT_HEADER_CPP "${KONT_INCLUDE_DIR}")
kdc_cpp_string(KDC_KONT_CPP_HEADER_CPP "${KONT_CPP_INCLUDE_DIR}")
kdc_cpp_string(KDC_KONT_CPP_LIBRARY_CPP "${KONT_CPP_LIBRARY}")
kdc_cpp_string(KDC_KONT_INCLUDE_CPP "${KDC_KONT_INCLUDE_PATH}")
kdc_cpp_string(KDC_KONT_LIBRARY_CPP "${KDC_KONT_LIBRARY_PATH}")
kdc_cpp_string(KDC_KONT_ARCH_CPP "${KDC_DETECTED_ARCHITECTURE}")
kdc_cpp_string(KDC_KONT_API_CPP "${KDC_KONT_NATIVE_API}")
kdc_cpp_string(KDC_KONT_API_STATUS_CPP "${KDC_KONT_API_COMPILE_STATUS}")
kdc_cpp_string(KDC_KONT_API_FAILURE_CPP "${KDC_KONT_API_FAILURE_REASON}")
kdc_cpp_string(KDC_KONT_VERSION_CPP "${KDC_KONT_VERSION}")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated/kdc")
configure_file(include/kdc/kont_build_config.hpp.in
               "${CMAKE_CURRENT_BINARY_DIR}/generated/kdc/kont_build_config.hpp"
               @ONLY)

include(FetchContent)
set(BUILD_TESTING OFF CACHE BOOL "Disable tests in fetched dependencies" FORCE)
set(EIGEN_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(CATCH_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(CATCH_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CATCH_BUILD_EXTRA_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(eigen URL https://gitlab.com/libeigen/eigen/-/archive/3.4.0/eigen-3.4.0.tar.gz)
FetchContent_Declare(json URL https://github.com/nlohmann/json/archive/refs/tags/v3.11.3.tar.gz)
FetchContent_Declare(catch2 URL https://github.com/catchorg/Catch2/archive/refs/tags/v3.5.2.tar.gz)
FetchContent_Declare(spdlog URL https://github.com/gabime/spdlog/archive/refs/tags/v1.15.0.tar.gz)
FetchContent_MakeAvailable(eigen json catch2 spdlog)

file(GLOB KDC_SOURCES CONFIGURE_DEPENDS src/*.cpp)
list(APPEND KDC_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/src/static_solver_registry.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/algorithms/nn_static_solver.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/algorithms/ip_static_solver.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/algorithms/brute_force_solver.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/algorithms/branch_and_bound_solver.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/algorithms/greedy_set_cover_solver.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/algorithms/lp_rounding_solver.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/algorithms/primal_dual_solver.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/algorithms/local_search_solver.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/algorithms/simulated_annealing_solver.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/algorithms/genetic_solver.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/algorithms/shifting_strategy_solver.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/algorithm_comparison.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/stats.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/solution_serializer.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/cli_utils.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/mock_ilp_solver.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/trace.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/comparative_runner.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/thread_pool.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/sanity_check.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/batch_runner.cpp"
)
list(REMOVE_DUPLICATES KDC_SOURCES)
list(REMOVE_ITEM KDC_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")
add_library(kdc-core ${KDC_SOURCES})
target_link_libraries(kdc-core PUBLIC Eigen3::Eigen nlohmann_json::nlohmann_json spdlog::spdlog)
target_include_directories(kdc-core PUBLIC
  include "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_definitions(kdc-core PRIVATE
  KDC_GIT_COMMIT="${KDC_GIT_COMMIT}"
  KDC_GIT_DIRTY="${KDC_GIT_DIRTY}"
  KDC_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
  KDC_COMPILER_FLAGS="${KDC_RECORDED_COMPILER_FLAGS}"
)
if(KDC_KONT_AVAILABLE)
  add_library(kdc-kont-native MODULE src/kont_native_adapter.cpp)
  target_include_directories(kdc-kont-native PRIVATE
    include "${CMAKE_CURRENT_BINARY_DIR}/generated")
  target_link_libraries(kdc-kont-native PRIVATE Eigen3::Eigen)
  if(KDC_KONT_NATIVE_API STREQUAL "COPT")
    target_include_directories(kdc-kont-native PRIVATE ${COPT_INCLUDE_DIR})
    target_link_libraries(kdc-kont-native PRIVATE ${COPT_LIBRARY})
    target_compile_definitions(kdc-kont-native PRIVATE KDC_NATIVE_API_COPT)
  else()
    target_include_directories(kdc-kont-native PRIVATE
      ${KONT_INCLUDE_DIR} ${KONT_CPP_INCLUDE_DIR})
    target_link_libraries(kdc-kont-native PRIVATE ${KONT_CPP_LIBRARY})
    target_compile_definitions(kdc-kont-native PRIVATE KDC_NATIVE_API_KONT)
  endif()
  get_filename_component(KDC_KONT_LIBRARY_DIRECTORY
                         "${KDC_KONT_LIBRARY_PATH}" DIRECTORY)
  set_target_properties(kdc-kont-native PROPERTIES
    BUILD_RPATH "${KDC_KONT_LIBRARY_DIRECTORY}")
  target_compile_definitions(kdc-core PRIVATE
    KDC_HAS_KONT_NATIVE_PLUGIN
    KDC_KONT_NATIVE_PLUGIN_PATH=\"$<TARGET_FILE:kdc-kont-native>\")
  target_compile_definitions(kdc-core PUBLIC KDC_HAS_KONT)
  target_link_libraries(kdc-core PUBLIC ${CMAKE_DL_LIBS})
endif()
target_compile_options(kdc-core PRIVATE
  $<$<CONFIG:Debug>:-Wall;-Wextra;-Wpedantic;-Wshadow;-Wconversion;-Werror>
  $<$<CONFIG:Release>:-O3;-march=native;-DNDEBUG>
)

add_executable(kdc-solver src/main.cpp)
target_link_libraries(kdc-solver PRIVATE kdc-core)
if(TARGET kdc-kont-native)
  add_dependencies(kdc-solver kdc-kont-native)
endif()

if(BUILD_TESTS)
  enable_testing()
  file(GLOB TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp)
  add_executable(kdc-tests ${TEST_SOURCES})
  target_link_libraries(kdc-tests PRIVATE kdc-core Catch2::Catch2WithMain)
  target_include_directories(kdc-tests PRIVATE include)
  add_dependencies(kdc-tests kdc-solver)
  target_compile_definitions(
    kdc-tests PRIVATE KDC_SOLVER_EXECUTABLE="$<TARGET_FILE:kdc-solver>")
  add_test(NAME all COMMAND kdc-tests --allow-running-no-tests)
endif()
