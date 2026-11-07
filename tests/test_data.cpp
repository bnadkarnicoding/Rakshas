// tests/test_data.cpp — the content file reader.
#include "core/data.h"
#include "test.h"

using namespace rk;

TEST(data_blocks_fields_and_comments) {
  const char* text =
      "# Teachers of the forest\n"
      "teacher gargi\n"
      "  name: Gargi Vachaknavi\n"
      "  price: Answer one question honestly\n"
      "\n"
      "teacher martial\n"
      "  name: The Akhada Master\n";
  data::Node root;
  data::Errors errors;
  CHECK(data::parse(text, "teachers.rk", root, errors));
  CHECK(errors.ok());
  CHECK(root.children.size() == 2);
  CHECK(root.children[0].type == "teacher");
  CHECK(root.children[0].name == "gargi");
  CHECK(root.children[0].find("name")->value == "Gargi Vachaknavi");
  CHECK(root.children[1].find("name")->value == "The Akhada Master");
  CHECK(root.children[1].line == 6);
}

TEST(data_multiline_text) {
  const char* text =
      "teacher gargi\n"
      "  greeting:\n"
      "    You ask how to win.\n"
      "    I ask what you will do\n"
      "\n"
      "    once you have won.\n"
      "  rival: martial\n";
  data::Node root;
  data::Errors errors;
  CHECK(data::parse(text, "t.rk", root, errors));
  const data::Node& g = root.children.at(0);
  CHECK(g.find("greeting")->value == "You ask how to win.\nI ask what you will do\n\nonce you have won.");
  CHECK(g.find("rival")->value == "martial");
}

TEST(data_nested_blocks) {
  const char* text =
      "terrain\n"
      "  size: 200\n"
      "  hill north\n"
      "    height: 9\n"
      "  hill south\n"
      "    height: 4\n"
      "prop stone\n"
      "  size: 1 2 3\n";
  data::Node root;
  data::Errors errors;
  CHECK(data::parse(text, "r.rk", root, errors));
  CHECK(root.children.size() == 2);
  const data::Node& terrain = root.children[0];
  CHECK(terrain.children.size() == 2);
  float h = 0;
  CHECK(data::read_number(terrain.children[1], "height", h, errors, true));
  CHECK_NEAR(h, 4.0f, 1e-6f);
  float size[3] = {};
  CHECK(data::read_numbers(root.children[1], "size", size, 3, errors, true));
  CHECK_NEAR(size[2], 3.0f, 1e-6f);
  CHECK(errors.ok());
}

TEST(data_reports_readable_errors) {
  data::Node root;
  data::Errors errors;
  data::parse("prop stone\n  size: big\n  yaww: 15\n\tcolor: 1 1 1\n", "r.rk", root, errors);
  CHECK(errors.messages.size() == 1);  // the tab
  CHECK(errors.messages[0].find("line 4") != std::string::npos);

  float size[3];
  data::read_numbers(root.children[0], "size", size, 3, errors);
  CHECK(errors.messages.back().find("'big' is not a number") != std::string::npos);

  data::check_fields(root.children[0], {"size", "yaw", "position"}, errors);
  CHECK(errors.messages.back().find("did you mean 'yaw'") != std::string::npos);
}

TEST(data_duplicate_and_missing_fields) {
  data::Node root;
  data::Errors errors;
  data::parse("prop a\n  yaw: 1\n  yaw: 2\n", "r.rk", root, errors);
  CHECK(errors.messages.size() == 1);
  CHECK(errors.messages[0].find("set twice") != std::string::npos);
  float v = 0;
  CHECK(!data::read_number(root.children[0], "size", v, errors, true));
  CHECK(errors.messages.back().find("needs a 'size' field") != std::string::npos);
}

TEST(data_edit_distance) {
  CHECK(data::edit_distance("teaches", "teachs") == 1);
  CHECK(data::edit_distance("", "abc") == 3);
  CHECK(data::edit_distance("same", "same") == 0);
}
