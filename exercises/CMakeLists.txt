file(GLOB EXERCISE_SOURCES *.cpp)

set(EXERCISE_TARGETS "")
foreach(src ${EXERCISE_SOURCES})
  get_filename_component(name ${src} NAME_WE)
  add_executable(ex_${name} ${src})
  target_link_libraries(ex_${name} PRIVATE gr_sim_core)
  list(APPEND EXERCISE_TARGETS ex_${name})
endforeach()

add_custom_target(exercises DEPENDS ${EXERCISE_TARGETS})
