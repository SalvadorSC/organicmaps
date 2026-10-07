project(commuter_live)

set(SRC
  bearing.cpp
  bearing.hpp
  names.cpp
  names.hpp
  parser.cpp
  parser.hpp
  fgc_scheme_data.cpp
  scheme_data.cpp
  scheme_data.hpp
  service.cpp
  service.hpp
  snap.cpp
  snap.hpp
  stations.cpp
  types.hpp
)

omim_add_library(${PROJECT_NAME} ${SRC})

target_link_libraries(${PROJECT_NAME}
  PUBLIC
    platform
    coding
    geometry
)

# Anonymous helpers in the parsers share a unity batch and would collide.
set_target_properties(${PROJECT_NAME} PROPERTIES UNITY_BUILD OFF)

omim_add_test_subdirectory(commuter_live_tests)
