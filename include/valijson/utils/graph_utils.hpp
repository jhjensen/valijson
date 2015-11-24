#ifndef __VALIJSON_UTILS_GRAPH_UTILS_HPP
#define __VALIJSON_UTILS_GRAPH_UTILS_HPP

#include <rapidjson/document.h>

#include "../adapters/graph_adapter.hpp"
#include "../graph/graph_parser.hpp"

#include "file_utils.hpp"

namespace valijson {
namespace utils {

inline bool loadDocument(const std::string &path, graph::Graph &graph)
{
    // Load schema JSON from file
    std::string file;
    if (!loadFile(path, file)) {
        std::cerr << "Failed to load json from file '" << path << "'." << std::endl;
        return false;
    }

    // Parse schema
    rapidjson::Document document;
    document.Parse<0>(file.c_str());
    if (document.HasParseError()) {
        std::cerr << "RapidJson failed to parse the document:" << std::endl;
        std::cerr << "Parse error: " << document.GetParseError() << std::endl;
        std::cerr << "Near: " << file.substr((std::max)(size_t(0), document.GetErrorOffset() - 20), 40) << std::endl;
        return false;
    }

    adapters::RapidJsonAdapter rapidJsonAdapter(document);

    graph::GraphParser graphParser;
    graphParser.parse(rapidJsonAdapter, graph);

    return true;
}

}  // namespace utils
}  // namespace valijson

#endif
