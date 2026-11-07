// core/data.cpp — parser for Rakshas content files. See data.h for the format.
#include "core/data.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>

#include "core/file.h"

namespace rk::data {
namespace {

std::string_view trim(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
  return s;
}

bool is_key(std::string_view s) {
  if (s.empty()) return false;
  for (char c : s) {
    bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    if (!ok) return false;
  }
  return true;
}

bool parse_float(std::string_view word, float& out) {
  std::string tmp(word);
  if (tmp.empty()) return false;
  char* end = nullptr;
  errno = 0;
  float v = std::strtof(tmp.c_str(), &end);
  if (errno != 0 || end != tmp.c_str() + tmp.size()) return false;
  out = v;
  return true;
}

struct Open {
  int indent;
  Node* node;
};

}  // namespace

const Field* Node::find(std::string_view key) const {
  for (const Field& f : fields)
    if (f.key == key) return &f;
  return nullptr;
}

void Errors::add(const std::string& file, int line, const std::string& message) {
  messages.push_back(file + ", line " + std::to_string(line) + ": " + message);
}

std::string Errors::joined() const {
  std::string out;
  for (const auto& m : messages) {
    if (!out.empty()) out += '\n';
    out += m;
  }
  return out;
}

bool parse(std::string_view text, const std::string& file_name, Node& root, Errors& errors) {
  size_t errors_before = errors.messages.size();
  root = Node{};
  root.file = file_name;
  std::vector<Open> stack{{-1, &root}};

  // Multi-line text state.
  Field* text_field = nullptr;
  int text_key_indent = 0;
  int pending_blank_lines = 0;

  int line_no = 0;
  size_t pos = 0;
  while (pos <= text.size()) {
    size_t end = text.find('\n', pos);
    if (end == std::string_view::npos) end = text.size();
    std::string_view raw = text.substr(pos, end - pos);
    pos = end + 1;
    ++line_no;

    std::string_view body = trim(raw);
    int indent = 0;
    bool tab_in_indent = false;
    for (char c : raw) {
      if (c == ' ') ++indent;
      else if (c == '\t') { tab_in_indent = true; break; }
      else break;
    }

    if (body.empty()) {
      if (text_field) ++pending_blank_lines;
      if (end == text.size()) break;
      continue;
    }

    if (text_field) {
      if (indent > text_key_indent && !tab_in_indent) {
        if (!text_field->value.empty()) {
          text_field->value += '\n';
          for (int i = 0; i < pending_blank_lines; ++i) text_field->value += '\n';
        }
        pending_blank_lines = 0;
        text_field->value += std::string(body);
        if (end == text.size()) break;
        continue;
      }
      text_field = nullptr;
      pending_blank_lines = 0;
    }

    if (body.front() == '#') {
      if (end == text.size()) break;
      continue;
    }
    if (tab_in_indent) {
      errors.add(file_name, line_no, "please indent with spaces, not tabs");
      if (end == text.size()) break;
      continue;
    }

    while (stack.back().indent >= indent) stack.pop_back();
    Node* parent = stack.back().node;

    size_t colon = body.find(':');
    if (colon != std::string_view::npos) {
      std::string_view key = trim(body.substr(0, colon));
      std::string_view value = trim(body.substr(colon + 1));
      if (!is_key(key)) {
        errors.add(file_name, line_no,
                   "'" + std::string(key) +
                       "' is not a valid field name (use lowercase letters, digits and _)");
      } else if (parent->find(key)) {
        errors.add(file_name, line_no, "'" + std::string(key) + "' is set twice in the same block");
      } else {
        parent->fields.push_back({std::string(key), std::string(value), line_no});
        if (value.empty()) {
          text_field = &parent->fields.back();
          text_key_indent = indent;
        }
      }
    } else {
      size_t space = body.find(' ');
      std::string_view type = body.substr(0, space);
      std::string_view name = space == std::string_view::npos ? std::string_view{}
                                                              : trim(body.substr(space + 1));
      if (!is_key(type)) {
        errors.add(file_name, line_no,
                   "expected a block like 'teacher gargi' or a field like 'name: Gargi', got '" +
                       std::string(body) + "'");
      } else {
        Node child;
        child.type = std::string(type);
        child.name = std::string(name);
        child.file = file_name;
        child.line = line_no;
        parent->children.push_back(std::move(child));
        stack.push_back({indent, &parent->children.back()});
      }
    }
    if (end == text.size()) break;
  }
  return errors.messages.size() == errors_before;
}

bool load_file(const std::string& path, Node& root, Errors& errors) {
  std::vector<u8> bytes;
  if (!read_file_bytes(path, bytes)) {
    errors.add(path, 1, "could not open the file (check the name and folder)");
    return false;
  }
  std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  return parse(text, path, root, errors);
}

bool read_number(const Node& node, std::string_view key, float& out, Errors& errors, bool required) {
  return read_numbers(node, key, &out, 1, errors, required);
}

bool read_numbers(const Node& node, std::string_view key, float* out, int count, Errors& errors,
                  bool required) {
  const Field* f = node.find(key);
  if (!f) {
    if (required)
      errors.add(node.file, node.line,
                 "'" + node.type + " " + node.name + "' needs a '" + std::string(key) + "' field");
    return !required;
  }
  std::vector<float> values;
  std::string_view rest = f->value;
  while (!(rest = trim(rest)).empty()) {
    size_t space = rest.find(' ');
    std::string_view word = rest.substr(0, space);
    float v = 0.0f;
    if (!parse_float(word, v)) {
      errors.add(node.file, f->line,
                 "'" + f->key + "' should be " + (count == 1 ? "a number" : std::to_string(count) + " numbers") +
                     ", but '" + std::string(word) + "' is not a number");
      return false;
    }
    values.push_back(v);
    if (space == std::string_view::npos) break;
    rest = rest.substr(space + 1);
  }
  if (static_cast<int>(values.size()) != count) {
    errors.add(node.file, f->line,
               "'" + f->key + "' should have " + std::to_string(count) + " number" +
                   (count == 1 ? "" : "s") + ", found " + std::to_string(values.size()));
    return false;
  }
  std::copy(values.begin(), values.end(), out);
  return true;
}

bool read_text(const Node& node, std::string_view key, std::string& out, Errors& errors,
               bool required) {
  const Field* f = node.find(key);
  if (!f) {
    if (required)
      errors.add(node.file, node.line,
                 "'" + node.type + " " + node.name + "' needs a '" + std::string(key) + "' field");
    return !required;
  }
  out = f->value;
  return true;
}

int edit_distance(std::string_view a, std::string_view b) {
  std::vector<int> prev(b.size() + 1), cur(b.size() + 1);
  for (size_t j = 0; j <= b.size(); ++j) prev[j] = static_cast<int>(j);
  for (size_t i = 1; i <= a.size(); ++i) {
    cur[0] = static_cast<int>(i);
    for (size_t j = 1; j <= b.size(); ++j) {
      int sub = prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1);
      cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, sub});
    }
    std::swap(prev, cur);
  }
  return prev[b.size()];
}

std::string did_you_mean(std::string_view word, std::initializer_list<const char*> known) {
  const char* best = nullptr;
  int best_distance = 3;  // only suggest close matches
  for (const char* k : known) {
    int d = edit_distance(word, k);
    if (d < best_distance) {
      best_distance = d;
      best = k;
    }
  }
  return best ? " (did you mean '" + std::string(best) + "'?)" : std::string();
}

void check_fields(const Node& node, std::initializer_list<const char*> known, Errors& errors) {
  for (const Field& f : node.fields) {
    bool found = false;
    const char* best = nullptr;
    int best_distance = 3;  // only suggest close matches
    for (const char* k : known) {
      if (f.key == k) {
        found = true;
        break;
      }
      int d = edit_distance(f.key, k);
      if (d < best_distance) {
        best_distance = d;
        best = k;
      }
    }
    if (!found) {
      std::string msg = "unknown field '" + f.key + "' in '" + node.type + "'";
      if (best) msg += " (did you mean '" + std::string(best) + "'?)";
      errors.add(node.file, f.line, msg);
    }
  }
}

}  // namespace rk::data
