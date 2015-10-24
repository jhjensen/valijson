#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>

#include <gtest/gtest.h>

#include <valijson/adapters/graph_adapter.hpp>
#include <valijson/graph/graph.hpp>

class TestGraphAdapter : public testing::Test
{

};

TEST_F(TestGraphAdapter, BasicArrayIteration)
{
    const unsigned int numElements = 10;

    valijson::graph::GraphNode::Array arr;
    for (unsigned int i = 0; i < numElements; i++) {
        arr.push_back(boost::make_shared<valijson::graph::GraphNode>(
                boost::lexical_cast<std::string>(i)));
    }

    boost::shared_ptr<valijson::graph::GraphNode> graphNode =
            boost::make_shared<valijson::graph::GraphNode>(arr);
    valijson::adapters::JsonGraphAdapter adapter(graphNode);

    // Ensure that wrapping the document preserves the array and does not allow
    // it to be cast to other types
    ASSERT_NO_THROW( adapter.getArray() );
    ASSERT_ANY_THROW( adapter.getBool() );
    ASSERT_ANY_THROW( adapter.getDouble() );
    ASSERT_ANY_THROW( adapter.getObject() );
    ASSERT_ANY_THROW( adapter.getString() );

    // Ensure that the array contains the expected number of elements
    EXPECT_EQ( numElements, adapter.getArray().size() );

    // Ensure that the elements are returned in the order they were inserted
    unsigned int expectedValue = 0;
    BOOST_FOREACH( const valijson::adapters::JsonGraphAdapter value, adapter.getArray() ) {
        ASSERT_TRUE( value.isString() );
        EXPECT_STREQ( boost::lexical_cast<std::string>(expectedValue).c_str(), value.getString().c_str() );
        expectedValue++;
    }

    // Ensure that the correct number of elements were iterated over
    EXPECT_EQ(numElements, expectedValue);
}

TEST_F(TestGraphAdapter, BasicObjectIteration)
{
//    const unsigned int numElements = 10;
//
//    // Create a rapidjson document that consists of an object that maps numeric
//    // strings their corresponding numeric values
//    Json::Value document(Json::objectValue);
//    for (unsigned int i = 0; i < numElements; i++) {
//        std::string name(boost::lexical_cast<std::string>(i));
//        document[name] = Json::Value(double(i));
//    }
//
//    // Ensure that wrapping the document preserves the object and does not
//    // allow it to be cast to other types
//    valijson::adapters::JsonCppAdapter adapter(document);
//    ASSERT_NO_THROW( adapter.getObject() );
//    ASSERT_ANY_THROW( adapter.getArray() );
//    ASSERT_ANY_THROW( adapter.getBool() );
//    ASSERT_ANY_THROW( adapter.getDouble() );
//    ASSERT_ANY_THROW( adapter.getString() );
//
//    // Ensure that the object contains the expected number of members
//    EXPECT_EQ( numElements, adapter.getObject().size() );
//
//    // Ensure that the members are returned in the order they were inserted
//    unsigned int expectedValue = 0;
//    BOOST_FOREACH( const valijson::adapters::JsonCppAdapter::ObjectMember member, adapter.getObject() ) {
//        ASSERT_TRUE( member.second.isNumber() );
//        EXPECT_EQ( boost::lexical_cast<std::string>(expectedValue), member.first );
//        EXPECT_EQ( double(expectedValue), member.second.getDouble() );
//        expectedValue++;
//    }
//
//    // Ensure that the correct number of elements were iterated over
//    EXPECT_EQ( numElements, expectedValue );
}
