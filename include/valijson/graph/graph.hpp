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

    GraphNode(const Array &value)
      : value(value)
    {

    }

    GraphNode(bool value)
      : value(value)
    {

    }

    GraphNode(double value)
      : value(value)
    {

    }

    GraphNode(int64_t value)
      : value(value)
    {

    }

    GraphNode(const Object &value)
      : value(value)
    {

    }

    GraphNode(const Reference &value)
      : value(value)
    {

    }

    GraphNode(const String &value)
      : value(value)
    {

    }

    bool isEmpty() const
    {
        return !value;
    }

    bool isReference() const
    {
        if (!value) {
            return false;
        }

        return boost::get<Reference>(&(*value));
    }

    void reset()
    {
        value = boost::none;
    }

    bool resolvesToArray() const
    {
        return resolvesTo<Array>();
    }

    bool resolvesToBool() const
    {
        return resolvesTo<bool>();
    }

    bool resolvesToInteger() const
    {
        return resolvesTo<int64_t>();
    }

    bool resolvesToObject() const
    {
        return resolvesTo<Object>();
    }

    bool resolvesToString() const
    {
        return resolvesTo<String>();
    }

    boost::optional<Array> resolveToArray() const
    {
        return resolveTo<Array>();
    }

    bool resolveToArray(Array &value) const
    {
        return resolveTo(value);
    }

    boost::optional<bool> resolveToBool() const
    {
        return resolveTo<bool>();
    }

    bool resolveToBool(bool &value) const
    {
        return resolveTo(value);
    }

    boost::optional<double> resolveToDouble() const
    {
        return resolveTo<double>();
    }

    bool resolveToDouble(double &value) const
    {
        return resolveTo(value);
    }

    boost::optional<int64_t> resolveToInteger() const
    {
        return resolveTo<int64_t>();
    }

    bool resolveToInteger(int64_t &value) const
    {
        return resolveTo(value);
    }

    boost::optional<Object> resolveToObject() const
    {
        return resolveTo<Object>();
    }

    bool resolveToObject(Object &value) const
    {
        return resolveTo(value);
    }

    boost::optional<String> resolveToString() const
    {
        return resolveTo<String>();
    }

    bool resolveToString(String &value) const
    {
        return resolveTo(value);
    }

    void setArray(const Array &value)
    {
        this->value = value;
    }

    void setBool(bool value)
    {
        this->value = value;
    }

    void setDouble(double value)
    {
        this->value = value;
    }

    void setInteger(int64_t value)
    {
        this->value = value;
    }

    void setObject(const Object &value)
    {
        this->value = value;
    }

    void setReference(const Reference &reference)
    {
        this->value = value;
    }

    void setString(const String &value)
    {
        this->value = value;
    }

    bool sizeOfResolvedArray(size_t &result) const
    {
        if (!value) {
            return false;
        }

        const Array *a = boost::get<Array>(&(*value));
        if (a) {
            return a->size();
        }

        const Reference *r = boost::get<Reference>(&(*value));
        if (r) {
            boost::shared_ptr<const GraphNode> rs = r->lock();
            if (rs) {
                return rs->sizeOfResolvedArray(result);
            } else {
                throw std::runtime_error("Referenced node no longer exists");
            }
        }

        return false;
    }

    bool sizeOfResolvedObject(size_t &result) const
    {
        if (!value) {
            return false;
        }

        const Object *o = boost::get<Object>(&(*value));
        if (o) {
            return o->size();
        }

        const Reference *r = boost::get<Reference>(&(*value));
        if (r) {
            boost::shared_ptr<const GraphNode> rs = r->lock();
            if (rs) {
                return rs->sizeOfResolvedObject(result);
            } else {
                throw std::runtime_error("Referenced node no longer exists");
            }
        }

        return false;
    }

private:
    typedef boost::variant<
            Array, bool, double, int64_t, Object, Reference, String> GraphValue;

    template<typename ValueType>
    bool resolvesTo() const
    {
        if (!value) {
            return false;
        }

        // Check for reference nodes
        const Reference *r = boost::get<Reference>(&(*value));
        if (r) {
            boost::shared_ptr<const GraphNode> rs = r->lock();
            if (rs) {
                return rs->resolvesTo<ValueType>();
            } else {
                throw std::runtime_error("Referenced node no longer exists.");
            }
        }

        return boost::get<ValueType>(&(*value));
    }

    template<typename ValueType>
    boost::optional<ValueType> resolveTo() const
    {
        if (value) {
            const ValueType *v = boost::get<ValueType>(&(*value));
            if (v) {
                return *v;
            }

            const Reference *r = boost::get<Reference>(&(*value));
            if (r) {
                boost::shared_ptr<const GraphNode> rs = r->lock();
                if (rs) {
                    return rs->resolveTo<ValueType>();
                } else {
                    throw std::runtime_error(
                            "Referenced node no longer exists");
                }
            }
        }

        return boost::none;
    }

    template<typename ValueType>
    bool resolveTo(ValueType &valueType) const
    {
        if (value) {
            const ValueType *v = boost::get<ValueType>(&(*value));
            if (v) {
                valueType = *v;
                return true;
            }

            const Reference *r = boost::get<Reference>(&(*value));
            if (r) {
                boost::shared_ptr<const GraphNode> rs = r->lock();
                if (rs) {
                    return rs->resolveTo(valueType);
                } else {
                    throw std::runtime_error(
                            "Referenced node no longer exists");
                }
            }
        }

        return false;
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
