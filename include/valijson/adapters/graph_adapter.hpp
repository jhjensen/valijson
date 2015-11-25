#ifndef __VALIJSON_ADAPTERS_JSON_GRAPH_ADAPTER_HPP
#define __VALIJSON_ADAPTERS_JSON_GRAPH_ADAPTER_HPP

#include <string>
#include <boost/bind.hpp>
#include <boost/iterator/iterator_facade.hpp>

#include <valijson/adapters/adapter.hpp>
#include <valijson/adapters/basic_adapter.hpp>
#include <valijson/adapters/frozen_value.hpp>

#include "../graph/graph.hpp"

namespace valijson {
namespace adapters {

class JsonGraphAdapter;
class JsonGraphArrayValueIterator;
class JsonGraphObjectMemberIterator;

typedef std::pair<std::string, JsonGraphAdapter> JsonGraphObjectMember;

class JsonGraphArray
{
public:
    typedef JsonGraphArrayValueIterator const_iterator;
    typedef JsonGraphArrayValueIterator iterator;

    JsonGraphArray()
      : value(emptyArray()) { }

    JsonGraphArray(const graph::GraphNode::Array &value)
      : value(value) { }

    JsonGraphArrayValueIterator begin() const;

    JsonGraphArrayValueIterator end() const;

    size_t size() const
    {
        return value.size();
    }

private:

    static const graph::GraphNode::Array & emptyArray()
    {
        static const graph::GraphNode::Array arr;
        return arr;
    }

    const graph::GraphNode::Array value;

};

class JsonGraphObject
{
public:

    typedef JsonGraphObjectMemberIterator const_iterator;
    typedef JsonGraphObjectMemberIterator iterator;

    JsonGraphObject()
      : value(emptyObject()) { }

    JsonGraphObject(const graph::GraphNode::Object &value)
      : value(value) { }

    JsonGraphObjectMemberIterator begin() const;

    JsonGraphObjectMemberIterator end() const;

    JsonGraphObjectMemberIterator find(const std::string &propertyName) const;

    size_t size() const
    {
        return value.size();
    }

private:

    static const graph::GraphNode::Object & emptyObject()
    {
        static const graph::GraphNode::Object obj;
        return obj;
    }

    const graph::GraphNode::Object value;
};

class JsonGraphFrozenValue: public FrozenValue
{
public:
    JsonGraphFrozenValue(const graph::GraphNode &source)
      : value(source)
    {

    }

    virtual FrozenValue * clone() const
    {
        return new JsonGraphFrozenValue(value);
    }

    virtual bool equalTo(const Adapter &other, bool strict) const;

private:

    const graph::GraphNode value;
};

class JsonGraphValue
{
public:
    JsonGraphValue()
      : value(emptyObject()) { }

    JsonGraphValue(boost::shared_ptr<const graph::GraphNode> value)
      : value(value) { }

    FrozenValue * freeze() const
    {
        return new JsonGraphFrozenValue(*value);
    }

    boost::optional<JsonGraphArray> getArrayOptional() const
    {
        const boost::optional<graph::GraphNode::Array> arr =
                value->resolveToArray();
        if (arr) {
            return JsonGraphArray(*arr);
        }

        return boost::none;
    }

    bool getArraySize(size_t &result) const
    {
        return value->sizeOfResolvedArray(result);
    }

    bool getBool(bool &result) const
    {
        return value->resolveToBool(result);
    }

    bool getDouble(double &result) const
    {
        return value->resolveToDouble(result);
    }

    bool getInteger(int64_t &result) const
    {
        return value->resolveToInteger(result);
    }

    boost::optional<JsonGraphObject> getObjectOptional() const
    {
        const boost::optional<graph::GraphNode::Object> object =
                value->resolveToObject();
        if (object) {
            return JsonGraphObject(*object);
        }

        return boost::none;
    }

    bool getObjectSize(size_t &result) const
    {
        return value->sizeOfResolvedObject(result);
    }

    bool getString(std::string &result) const
    {
        return value->resolveToString(result);
    }

    static bool hasStrictTypes()
    {
        return true;
    }

    bool isArray() const
    {
        return value->resolvesToArray();
    }

    bool isBool() const
    {
        return value->resolvesToBool();
    }

    bool isDouble() const
    {
        return value->resolvesToDouble();
    }

    bool isInteger() const
    {
        return value->resolvesToInteger();
    }

    bool isNull() const
    {
        return value->isEmpty();
    }

    bool isNumber() const
    {
        return value->resolvesToDouble() || value->resolvesToInteger();
    }

    bool isObject() const
    {
        return value->resolvesToObject();
    }

    bool isString() const
    {
        return value->resolvesToString();
    }

private:

    static boost::shared_ptr<const graph::GraphNode> emptyObject()
    {
        static boost::shared_ptr<graph::GraphNode> object =
                boost::make_shared<graph::GraphNode>(
                        graph::GraphNode::Object());
        return object;
    }

