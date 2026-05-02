cmake_minimum_required(VERSION 3.20)

project(gr_sim CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CheckCXXCompilerFlag)

option(GR_SIM_ENABLE_NATIVE_BENCH_FLAGS
    "Enable CPU-specific optimization flags for gr_bench when building on a supported machine"
    OFF)

add_compile_options(-O2 -Wall -Wextra)

# ---------------------------------------------------------------------------
# Optional dependencies
# ---------------------------------------------------------------------------

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(PAPI QUIET papi)
endif()

# ---------------------------------------------------------------------------
# gr_sim_core — static library shared by simulator and exercises
# ---------------------------------------------------------------------------

file(GLOB_RECURSE CORE_SOURCES
    src/*.cpp
    src/console/*.cpp
    src/system/*.cpp
    src/engine/*.cpp
)
list(FILTER CORE_SOURCES EXCLUDE REGEX "bench/")
list(FILTER CORE_SOURCES EXCLUDE REGEX "src/main\\.cpp$")

add_library(gr_sim_core STATIC ${CORE_SOURCES})
target_include_directories(gr_sim_core PUBLIC src)

# ---------------------------------------------------------------------------
# gr_sim — main simulator
# ---------------------------------------------------------------------------

add_executable(gr_sim src/main.cpp)
target_link_libraries(gr_sim PRIVATE gr_sim_core)

# ---------------------------------------------------------------------------
# exercises
# ---------------------------------------------------------------------------

add_subdirectory(exercises)

# ---------------------------------------------------------------------------
# gr_bench — hardware characterization tool (standalone)
# ---------------------------------------------------------------------------

file(GLOB BENCH_SOURCES bench/*.cpp)
add_executable(gr_bench ${BENCH_SOURCES})
target_include_directories(gr_bench PRIVATE bench)

if(GR_SIM_ENABLE_NATIVE_BENCH_FLAGS)
    check_cxx_compiler_flag("-mfma"         _has_mfma)
    check_cxx_compiler_flag("-mfpmath=sse"  _has_mfpmath_sse)
    check_cxx_compiler_flag("-march=native" _has_march_native)

    if(_has_mfma)
        target_compile_options(gr_bench PRIVATE -mfma)
    endif()
    if(_has_mfpmath_sse)
        target_compile_options(gr_bench PRIVATE -mfpmath=sse)
    endif()
    if(_has_march_native)
        target_compile_options(gr_bench PRIVATE -march=native)
    endif()

    message(STATUS "gr_bench native ISA flags enabled when supported by the compiler")
else()
    message(STATUS "gr_bench native ISA flags disabled for portable builds")
endif()

if(PAPI_FOUND)
    target_compile_definitions(gr_bench PRIVATE HAVE_PAPI)
    target_include_directories(gr_bench PRIVATE ${PAPI_INCLUDE_DIRS})
    target_link_libraries(gr_bench PRIVATE ${PAPI_LIBRARIES})
    message(STATUS "PAPI found — hardware counters enabled")
else()
    message(STATUS "PAPI not found — gr_bench will use proxy measurement")
endif()

# ---------------------------------------------------------------------------
# Build info
# ---------------------------------------------------------------------------
message(STATUS "Targets: gr_sim  gr_bench")
message(STATUS "Run gr_bench first: ./gr_bench --output cache/hardware_profile.bin")


add_custom_target(math_renderer_mvp
    COMMAND ${CMAKE_COMMAND} -E echo "Use: python -m tools.math_renderer.main --src src --output docs/generated"
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Math renderer MVP hint target"
)