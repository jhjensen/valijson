#ifndef __VALIJSON_GRAPH_GRAPH_HPP
#define __VALIJSON_GRAPH_GRAPH_HPP

#include <map>
#include <string>
#include <vector>

#include <boost/make_shared.hpp>
#include <boost/optional.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/variant.hpp>
#include <boost/weak_ptr.hpp>

namespace valijson {
namespace graph {

class GraphNode
{
public:
    typedef std::string String;
    typedef std::vector<boost::shared_ptr<GraphNode> > Array;
    typedef std::map<std::string, boost::shared_ptr<GraphNode> > Object;
    typedef boost::weak_ptr<GraphNode> Reference;

    GraphNode()
    {

    };

    bool containsArray() const
    {
        return contains<Array>();
    }

    bool containsObject() const
    {
        return contains<Object>();
    }

    bool containsString() const
    {
        return contains<String>();
    }

    bool isEmpty() const
    {
        return !value;
    }

    void reset()
    {
        value = boost::none;
    }

    void setArray(const Array &value)
    {
        this->value = value;
    }

    void setObject(const Object &value)
    {
        this->value = value;
    }

    void setString(const String &value)
    {
        this->value = value;
    }

private:
    typedef boost::variant<Array, Object, Reference, String> GraphValue;

    template<typename ValueType>
    bool contains() const
    {
        if (!value) {
            return false;
        }

        // Check for reference nodes
        const Reference *r = boost::get<Reference>(&(*value));
        if (r) {
            boost::shared_ptr<const GraphNode> rs = r->lock();
            if (rs) {
                return rs->contains<ValueType>();
            } else {
                throw std::runtime_error("Referenced node no longer exists.");
            }
        }

        return boost::get<ValueType>(&(*value));
    }

    boost::optional<GraphValue> value;
};

class Graph
{
public:
    Graph()
      : root(boost::make_shared<GraphNode>())
    {

    }

    boost::shared_ptr<GraphNode> getRootNode()
    {
        return root;
    }

private:
    boost::shared_ptr<GraphNode> root;
};

} // namespace graph
} // namespace valijson

#endif
