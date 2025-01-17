#include <string>
#include <sstream>
#include <iostream>
#include <cstddef>
#include <iomanip>

#include "ast.hpp"
#include "json.hpp"
#include "context.hpp"
#include "position.hpp"
#include "source_map.hpp"

namespace Sass {
  using std::ptrdiff_t;
  SourceMap::SourceMap() : current_position(0, 0, 0), file("stdin") { }
  SourceMap::SourceMap(const string& file) : current_position(0, 0, 0), file(file) { }

  string SourceMap::generate_source_map(Context &ctx) {

    const bool include_sources = ctx.source_map_contents;
    const vector<string> includes = ctx.include_links;
    const vector<char*> sources = ctx.sources;

    JsonNode* json_srcmap = json_mkobject();

    json_append_member(json_srcmap, "version", json_mknumber(3));

    // pass-through sourceRoot option
    if (!ctx.source_map_root.empty()) {
      JsonNode* root = json_mkstring(ctx.source_map_root.c_str());
      json_append_member(json_srcmap, "sourceRoot", root);
    }

    const char *include = file.c_str();
    JsonNode *json_include = json_mkstring(include);
    json_append_member(json_srcmap, "file", json_include);

    JsonNode *json_includes = json_mkarray();
    for (size_t i = 0; i < source_index.size(); ++i) {
      const char *include = includes[source_index[i]].c_str();
      JsonNode *json_include = json_mkstring(include);
      json_append_element(json_includes, json_include);
    }
    json_append_member(json_srcmap, "sources", json_includes);

    JsonNode *json_contents = json_mkarray();
    if (include_sources) {
      for (size_t i = 0; i < source_index.size(); ++i) {
        const char *content = sources[source_index[i]];
        JsonNode *json_content = json_mkstring(content);
        json_append_element(json_contents, json_content);
      }
    }
    json_append_member(json_srcmap, "sourcesContent", json_contents);

    string mappings = serialize_mappings();
    JsonNode *json_mappings = json_mkstring(mappings.c_str());
    json_append_member(json_srcmap, "mappings", json_mappings);

    JsonNode *json_names = json_mkarray();
    // so far we have no implementation for names
    // no problem as we do not alter any identifiers
    json_append_member(json_srcmap, "names", json_names);

    char *str = json_stringify(json_srcmap, "\t");
    string result = string(str);
    free(str);
    json_delete(json_srcmap);
    return result;
  }

  string SourceMap::serialize_mappings() {
    string result = "";

    size_t previous_generated_line = 0;
    size_t previous_generated_column = 0;
    size_t previous_original_line = 0;
    size_t previous_original_column = 0;
    size_t previous_original_file = 0;
    for (size_t i = 0; i < mappings.size(); ++i) {
      const size_t generated_line = mappings[i].generated_position.line;
      const size_t generated_column = mappings[i].generated_position.column;
      const size_t original_line = mappings[i].original_position.line;
      const size_t original_column = mappings[i].original_position.column;
      const size_t original_file = mappings[i].original_position.file;

      if (generated_line != previous_generated_line) {
        previous_generated_column = 0;
        if (generated_line > previous_generated_line) {
          result += std::string(generated_line - previous_generated_line, ';');
          previous_generated_line = generated_line;
        }
      }
      else if (i > 0) {
        result += ",";
      }

      // generated column
      result += base64vlq.encode(static_cast<int>(generated_column) - static_cast<int>(previous_generated_column));
      previous_generated_column = generated_column;
      // file
      result += base64vlq.encode(static_cast<int>(original_file) - static_cast<int>(previous_original_file));
      previous_original_file = original_file;
      // source line
      result += base64vlq.encode(static_cast<int>(original_line) - static_cast<int>(previous_original_line));
      previous_original_line = original_line;
      // source column
      result += base64vlq.encode(static_cast<int>(original_column) - static_cast<int>(previous_original_column));
      previous_original_column = original_column;
    }

    return result;
  }

  void SourceMap::prepend(const OutputBuffer& out)
  {
    Offset size(out.smap.current_position);
    for (Mapping mapping : out.smap.mappings) {
      if (mapping.generated_position.line > size.line) {
        throw(runtime_error("prepend sourcemap has illegal line"));
      }
      if (mapping.generated_position.line == size.line) {
        if (mapping.generated_position.column > size.column) {
          throw(runtime_error("prepend sourcemap has illegal column"));
        }
      }
    }
    // will adjust the offset
    prepend(Offset(out.buffer));
    // now add the new mappings
    VECTOR_UNSHIFT(mappings, out.smap.mappings);
  }

  void SourceMap::append(const OutputBuffer& out)
  {
    append(Offset(out.buffer));
  }

  void SourceMap::prepend(const Offset& offset)
  {
    if (offset.line != 0 || offset.column != 0) {
      for (Mapping& mapping : mappings) {
        // move stuff on the first old line
        if (mapping.generated_position.line == 0) {
          mapping.generated_position.column += offset.column;
        }
        // make place for the new lines
        mapping.generated_position.line += offset.line;
      }
    }
    if (current_position.line == 0) {
      current_position.column += offset.column;
    }
    current_position.line += offset.line;
  }

  void SourceMap::append(const Offset& offset)
  {
    current_position += offset;
  }

  void SourceMap::add_open_mapping(AST_Node* node)
  {
    mappings.push_back(Mapping(node->pstate(), current_position));
  }

  void SourceMap::add_close_mapping(AST_Node* node)
  {
    mappings.push_back(Mapping(node->pstate() + node->pstate().offset, current_position));
  }

  ParserState SourceMap::remap(const ParserState& pstate) {
    for (size_t i = 0; i < mappings.size(); ++i) {
      if (
        mappings[i].generated_position.file == pstate.file &&
        mappings[i].generated_position.line == pstate.line &&
        mappings[i].generated_position.column == pstate.column
      ) return ParserState(pstate.path, pstate.src, mappings[i].original_position, pstate.offset);
    }
    return ParserState(pstate.path, pstate.src, Position(-1, -1, -1), Offset(0, 0));

  }

}
