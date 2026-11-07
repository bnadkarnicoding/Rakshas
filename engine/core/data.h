// core/data.h — reader for Rakshas content files (.rk).
//
// The format is designed to be written by people, not programs:
//
//   # A comment line starts with '#'.
//   teacher gargi                 <- a block: a type word, then an optional name
//     name: Gargi Vachaknavi      <- a field: key, colon, value
//     price: Answer one question honestly
//     greeting:                   <- empty value = multi-line text follows,
//       You ask how to win.          indented further than the key
//       I ask what you will do
//       once you have won.
//
// Blocks nest by indentation (spaces only). Errors name the file and line in
// plain words, and unknown keys get a "did you mean ...?" suggestion.
#pragma once
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace rk::data {

struct Field {
  std::string key;
  std::string value;
  int line = 0;
};

struct Node {
  std::string type;   // e.g. "teacher" (empty for the file's root)
  std::string name;   // e.g. "gargi" (may be empty)
  std::string file;
  int line = 0;
  std::vector<Field> fields;
  std::vector<Node> children;

  const Field* find(std::string_view key) const;
};

// Collects problems found while reading content, for showing to people.
struct Errors {
  std::vector<std::string> messages;
  bool ok() const { return messages.empty(); }
  void add(const std::string& file, int line, const std::string& message);
  std::string joined() const;
};

// Parses text into `root` (whose children are the top-level blocks).
bool parse(std::string_view text, const std::string& file_name, Node& root, Errors& errors);

// Reads and parses a file from disk.
bool load_file(const std::string& path, Node& root, Errors& errors);

// --- Typed field access (each reports a readable error on bad input) --------

// Reads a single number. Missing + not required -> leaves `out` unchanged.
bool read_number(const Node& node, std::string_view key, float& out, Errors& errors,
                 bool required = false);

// Reads exactly `count` space-separated numbers, e.g. "position: 3 -4".
bool read_numbers(const Node& node, std::string_view key, float* out, int count, Errors& errors,
                  bool required = false);

// Reads text (single- or multi-line).
bool read_text(const Node& node, std::string_view key, std::string& out, Errors& errors,
               bool required = false);

// Reports any field not in `known`, suggesting the closest known key.
void check_fields(const Node& node, std::initializer_list<const char*> known, Errors& errors);

// Returns " (did you mean 'x'?)" for the closest known word, or "" if none is close.
std::string did_you_mean(std::string_view word, std::initializer_list<const char*> known);

// Edit distance between two words (used for "did you mean" suggestions).
int edit_distance(std::string_view a, std::string_view b);

}  // namespace rk::data
