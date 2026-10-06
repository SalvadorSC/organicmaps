project(bus_live_tests)

set(SRC
  arrivals_tests.cpp
  gtfs_tests.cpp
  live_feed_tests.cpp
  stop_matcher_tests.cpp
  tmb_parser_tests.cpp
)

omim_add_test(${PROJECT_NAME} ${SRC} NO_PLATFORM_INIT)

target_link_libraries(${PROJECT_NAME} PRIVATE bus_live)

target_compile_definitions(${PROJECT_NAME} PRIVATE BUS_LIVE_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")

set_target_properties(${PROJECT_NAME} PROPERTIES UNITY_BUILD OFF)