    boost::shared_ptr<const graph::GraphNode> value;
};

class JsonGraphAdapter:
    public BasicAdapter<JsonGraphAdapter,
                        JsonGraphArray,
                        JsonGraphObjectMember,
                        JsonGraphObject,
                        JsonGraphValue>
{
public:
    JsonGraphAdapter()
      : BasicAdapter() { }

    JsonGraphAdapter(graph::Graph &graph)
      : BasicAdapter(boost::shared_ptr<const graph::GraphNode>(graph.getRootNode())) { }

    JsonGraphAdapter(boost::shared_ptr<const graph::GraphNode> node)
      : BasicAdapter(node) { }
};

/**
 * @brief   Class for iterating over values held in a JSON array.
 *
 * This class provides a JSON array iterator that dereferences as an instance of
 * JsonGraphAdapter representing a value stored in the array. It has been
 * implemented using the boost iterator_facade template.
 *
 * @see JsonGraphArray
 */
class JsonGraphArrayValueIterator:
    public boost::iterator_facade<
        JsonGraphArrayValueIterator,           // name of derived type
        JsonGraphAdapter,                      // value type
        boost::bidirectional_traversal_tag,  // bi-directional iterator
        JsonGraphAdapter>                      // type returned when dereferenced
{
public:

    /**
     * @brief   Construct a new JsonGraphArrayValueIterator using an existing
     *          JsonGraph iterator.
     *
     * @param   itr  JsonGraph iterator to store
     */
    JsonGraphArrayValueIterator(const graph::GraphNode::Array::const_iterator &itr)
      : itr(itr) { }

    /// Returns a JsonGraphAdapter that contains the value of the current element.
    JsonGraphAdapter dereference() const
    {
        return JsonGraphAdapter(*itr);
    }

    /**
     * @brief   Compare this iterator against another iterator.
     *
     * Note that this directly compares the iterators, not the underlying
     * values, and assumes that two identical iterators will point to the same
     * underlying object.
     *
     * @param   rhs  iterator to compare against
     *
     * @returns true if the iterators are equal, false otherwise.
     */
    bool equal(const JsonGraphArrayValueIterator &rhs) const
    {
        return itr == rhs.itr;
    }

    void increment()
    {
        itr++;
    }

    void decrement()
    {
        itr--;
    }

    void advance(std::ptrdiff_t n)
    {
        if (n > 0) {
            while (n-- > 0) {
                itr++;
            }
        } else {
            while (n++ < 0) {
                itr--;
            }
        }
    }

private:

    graph::GraphNode::Array::const_iterator itr;
};

/**
 * @brief   Class for iterating over the members belonging to a JSON object.
 *
 * This class provides a JSON object iterator that dereferences as an instance
 * of JsonGraphObjectMember representing one of the members of the object. It has
 * been implemented using the boost iterator_facade template.
 *
 * @see JsonGraphObject
 * @see JsonGraphObjectMember
 */
class JsonGraphObjectMemberIterator:
    public boost::iterator_facade<
        JsonGraphObjectMemberIterator,         // name of derived type
        JsonGraphObjectMember,                 // value type
        boost::bidirectional_traversal_tag,  // bi-directional iterator
        JsonGraphObjectMember>                 // type returned when dereferenced
{
public:

    /**
     * @brief   Construct an iterator from a JsonGraph iterator.
     *
     * @param   itr  JsonGraph iterator to store
     */
    JsonGraphObjectMemberIterator(const graph::GraphNode::Object::const_iterator &itr)
      : itr(itr) { }

    /**
     * @brief   Returns a JsonGraphObjectMember that contains the key and value
     *          belonging to the object member identified by the iterator.
     */
    JsonGraphObjectMember dereference() const
    {
        return JsonGraphObjectMember(itr->first, JsonGraphAdapter(itr->second));
    }

    /**
     * @brief   Compare this iterator with another iterator.
     *
     * Note that this directly compares the iterators, not the underlying
     * values, and assumes that two identical iterators will point to the same
     * underlying object.
     *
     * @param   rhs  Iterator to compare with
     *
     * @returns true if the underlying iterators are equal, false otherwise
     */
    bool equal(const JsonGraphObjectMemberIterator &rhs) const
    {
        return itr == rhs.itr;
    }

    void increment()
    {
        itr++;
    }

    void decrement()
    {
        itr--;
    }

private:

    /// Iternal copy of the original JsonGraph iterator
    graph::GraphNode::Object::const_iterator itr;
};

/// Specialisation of the AdapterTraits template struct for JsonGraphAdapter.
template<>
struct AdapterTraits<valijson::adapters::JsonGraphAdapter>
{
    typedef graph::Graph DocumentType;

    static std::string adapterName()
    {
        return "JsonGraphAdapter";
    }
};

inline bool JsonGraphFrozenValue::equalTo(const Adapter &other, bool strict) const
{
    throw std::runtime_error("Not implemented");
}

inline JsonGraphArrayValueIterator JsonGraphArray::begin() const
{
    return value.begin();
}

inline JsonGraphArrayValueIterator JsonGraphArray::end() const
{
    return value.end();
}

inline JsonGraphObjectMemberIterator JsonGraphObject::begin() const
{
    return value.begin();
}

inline JsonGraphObjectMemberIterator JsonGraphObject::end() const
{
    return value.end();
}

inline JsonGraphObjectMemberIterator JsonGraphObject::find(
    const std::string &propertyName) const
{
    return value.find(propertyName);
}

}  // namespace adapters
}  // namespace valijson

#endif
