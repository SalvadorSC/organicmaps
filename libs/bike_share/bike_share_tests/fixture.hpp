#pragma once

#include "testing/testing.hpp"

#include <fstream>
#include <iterator>
#include <string>

inline std::string ReadBikeShareFixture(std::string const & name)
{
  std::string const path = std::string(BIKE_SHARE_TEST_DATA_DIR) + "/" + name;
  std::ifstream input(path);
  TEST(static_cast<bool>(input), ("Cannot open", path));
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}
