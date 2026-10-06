project(bike_share_tests)

set(SRC
  availability_service_tests.cpp
  gbfs_parser_tests.cpp
  station_matcher_tests.cpp
)

omim_add_test(${PROJECT_NAME} ${SRC} NO_PLATFORM_INIT)

target_link_libraries(${PROJECT_NAME} PRIVATE bike_share)

target_compile_definitions(${PROJECT_NAME} PRIVATE BIKE_SHARE_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")

set_target_properties(${PROJECT_NAME} PROPERTIES UNITY_BUILD OFF)
