#include <gtest/gtest.h>

#include <valijson/adapters/rapidjson_adapter.hpp>

#include <valijson/graph/graph.hpp>
#include <valijson/graph/graph_parser.hpp>

using valijson::adapters::RapidJsonAdapter;

using valijson::graph::Graph;
using valijson::graph::GraphParser;

typedef rapidjson::MemoryPoolAllocator<rapidjson::CrtAllocator>
        RapidJsonCrtAllocator;

class TestGraphParser : public testing::Test
{

};

TEST_F(TestGraphParser, GraphParserTestPlaceholder)
{
    rapidjson::Document document;
    document.SetObject();
    RapidJsonAdapter adapter(document);

    Graph graph;
    GraphParser graphParser;

    EXPECT_THROW(graphParser.parse(adapter, graph), std::exception);
}
