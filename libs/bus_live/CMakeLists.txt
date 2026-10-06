project(bus_live)

set(SRC
  arrivals.cpp
  arrivals.hpp
  arrivals_service.cpp
  arrivals_service.hpp
  gtfs_csv.cpp
  gtfs_csv.hpp
  gtfs_rt.cpp
  gtfs_rt.hpp
  stop_matcher.cpp
  stop_matcher.hpp
  tmb_parser.cpp
  tmb_parser.hpp
  types.hpp
)

omim_add_library(${PROJECT_NAME} ${SRC})

target_link_libraries(${PROJECT_NAME}
  PUBLIC
    platform
    coding
    geometry
)

omim_add_test_subdirectory(bus_live_tests)
