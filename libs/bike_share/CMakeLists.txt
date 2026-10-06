project(bike_share)

set(SRC
  availability_service.cpp
  availability_service.hpp
  feed_catalog.cpp
  feed_catalog.hpp
  gbfs_parser.cpp
  gbfs_parser.hpp
  station_matcher.cpp
  station_matcher.hpp
  types.hpp
)

omim_add_library(${PROJECT_NAME} ${SRC})

target_link_libraries(${PROJECT_NAME}
  PUBLIC
    platform
    coding
    geometry
)

omim_add_test_subdirectory(bike_share_tests)
