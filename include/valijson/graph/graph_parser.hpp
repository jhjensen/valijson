#ifndef __VALIJSON_GRAPH_GRAPH_PARSER_HPP
#define __VALIJSON_GRAPH_GRAPH_PARSER_HPP

#include <boost/weak_ptr.hpp>

#include <map>
#include <stdexcept>
#include <string>

#include "../internal/json_pointer.hpp"
#include "../internal/json_reference.hpp"

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

private:
    typedef std::map<std::string, boost::weak_ptr<GraphNode> > ResolvedNodes;

    /**
     * @brief  Parse a JSON node and resolve JSON References when present
     *
     * This function parses a JSON node and if it discovers a JSON Reference,
     * will locate the relevant JSON document, resolve the reference, and
     * return a new graph node.
     *
     * A JSON Reference consists of an object with a string-valued $ref
     * property that identifies the referenced document fragment, and in our
     * final graph, we want the JSON Reference object to be replaced by a
     * pointer to the referenced fragment.
     *
     * Circular references are supported, but only when there is at least one
     * concrete JSON value between two JSON References that form a loop. For
     * example:
     *
     * {
     *   "a": {
     *     "b" : {
     *       "$ref": "#/a"
     *     }
     * }
     *
     * This kind of circular reference is supported, because we can create a
     * graph node for the value of property 'a', and when parsing the value of
     * property of 'b', we return the same graph node.
     *
     * An example of a circular reference that is *not* supported is:
     *
     * {
     *   "a": {
     *     "$ref": "#/a"
     *   }
     * }
     *
     * Or more generally,
     *
     * {
     *   "a": {
     *     "$ref": "#/b"
     *   },
     *   "b": {
     *     "$ref": "#/a"
     *   }
     * }
     *
     * Although these circular references can be detected, it is impossible to
     * determine the kind of node(s) that should be created to anchor the
     * JSON References. One solution would be to allow JSON References as a 
     * native value type, but this requires that third-party JSON Parsers be
     * aware of JSON References. So we just throw an exception instead.
     *
     * @param  rootNode       Root of the current JSON document
     * @param  startNode      Node that is currently being parsed
     * @param  path           Canonical path to the current node; for lookups
     * @param  resolvedNodes  A map from canonical paths to graph nodes that
     *                        have already been resolved
     */
    template<typename AdapterType>
    boost::shared_ptr<GraphNode> parseNode(
        const AdapterType &rootNode,
        const AdapterType &startNode,
        const std::string &path,
        ResolvedNodes &resolvedNodes)
    {
        typedef std::set<std::string> UntypedReferences;

        ResolvedNodes::const_iterator itr = resolvedNodes.find(path);
        if (itr != resolvedNodes.end()) {
            return boost::shared_ptr<GraphNode>(itr->second);
        }

        // Track the JSON References that were visited before finding a
        // concrete JSON value, so that they can all be added to the set of
        // resolved nodes
        UntypedReferences untypedReferences;

        const AdapterType *currentRoot = &rootNode;
        const AdapterType *currentNode = &startNode;

        std::string currentPath = path;

        while (const boost::optional<std::string> jsonReference =
            parseJsonReference(*currentNode)) {

            // Update effective path using JSON Pointer
            currentPath = internal::json_reference::getJsonReferencePointer(
                *jsonReference);

            // TODO: Determine root based on JSON Reference URI
            currentRoot = &rootNode;

            // Check for previously resolved nodes with the same path
            itr = resolvedNodes.find(currentPath);
            if (resolvedNodes.end() != itr) {
                // Assign previously resolved node to the paths seen while
                // traversing this chain of references
                BOOST_FOREACH ( const std::string &s, untypedReferences ) {
                    resolvedNodes.insert(
                        ResolvedNodes::value_type(s, itr->second));
                }

                // Return a reference node
                return boost::make_shared<GraphNode>(itr->second);
            }

            // Add this path to a list of references that have not previously
            // been resolved to a concrete JSON value
            untypedReferences.insert(currentPath);

            // Find the referenced document node and repeat
            currentNode = internal::json_pointer::resolveJsonPointer(
                *currentNode, currentPath);
        }

        // Create a placeholder graph node and insert it into the set of
        // resolved nodes so that circular references can be handled gracefully
        boost::shared_ptr<GraphNode> newGraphNode =
            boost::make_shared<GraphNode>();
        resolvedNodes.insert(
            ResolvedNodes::value_type(currentPath, newGraphNode));

        parseNodeInto(*newGraphNode, *currentRoot, *currentNode, currentPath,
            resolvedNodes);

        // Assign the new node to the chain of references seen while resolving
        // and parsing the current graph node
        BOOST_FOREACH ( const std::string &s, untypedReferences ) {
            resolvedNodes.insert(ResolvedNodes::value_type(s, newGraphNode));
        }

        return newGraphNode;
    }

    template<typename AdapterType>
    void parseNodeInto(
        GraphNode &graphNode,
        const AdapterType &rootNode,
        const AdapterType &currentNode,
        const std::string &path,
        ResolvedNodes &resolvedNodes)
    {
        typedef typename AdapterType::ObjectMember ObjectMember;

        if (currentNode.isObject()) {
            BOOST_FOREACH( const ObjectMember m, currentNode.getObject() ) {
                const std::string &propertyName = m.first;
                const std::string newPath = path + "/" + propertyName;

                boost::weak_ptr<GraphNode> newNode = parseNode(rootNode,
                    m.second, newPath, resolvedNodes);
            }

        } else if (currentNode.isArray()) {
            unsigned int currentIndex = 0;
            BOOST_FOREACH( const AdapterType a, currentNode.getArray()) {
                const std::string newPath = path + "/" +
                    boost::lexical_cast<std::string>(currentIndex);

                boost::weak_ptr<GraphNode> newNode = parseNode(rootNode,
                    a, newPath, resolvedNodes);

                currentIndex++;
            }

        }
    }

    /**
     * @brief   Parse a JSON node and return a JSON Reference if present
     *
     * @param   node  Node to parse
     *
     * @return  optional string, set only if a JSON Reference is found
     */
    template<typename AdapterType>
    boost::optional<std::string> parseJsonReference(
        const AdapterType &node)
    {
        if (node.maybeObject()) {
            const typename AdapterType::Object object = node->asObject();
            const typename AdapterType::Object::const_iterator itr =
                object.find("$ref");
            if (itr != object.end()) {
                if (itr->second.isString()) {
                    return itr->second.asString();
                } else {
                    throw std::runtime_error(
                        "Found '$ref' property but it did not have a valid "
                        "string value.");
                }
            }
        }

        return boost::none;
    }

};

} // namespace graph
} // namespace valijson

#endif
