project(metro_live)

set(SRC
  estimator.cpp
  estimator.hpp
  parser.cpp
  parser.hpp
  service.cpp
  service.hpp
  types.hpp
)

omim_add_library(${PROJECT_NAME} ${SRC})

target_link_libraries(${PROJECT_NAME}
  PUBLIC
    platform
    coding
    geometry
)

omim_add_test_subdirectory(metro_live_tests)
