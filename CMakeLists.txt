cmake_minimum_required(VERSION 3.28)

project(gr_sim CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Compiler flags — required for correct FMA counting in gr_bench
add_compile_options(-O2 -mfma -mfpmath=sse -march=native -Wall -Wextra)

# ---------------------------------------------------------------------------
# Optional dependencies
# ---------------------------------------------------------------------------

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(PAPI QUIET papi)
endif()

# ---------------------------------------------------------------------------
# gr_sim — main simulator
# ---------------------------------------------------------------------------

file(GLOB_RECURSE SIM_SOURCES
    src/*.cpp
    src/console/*.cpp
    src/system/*.cpp
    src/engine/*.cpp
)
# Exclude bench sources from simulator target
list(FILTER SIM_SOURCES EXCLUDE REGEX "bench/")

add_executable(gr_sim ${SIM_SOURCES})
target_include_directories(gr_sim PRIVATE src)

# ---------------------------------------------------------------------------
# gr_bench — hardware characterization tool (standalone)
# ---------------------------------------------------------------------------

file(GLOB BENCH_SOURCES bench/*.cpp)
add_executable(gr_bench ${BENCH_SOURCES})
target_include_directories(gr_bench PRIVATE bench)

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