#ifndef __TREE_H__
#define __TREE_H__

#include <sstream>
#include <vector>
#include <iterator>

#include "file_panel.h"
#include "spdlog/spdlog.h"

struct Tree {
Tree(const std::string &filename, const std::string &path, uint32_t modified)
    : name(filename)
    , full_path(path)
    , date_modified(modified)
    , has_metadata(false)
    , parent(this) {
    // spdlog::debug("creating new node {}, {}", name, path);
  }

  ~Tree() {
    children.clear();
    // delete file_panel;
  }

  bool is_leaf() const {
    // this is just wrong, a folder without files is a still a
    // folder. lucky the files.list endpoint doesn't return empty
    // folders
    return children.empty();
  }
  
  Tree& find_or_create(const std::string &value, const std::string &path, uint32_t modified) {
    if (modified > date_modified) {
      date_modified = modified;
    }

    const auto &entry = children.find(value);
    if (entry != children.cend()) {
      return entry->second;
    }

    children.insert({value, Tree(value, path, modified)});
    Tree &child = children.find(value)->second;
    child.parent = this;
    return child;
  }

  void add_path(const std::vector<std::string>& paths, const std::string &path, uint32_t modified) {
    if (paths.empty()) {
      return;
    }

    Tree *cur_node = this;
    std::string cur_path;
    for (const auto &p : paths) {
      if (cur_path.length() > 0) {
	cur_path = fmt::format("{}/{}", cur_path, p);
      } else {
	cur_path = p;
      }

      cur_node = &(cur_node->find_or_create(p, cur_path, modified));
    }
  }

  Tree *find_path(const std::vector<std::string>& paths) {
    if (paths.empty()) {
      return this;
    }

    Tree *cur_node = this;
    for (const auto &p : paths) {
      const auto &entry = cur_node->children.find(p);
      if (entry != cur_node->children.cend()) {
	cur_node = &entry->second;
      } else {
	return this;
      }
    }
    return cur_node->is_leaf() ? this : cur_node;
  }
  

  Tree *get_child(const std::string child) {
    const auto &e = children.find(child);
    if (e != children.cend()) {
      // spdlog::debug("get_child {}, result = {}", child, e->second.name);
      return &e->second;
    }
    
    return NULL;
  }

  void set_name(const std::string &n) {
    name = n;
  }

  void traverse() const {
    // spdlog::debug("%s%s", name, is_leaf() ? " *": "");
    if (!is_leaf()) {
      for (auto e = children.cbegin(); e != children.cend(); ++e) {
	e->second.traverse();
      }
    }
  }

  bool contains_metadata() {
    return has_metadata;
  }

  void set_metadata(json &j) {
    has_metadata = true;
    metadata = j;
  }

  void clear() {
    children.clear();
  }

  const char* get_thumbpath() {
    if (metadata.contains("result") && metadata["result"].contains("thumbnails")) {
      // XXX: fix my index
      return metadata["result"]["thumbnails"][1]["relative_path"].template get<std::string>().c_str();
    }
    return NULL;
  }
  
  std::string name;
  std::string full_path;
  uint32_t date_modified;
  json metadata;
  bool has_metadata;
  Tree *parent;
  std::map<std::string, Tree> children;
};

#endif // __TREE_H__
