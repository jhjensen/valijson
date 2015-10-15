#ifndef __VALIJSON_GRAPH_GRAPH_PARSER_HPP
#define __VALIJSON_GRAPH_GRAPH_PARSER_HPP

#include <stdexcept>

namespace valijson {
namespace graph {

class GraphParser
{
public:
    template<typename AdapterType>
    void parse(const AdapterType &root, Graph &graph)
    {
        throw std::runtime_error("Not implemented");
    }
};

} // namespace graph
} // namespace valijson

#endif
