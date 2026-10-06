project(metro_live_tests)

set(SRC
  metro_tests.cpp
)

omim_add_test(${PROJECT_NAME} ${SRC} NO_PLATFORM_INIT)

target_link_libraries(${PROJECT_NAME} PRIVATE metro_live)

set_target_properties(${PROJECT_NAME} PROPERTIES UNITY_BUILD OFF)
