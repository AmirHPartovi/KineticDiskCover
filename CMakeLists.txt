cmake_minimum_required(VERSION 3.20)
project(kdc-solver VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_TESTS "Build tests" ON)
option(BUILD_BENCHMARK "Build benchmark" ON)
set(KONT_ROOT "" CACHE PATH "KONT installation root")

find_path(KONT_INCLUDE_DIR copt.h HINTS "${KONT_ROOT}/include")
find_library(KONT_LIBRARY NAMES copt HINTS "${KONT_ROOT}/lib")
set(KDC_KONT_AVAILABLE FALSE)
if(KONT_INCLUDE_DIR AND KONT_LIBRARY)
  set(KDC_KONT_AVAILABLE TRUE)
else()
  message(STATUS "KONT not found; using the built-in exact fallback and MockILPSolver")
endif()

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

include(CheckCXXSourceCompiles)
unset(KDC_HAS_COPT_CPP_API CACHE)
if(KDC_KONT_AVAILABLE)
  set(CMAKE_REQUIRED_INCLUDES "${KONT_INCLUDE_DIR}")
  set(CMAKE_REQUIRED_LIBRARIES "${KONT_LIBRARY}")
  check_cxx_source_compiles([=[
  #include <copt.h>
  #include <string>
  #include <vector>
  int main() {
    static copt::Env env;
    copt::Model model = env.CreateModel("kdc_api_check");
    copt::Var variable =
        model.AddVar(0.0, 1.0, 1.0, COPT_BINARY, "x");
    copt::Expr expression;
    expression += 1.0 * variable;
    model.AddConstr(expression, COPT_GREATER_EQUAL, 1.0, "c");
    model.SetDblParam(COPT_DBLPARAM_TIMELIMIT, 1.0);
    model.SetDblParam(COPT_DBLPARAM_RELGAP, 0.0);
    model.SetIntParam(COPT_INTPARAM_LOGGING, 0);
    model.SetIntParam(COPT_INTPARAM_THREADS, 1);
    if (model.Solve() == 0) {
      (void)variable.Get(COPT_DBLINFO_VALUE);
      (void)model.GetDblAttr(COPT_DBLATTR_LPOBJVAL);
      (void)model.GetDblAttr(COPT_DBLATTR_BESTBND);
      (void)model.GetDblAttr(COPT_DBLATTR_SOLVINGTIME);
      (void)model.GetDblAttr(COPT_DBLATTR_MIPGAP);
      switch (model.GetIntAttr(COPT_INTATTR_MIPSTATUS)) {
        case COPT_MIP_STATUS_OPTIMAL:
        case COPT_MIP_STATUS_INFEASIBLE:
        case COPT_MIP_STATUS_UNBOUNDED:
        case COPT_MIP_STATUS_TIMELIMIT:
        case COPT_MIP_STATUS_GAPLIMIT:
          break;
        default:
          break;
      }
      (void)model.GetMessage();
    }
    model.clear();
    return 0;
  }
]=] KDC_HAS_COPT_CPP_API)
endif()
unset(CMAKE_REQUIRED_INCLUDES)
unset(CMAKE_REQUIRED_LIBRARIES)

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
target_include_directories(kdc-core PUBLIC include)
if(KDC_HAS_COPT_CPP_API)
  target_link_libraries(kdc-core PUBLIC ${KONT_LIBRARY})
  target_include_directories(kdc-core PUBLIC ${KONT_INCLUDE_DIR})
  target_compile_definitions(kdc-core PRIVATE KDC_HAS_COPT_CPP_API)
  target_compile_definitions(kdc-core PUBLIC KDC_HAS_KONT)
endif()
target_compile_options(kdc-core PRIVATE
  $<$<CONFIG:Debug>:-Wall;-Wextra;-Wpedantic;-Wshadow;-Wconversion;-Werror>
  $<$<CONFIG:Release>:-O3;-march=native;-DNDEBUG>
)

add_executable(kdc-solver src/main.cpp)
target_link_libraries(kdc-solver PRIVATE kdc-core)

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
