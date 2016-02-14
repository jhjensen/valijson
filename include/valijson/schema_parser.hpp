#pragma once
#ifndef __VALIJSON_SCHEMA_PARSER_HPP
#define __VALIJSON_SCHEMA_PARSER_HPP

#include <stdexcept>
#include <iostream>

#include <boost/foreach.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/static_assert.hpp>
#include <boost/type_traits.hpp>

#include <valijson/adapters/adapter.hpp>
#include <valijson/constraints/concrete_constraints.hpp>
#include <valijson/internal/json_pointer.hpp>
#include <valijson/internal/json_reference.hpp>
#include <valijson/schema.hpp>

#ifdef __clang__
#  pragma clang diagnostic push
#  pragma clang diagnostic ignored "-Wunused-local-typedef"
#endif

namespace valijson {

/**
 * @brief  Parser for populating a Schema based on a JSON Schema document.
 *
 * The SchemaParser class supports Drafts 3 and 4 of JSON Schema, however
 * Draft 3 support should be considered deprecated.
 *
 * The functions provided by this class have been templated so that they can
 * be used with different Adapter types.
 */
class SchemaParser
{
public:

    /// Supported versions of JSON Schema
    enum Version {
        kDraft3,      ///< @deprecated JSON Schema v3 has been superseded by v4
        kDraft4
    };

    /// Version of JSON Schema that should be expected when parsing
    const Version version;

    /**
     * @brief  Construct a new SchemaParser for a given version of JSON Schema
     *
     * @param  version  Version of JSON Schema that will be expected
     */
    SchemaParser(const Version version = kDraft4)
      : version(version) { }

    /**
     * @brief  Struct to contain templated function type for fetching documents
     */
    template<typename AdapterType>
    struct FunctionPtrs
    {
        typedef typename adapters::AdapterTraits<AdapterType>::DocumentType
                DocumentType;

        /// Templated function pointer type for fetching remote documents
        typedef const AdapterType * (*FetchDoc)(const std::string &uri);

        /// Templated function pointer type for freeing fetched documents
        typedef void (*FreeDoc)(const AdapterType *);
    };

    /**
     * @brief  Populate a Schema object from JSON Schema document
     *
     * When processing Draft 3 schemas, the parentSubschema and ownName pointers
     * should be set in contexts where a 'required' constraint would be valid.
     * These are used to add a RequiredConstraint object to the Schema that
     * contains the required property.
     *
     * @param  node          Reference to node to parse
     * @param  schema        Reference to Schema to populate
     * @param  fetchDoc      Function to fetch remote JSON documents (optional)
     */
    template<typename AdapterType>
    void populateSchema(
        const AdapterType &node,
        Schema &schema,
        typename FunctionPtrs<AdapterType>::FetchDoc fetchDoc = NULL,
        typename FunctionPtrs<AdapterType>::FreeDoc freeDoc = NULL)
    {
        if ((fetchDoc == NULL) ^ (freeDoc == NULL)) {
            throw std::runtime_error(
                    "Remote document fetching cannot be enabled without both "
                    "fetch and free functions");
        }

        typename DocumentCache<AdapterType>::Type docCache;
        SchemaCache schemaCache;
        try {
            populateSchema(schema, node, node, schema, boost::none, "",
                    fetchDoc, NULL, NULL, docCache, schemaCache);
        } catch (...) {
            freeDocumentCache<AdapterType>(docCache, freeDoc);
            throw;
        }

        freeDocumentCache<AdapterType>(docCache, freeDoc);
    }

private:

    template<typename AdapterType>
    struct DocumentCache
    {
        typedef typename adapters::AdapterTraits<AdapterType>::DocumentType
                DocumentType;

        typedef std::map<std::string, const AdapterType*> Type;
    };

    typedef std::map<std::string, boost::shared_ptr<Schema> > SchemaCache;

    /**
     * @brief  Free memory used by fetched documents
     *
     * If a custom 'free' function has not been provided, then the default
     * delete operator will be used.
     *
     * @param  docCache  collection of fetched documents to free
     * @param  freeDoc   optional custom free function
     */
    template<typename AdapterType>
    void freeDocumentCache(const typename DocumentCache<AdapterType>::Type
            &docCache, typename FunctionPtrs<AdapterType>::FreeDoc freeDoc)
    {
        typedef typename DocumentCache<AdapterType>::Type DocCacheType;

        BOOST_FOREACH( const typename DocCacheType::value_type &v, docCache ) {
            freeDoc(v.second);
        }
    }

    /**
     * @brief  Extract a JSON Reference string from a node
     *
     * @param  node    node to extract the JSON Reference from
     * @param  result  reference to string to set with the result
     *
     * @throws std::invalid_argument if node is an object containing a `$ref`
     *         property but with a value that cannot be interpreted as a string
     *
     * @return \c true if a JSON Reference was extracted; \c false otherwise
     */
    template<typename AdapterType>
    bool extractJsonReference(const AdapterType &node, std::string &result)
    {
        if (!node.isObject()) {
            return false;
        }

        const typename AdapterType::Object o = node.getObject();
        const typename AdapterType::Object::const_iterator itr = o.find("$ref");
        if (itr == o.end()) {
            return false;
        } else if (!itr->second.asString(result)) {
            throw std::invalid_argument(
                    "$ref property expected to contain string value.");
        }

        return true;
    }

    /**
     * Sanitise an optional JSON Pointer, trimming trailing slashes
     */
    std::string sanitiseJsonPointer(const boost::optional<std::string> input)
    {
        if (input) {
            // Trim trailing slash(es)
            std::string sanitised = *input;
            sanitised.erase(sanitised.find_last_not_of('/') + 1,
                    std::string::npos);

            return sanitised;
        }

        // If the JSON Pointer is not set, assume that the URI points to
        // the root of the document
        return "";
    }

    /**
     * @brief  Return pointer for the schema corresponding to a given node
     *
     * @todo   Implement support for schema cache
     *
     * @param  rootSchema    The Schema instance, and root subschema, through
     *                       which other subschemas can be created and
     *                       modified
     * @param  rootNode      Reference to the node from which JSON References
     *                       will be resolved when they refer to the current
     *                       document
     * @param  node          Reference to the node to parse
     * @param  currentScope  URI for current resolution scope
     * @param  nodePath      JSON Pointer representing path to current node
     * @param  fetchDoc      Function to fetch remote JSON documents (optional)
     * @param  parentSchema  Optional pointer to the parent schema, used to
     *                       support required keyword in Draft 3
     * @param  ownName       Optional pointer to a node name, used to support
     *                       the 'required' keyword in Draft 3
     * @param  docCache      Cache of resolved and fetched remote documents
     * @param  schemaCache   Cache of populated schemas
     */
    template<typename AdapterType>
    const Subschema * makeOrReuseSchema(
        Schema &rootSchema,
        const AdapterType &rootNode,
        const AdapterType &node,
        const boost::optional<std::string> currentScope,
        const std::string &nodePath,
        const typename FunctionPtrs<AdapterType>::FetchDoc fetchDoc,
        const Subschema *parentSubschema,
        const std::string *ownName,
        typename DocumentCache<AdapterType>::Type &docCache,
        SchemaCache &schemaCache)
    {
        const Subschema *subschema = rootSchema.createSubschema();
        populateSchema<AdapterType>(rootSchema, rootNode, node, *subschema,
                currentScope, nodePath, fetchDoc, parentSubschema, ownName,
                docCache, schemaCache);

        return subschema;
    }

    /**
     * @brief  Populate a Schema object from JSON Schema document
     *
     * When processing Draft 3 schemas, the parentSubschema and ownName pointers
     * should be set in contexts where a 'required' constraint would be valid.
     * These are used to add a RequiredConstraint object to the Schema that
     * contains the required property.
     *
     * @param  rootSchema       The Schema instance, and root subschema, through
     *                          which other subschemas can be created and
     *                          modified
     * @param  rootNode         Reference to the node from which JSON References
     *                          will be resolved when they refer to the current
     *                          document
     * @param  node             Reference to node to parse
     * @param  schema           Reference to Schema to populate
     * @param  currentScope     URI for current resolution scope
     * @param  nodePath         JSON Pointer representing path to current node
     * @param  fetchDoc         Optional function to fetch remote JSON documents
     * @param  parentSubschema  Optional pointer to the parent schema, used to
     *                          support required keyword in Draft 3
     * @param  ownName          Optional pointer to a node name, used to support
     *                          the 'required' keyword in Draft 3
     * @param  docCache         Cache of resolved and fetched remote documents
     * @param  schemaCache      Cache of populated schemas
     */
    template<typename AdapterType>
    void populateSchema(
        Schema &rootSchema,
        const AdapterType &rootNode,
        const AdapterType &node,
        const Subschema &subschema,
        const boost::optional<std::string> currentScope,
        const std::string &nodePath,
        const typename FunctionPtrs<AdapterType>::FetchDoc fetchDoc,
        const Subschema *parentSubschema,
        const std::string *ownName,
        typename DocumentCache<AdapterType>::Type &docCache,
        SchemaCache &schemaCache)
    {
        BOOST_STATIC_ASSERT_MSG((boost::is_convertible<AdapterType,
            const valijson::adapters::Adapter &>::value),
            "SchemaParser::populateSchema must be invoked with an "
            "appropriate Adapter implementation");

        std::string jsonRef;
        if (extractJsonReference(node, jsonRef)) {
            populateSchemaUsingJsonReference(rootSchema, jsonRef, rootNode,
                    node, subschema, currentScope, nodePath, fetchDoc,
                    parentSubschema, ownName, docCache, schemaCache);
            return;
        }

        const typename AdapterType::Object object = node.asObject();
        typename AdapterType::Object::const_iterator itr(object.end());

        if ((itr = object.find("id")) != object.end()) {
            if (itr->second.maybeString()) {
                rootSchema.setSubschemaId(&subschema, itr->second.asString());
            }
        }

        if ((itr = object.find("allOf")) != object.end()) {
            rootSchema.addConstraintToSubschema(
                    makeAllOfConstraint(rootSchema, rootNode, itr->second,
                            currentScope, nodePath + "/allOf", fetchDoc,
                            docCache, schemaCache),
                    &subschema);
        }

        if ((itr = object.find("anyOf")) != object.end()) {
            rootSchema.addConstraintToSubschema(
                    makeAnyOfConstraint(rootSchema, rootNode, itr->second,
                            currentScope, nodePath + "/anyOf", fetchDoc,
                            docCache, schemaCache),
                    &subschema);
        }

        if ((itr = object.find("dependencies")) != object.end()) {
            rootSchema.addConstraintToSubschema(
                    makeDependenciesConstraint(rootSchema, rootNode,
                            itr->second, currentScope,
                            nodePath + "/dependencies", fetchDoc, docCache,
                            schemaCache),
                    &subschema);
        }

        if ((itr = object.find("description")) != object.end()) {
            if (itr->second.maybeString()) {
                rootSchema.setSubschemaDescription(&subschema,
                        itr->second.asString());
            } else {
                throw std::runtime_error(
                        "'description' attribute should have a string value");
            }
        }

        if ((itr = object.find("divisibleBy")) != object.end()) {
            if (version == kDraft3) {
                if (itr->second.maybeInteger()) {
                    rootSchema.addConstraintToSubschema(
                            makeMultipleOfIntConstraint(itr->second),
                            &subschema);
                } else if (itr->second.maybeDouble()) {
                    rootSchema.addConstraintToSubschema(
                            makeMultipleOfDoubleConstraint(itr->second),
                            &subschema);
                } else {
                    throw std::runtime_error("Expected an numeric value for "
                            " 'divisibleBy' constraint.");
                }
            } else {
                throw std::runtime_error(
                        "'divisibleBy' constraint not valid after draft 3");
            }
        }

        if ((itr = object.find("enum")) != object.end()) {
            rootSchema.addConstraintToSubschema(makeEnumConstraint(itr->second),
                    &subschema);
        }

        {
            const typename AdapterType::Object::const_iterator itemsItr =
                    object.find("items");

            if (object.end() != itemsItr) {
                if (!itemsItr->second.isArray()) {
                    rootSchema.addConstraintToSubschema(
                            makeSingularItemsConstraint(rootSchema, rootNode,
                                    itemsItr->second, currentScope,
                                    nodePath + "/items", fetchDoc, docCache,
                                    schemaCache),
                            &subschema);

                } else {
                    const typename AdapterType::Object::const_iterator
                            additionalItemsItr = object.find("additionalItems");
                    rootSchema.addConstraintToSubschema(
                            makeLinearItemsConstraint(rootSchema, rootNode,
                                    itemsItr != object.end() ?
                                            &itemsItr->second : NULL,
                                    additionalItemsItr != object.end() ?
                                            &additionalItemsItr->second : NULL,
                                    currentScope, nodePath + "/items",
                                    nodePath + "/additionalItems", fetchDoc,
                                    docCache, schemaCache),
                            &subschema);
                }
            }
        }

        if ((itr = object.find("maximum")) != object.end()) {
            typename AdapterType::Object::const_iterator exclusiveMaximumItr = object.find("exclusiveMaximum");
            if (exclusiveMaximumItr == object.end()) {
                rootSchema.addConstraintToSubschema(
                        makeMaximumConstraint<AdapterType>(itr->second, NULL),
                        &subschema);
            } else {
                rootSchema.addConstraintToSubschema(
                        makeMaximumConstraint(itr->second,
                                &exclusiveMaximumItr->second),
                        &subschema);
            }
        } else if (object.find("exclusiveMaximum") != object.end()) {
            throw std::runtime_error(
                    "'exclusiveMaximum' constraint only valid if a 'maximum' "
                    "constraint is also present");
        }

        if ((itr = object.find("maxItems")) != object.end()) {
            rootSchema.addConstraintToSubschema(
                    makeMaxItemsConstraint(itr->second), &subschema);
        }

        if ((itr = object.find("maxLength")) != object.end()) {
            rootSchema.addConstraintToSubschema(
                    makeMaxLengthConstraint(itr->second), &subschema);
        }

        if ((itr = object.find("maxProperties")) != object.end()) {
            rootSchema.addConstraintToSubschema(
                    makeMaxPropertiesConstraint(itr->second), &subschema);
        }

        if ((itr = object.find("minimum")) != object.end()) {
            typename AdapterType::Object::const_iterator exclusiveMinimumItr = object.find("exclusiveMinimum");
            if (exclusiveMinimumItr == object.end()) {
                rootSchema.addConstraintToSubschema(
                        makeMinimumConstraint<AdapterType>(itr->second, NULL),
                        &subschema);
            } else {
                rootSchema.addConstraintToSubschema(
                        makeMinimumConstraint(itr->second,
                                &exclusiveMinimumItr->second),
                        &subschema);
            }
        } else if (object.find("exclusiveMinimum") != object.end()) {
            throw std::runtime_error(
                    "'exclusiveMinimum' constraint only valid if a 'minimum' "
                    "constraint is also present");
        }

        if ((itr = object.find("minItems")) != object.end()) {
            rootSchema.addConstraintToSubschema(
                    makeMinItemsConstraint(itr->second), &subschema);
        }

        if ((itr = object.find("minLength")) != object.end()) {
            rootSchema.addConstraintToSubschema(
                    makeMinLengthConstraint(itr->second), &subschema);
        }

        if ((itr = object.find("minProperties")) != object.end()) {
            rootSchema.addConstraintToSubschema(
                    makeMinPropertiesConstraint(itr->second), &subschema);
        }

        if ((itr = object.find("multipleOf")) != object.end()) {
            if (version == kDraft3) {
                throw std::runtime_error(
                        "'multipleOf' constraint not available in draft 3");
            } else if (itr->second.maybeInteger()) {
                rootSchema.addConstraintToSubschema(
                        makeMultipleOfIntConstraint(itr->second),
                        &subschema);
            } else if (itr->second.maybeDouble()) {
                rootSchema.addConstraintToSubschema(
                        makeMultipleOfDoubleConstraint(itr->second),
                        &subschema);
            } else {
                throw std::runtime_error("Expected an numeric value for "
                        " 'divisibleBy' constraint.");
            }
        }

        if ((itr = object.find("not")) != object.end()) {
            rootSchema.addConstraintToSubschema(
                    makeNotConstraint(rootSchema, rootNode, itr->second,
                            currentScope, nodePath + "/not", fetchDoc, docCache,
                            schemaCache),
                    &subschema);
        }

        if ((itr = object.find("oneOf")) != object.end()) {
            rootSchema.addConstraintToSubschema(
                    makeOneOfConstraint(rootSchema, rootNode, itr->second,
                            currentScope, nodePath + "/oneOf", fetchDoc,
                            docCache, schemaCache),
                    &subschema);
        }

        if ((itr = object.find("pattern")) != object.end()) {
            rootSchema.addConstraintToSubschema(
                    makePatternConstraint(itr->second), &subschema);
        }

        {
            // Check for schema keywords that require the creation of a
            // PropertiesConstraint instance.
            const typename AdapterType::Object::const_iterator
                propertiesItr = object.find("properties"),
                patternPropertiesItr = object.find("patternProperties"),
                additionalPropertiesItr = object.find("additionalProperties");
            if (object.end() != propertiesItr ||
                object.end() != patternPropertiesItr ||
                object.end() != additionalPropertiesItr) {
                rootSchema.addConstraintToSubschema(
                        makePropertiesConstraint(rootSchema, rootNode,
                                propertiesItr != object.end() ?
                                        &propertiesItr->second : NULL,
                                patternPropertiesItr != object.end() ?
                                        &patternPropertiesItr->second : NULL,
                                additionalPropertiesItr != object.end() ?
                                        &additionalPropertiesItr->second : NULL,
                                currentScope, nodePath + "/properties",
                                nodePath + "/patternProperties",
                                nodePath + "/additionalProperties",
                                fetchDoc, &subschema, docCache, schemaCache),
                        &subschema);
            }
        }

        if ((itr = object.find("required")) != object.end()) {
            if (version == kDraft3) {
                if (parentSubschema && ownName) {
                    boost::optional<constraints::RequiredConstraint>
                            constraint = makeRequiredConstraintForSelf(
                                    itr->second, *ownName);
                    if (constraint) {
                        rootSchema.addConstraintToSubschema(*constraint,
                                parentSubschema);
                    }
                } else {
                    throw std::runtime_error("'required' constraint not valid here");
                }
            } else {
                rootSchema.addConstraintToSubschema(
                        makeRequiredConstraint(itr->second), &subschema);
            }
        }

        if ((itr = object.find("title")) != object.end()) {
            if (itr->second.maybeString()) {
                rootSchema.setSubschemaTitle(&subschema,
                        itr->second.asString());
            } else {
                throw std::runtime_error(
                        "'title' attribute should have a string value");
            }
        }

        if ((itr = object.find("type")) != object.end()) {
            rootSchema.addConstraintToSubschema(
                    makeTypeConstraint(rootSchema, rootNode, itr->second,
                            currentScope, nodePath + "/type", fetchDoc,
                            docCache, schemaCache),
                    &subschema);
        }

        if ((itr = object.find("uniqueItems")) != object.end()) {
            boost::optional<constraints::UniqueItemsConstraint> constraint =
                    makeUniqueItemsConstraint(itr->second);
            if (constraint) {
                rootSchema.addConstraintToSubschema(*constraint, &subschema);
            }
        }
    }

    /**
     * @brief  Populate a schema using a JSON Reference
     *
     * Allows JSON references to be used with minimal changes to the parser
     * helper functions.
     *
     * @param  rootSchema       The Schema instance, and root subschema, through
     *                          which other subschemas can be created and 
     *                          modified
     * @param  jsonRef          String containing JSON Reference value
     * @param  rootNode         Reference to the node from which JSON References
     *                          will be resolved when they refer to the current
     *                          document; used for recursive parsing of schemas
     * @param  node             Reference to node to parse
     * @param  schema           Reference to Schema to populate
     * @param  currentScope     URI for current resolution scope
     * @param  nodePath         JSON Pointer representing path to current node
     * @param  fetchDoc         Optional function to fetch remote JSON documents
     * @param  parentSubschema  Optional pointer to the parent schema, used to
     *                          support required keyword in Draft 3
     * @param  ownName          Optional pointer to a node name, used to support
     *                          the 'required' keyword in Draft 3
    */
    template<typename AdapterType>
    void populateSchemaUsingJsonReference(
        Schema &rootSchema,
        const std::string &jsonRef,
        const AdapterType &rootNode,
        const AdapterType &node,
        const Subschema &subschema,
        const boost::optional<std::string> currentScope,
        const std::string &nodePath,
        const typename FunctionPtrs<AdapterType>::FetchDoc fetchDoc,
        const Subschema *parentSubschema,
        const std::string *ownName,
        typename DocumentCache<AdapterType>::Type &docCache,
        SchemaCache &schemaCache)
    {
        // Returns a document URI if the reference points somewhere
        // other than the current document
        const boost::optional<std::string> documentUri =
                internal::json_reference::getJsonReferenceUri(jsonRef);

        // Extract JSON Pointer from JSON Reference
        const std::string jsonPointer = sanitiseJsonPointer(
                internal::json_reference::getJsonReferencePointer(jsonRef));

        if (documentUri) {
            // Resolve reference against remote document
            if (!fetchDoc) {
                throw std::runtime_error(
                        "Support for JSON References not enabled.");
            }

            const AdapterType * docPtr = NULL;
            const typename DocumentCache<AdapterType>::Type::const_iterator
                    docCacheItr = docCache.find(*documentUri);
            if (docCacheItr == docCache.end()) {
                // Returns a shared pointer to the remote document that was
                // retrieved, or null if retrieval failed. The resulting
                // document must remain in scope until populateSchema returns.
                docPtr = (*fetchDoc)(*documentUri);

                // Can't proceed without the remote document
                if (!docPtr) {
                    throw std::runtime_error(
                            "Failed to fetch referenced schema document.");
                }

                // TODO: If this fails, how would the document be freed?
                docCache.insert(
                    typename DocumentCache<AdapterType>::Type::value_type(
                            *documentUri, docPtr));
            } else {
                docPtr = docCacheItr->second;
            }

            const AdapterType &ref = internal::json_pointer::resolveJsonPointer(
                    *docPtr, jsonPointer);

            // Resolve reference against retrieved document
            populateSchema<AdapterType>(rootSchema, ref, ref, subschema,
                    currentScope, nodePath, fetchDoc, parentSubschema, ownName,
                    docCache, schemaCache);

        } else {
            const AdapterType &ref = internal::json_pointer::resolveJsonPointer(
                    rootNode, jsonPointer);

            // Resolve reference against current document
            populateSchema<AdapterType>(rootSchema, rootNode, ref, subschema,
                    currentScope, nodePath, fetchDoc, parentSubschema, ownName,
                    docCache, schemaCache);
        }
    }

    /**
     * @brief   Make a new AllOfConstraint object
     *
     * @param   rootSchema    The Schema instance, and root subschema, through
     *                        which other subschemas can be created and modified
     * @param   rootNode      Reference to the node from which JSON References
     *                        will be resolved when they refer to the current
     *                        document; used for recursive parsing of schemas
     * @param   node          JSON node containing an array of child schemas
     * @param   currentScope  URI for current resolution scope
     * @param   nodePath      JSON Pointer representing path to current node
     * @param   fetchDoc      Function to fetch remote JSON documents (optional)
     * @param   docCache      Cache of resolved and fetched remote documents
     * @param   schemaCache   Cache of populated schemas
     *
     * @return  pointer to a new AllOfConstraint object that belongs to the
     *          caller
     */
    template<typename AdapterType>
    constraints::AllOfConstraint makeAllOfConstraint(
        Schema &rootSchema,
        const AdapterType &rootNode,
        const AdapterType &node,
        const boost::optional<std::string> currentScope,
        const std::string &nodePath,
        const typename FunctionPtrs<AdapterType>::FetchDoc fetchDoc,
        typename DocumentCache<AdapterType>::Type &docCache,
        SchemaCache &schemaCache)
    {
        if (!node.maybeArray()) {
            throw std::runtime_error(
                    "Expected array value for 'allOf' constraint.");
        }

        constraints::AllOfConstraint constraint;

        int index = 0;
        BOOST_FOREACH ( const AdapterType schemaNode, node.asArray() ) {
            if (schemaNode.maybeObject()) {
                const std::string childPath = nodePath + "/" +
                        boost::lexical_cast<std::string>(index);
                const Subschema *subschema = makeOrReuseSchema<AdapterType>(
                        rootSchema, rootNode, schemaNode, currentScope,
                        childPath, fetchDoc, NULL, NULL, docCache, schemaCache);
                constraint.addSubschema(subschema);
                index++;
            } else {
                throw std::runtime_error(
                        "Expected array element to be an object value in "
                        "'allOf' constraint.");
            }
        }

        return constraint;
    }

    /**
     * @brief   Make a new AnyOfConstraint object
     *
     * @param   rootSchema    The Schema instance, and root subschema, through
     *                        which other subschemas can be created and modified
     * @param   rootNode      Reference to the node from which JSON References
     *                        will be resolved when they refer to the current
     *                        document; used for recursive parsing of schemas
     * @param   node          JSON node containing an array of child schemas
     * @param   currentScope  URI for current resolution scope
     * @param   nodePath      JSON Pointer representing path to current node
     * @param   fetchDoc      Function to fetch remote JSON documents (optional)
     * @param   docCache      Cache of resolved and fetched remote documents
     * @param   schemaCache   Cache of populated schemas
     *
     * @return  pointer to a new AnyOfConstraint object that belongs to the
     *          caller
     */
    template<typename AdapterType>
    constraints::AnyOfConstraint makeAnyOfConstraint(
        Schema &rootSchema,
        const AdapterType &rootNode,
        const AdapterType &node,
        const boost::optional<std::string> currentScope,
        const std::string &nodePath,
        const typename FunctionPtrs<AdapterType>::FetchDoc fetchDoc,
        typename DocumentCache<AdapterType>::Type &docCache,
        SchemaCache &schemaCache)
    {
        if (!node.maybeArray()) {
            throw std::runtime_error(
                    "Expected array value for 'anyOf' constraint.");
        }

        constraints::AnyOfConstraint constraint;

        int index = 0;
        BOOST_FOREACH ( const AdapterType schemaNode, node.asArray() ) {
            if (schemaNode.maybeObject()) {
                const std::string childPath = nodePath + "/" +
                        boost::lexical_cast<std::string>(index);
                const Subschema *subschema = makeOrReuseSchema<AdapterType>(
                        rootSchema, rootNode, schemaNode, currentScope,
                        childPath, fetchDoc, NULL, NULL, docCache, schemaCache);
                constraint.addSubschema(subschema);
                index++;
            } else {
                throw std::runtime_error(
                        "Expected array element to be an object value in "
                        "'anyOf' constraint.");
            }
        }

        return constraint;
    }

    /**
     * @brief   Make a new DependenciesConstraint object
     *
     * The dependencies for a property can be defined several ways. When parsing
     * a Draft 4 schema, the following can be used:
     *  - an array that lists the name of each property that must be present
     *    if the dependent property is present
     *  - an object that specifies a schema which must be satisfied if the
     *    dependent property is present
     *
     * When parsing a Draft 3 schema, in addition to the formats above, the
     * following format can be used:
     *  - a string that names a single property that must be present if the
     *    dependent property is presnet
     *
     * Multiple methods can be used in the same dependency constraint.
     *
     * If the format of any part of the the dependency node does not match one
     * of these formats, an exception will be thrown.
     *
     * @param   rootSchema    The Schema instance, and root subschema, through
     *                        which other subschemas can be created and modified
     * @param   rootNode      Reference to the node from which JSON References
     *                        will be resolved when they refer to the current
     *                        document; used for recursive parsing of schemas
     * @param   node          JSON node containing an object that defines a
     *                        mapping of properties to their dependencies.
     * @param   currentScope  URI for current resolution scope
     * @param   nodePath      JSON Pointer representing path to current node
     * @param   fetchDoc      Function to fetch remote JSON documents (optional)
     * @param   docCache      Cache of resolved and fetched remote documents
     * @param   schemaCache   Cache of populated schemas
     *
     * @return  pointer to a new DependencyConstraint that belongs to the
     *          caller
     */
    template<typename AdapterType>
    constraints::DependenciesConstraint makeDependenciesConstraint(
        Schema &rootSchema,
        const AdapterType &rootNode,
        const AdapterType &node,
        const boost::optional<std::string> currentScope,
        const std::string &nodePath,
        const typename FunctionPtrs<AdapterType>::FetchDoc fetchDoc,
        typename DocumentCache<AdapterType>::Type &docCache,
        SchemaCache &schemaCache)
    {
        if (!node.maybeObject()) {
            throw std::runtime_error("Expected object value for 'dependencies' constraint.");
        }

        constraints::DependenciesConstraint dependenciesConstraint;

        // Process each of the dependency mappings defined by the object
        BOOST_FOREACH ( const typename AdapterType::ObjectMember member, node.asObject() ) {

            // First, we attempt to parse the value of the dependency mapping
            // as an array of strings. If the Adapter type does not support
            // strict types, then an empty string or empty object will be cast
            // to an array, and the resulting dependency list will be empty.
            // This is equivalent to using an empty object, but does mean that
            // if the user provides an actual string then this error will not
            // be detected.
            if (member.second.maybeArray()) {
                // Parse an array of dependency names
                std::vector<std::string> dependentPropertyNames;
                BOOST_FOREACH( const AdapterType dependencyName, member.second.asArray() ) {
                    if (dependencyName.maybeString()) {
                        dependentPropertyNames.push_back(dependencyName.getString());
                    } else {
                        throw std::runtime_error("Expected string value in dependency list of property '" +
                            member.first + "' in 'dependencies' constraint.");
                    }
                }

                dependenciesConstraint.addPropertyDependencies(member.first,
                        dependentPropertyNames);

            // If the value of dependency mapping could not be processed as an
            // array, we'll try to process it as an object instead. Note that
            // strict type comparison is used here, since we've already
            // exercised the flexibility by loosely-typed Adapter types. If the
            // value of the dependency mapping is an object, then we'll try to
            // process it as a dependent schema.
            } else if (member.second.isObject()) {
                // Parse dependent subschema
                const Subschema *childSubschema =
                        makeOrReuseSchema<AdapterType>(rootSchema, rootNode,
                                member.second, currentScope, nodePath, fetchDoc,
                                NULL, NULL, docCache, schemaCache);
                dependenciesConstraint.addSchemaDependency(member.first,
                        childSubschema);

            // If we're supposed to be parsing a Draft3 schema, then the value
            // of the dependency mapping can also be a string containing the
            // name of a single dependency.
            } else if (version == kDraft3 && member.second.isString()) {
                dependenciesConstraint.addPropertyDependency(member.first,
                        member.second.getString());

            // All other types result in an exception being thrown.
            } else {
                throw std::runtime_error("Invalid dependencies definition.");
            }
        }

        return dependenciesConstraint;
    }

    /**
     * @brief   Make a new EnumConstraint object.
     *
     * @param   node  JSON node containing an array of values permitted by the
     *                constraint.
     *
     * @return  pointer to a new EnumConstraint that belongs to the caller
     */
    template<typename AdapterType>
    constraints::EnumConstraint makeEnumConstraint(
        const AdapterType &node)
    {
        // Make a copy of each value in the enum array
        constraints::EnumConstraint constraint;
        BOOST_FOREACH( const AdapterType value, node.getArray() ) {
            constraint.addValue(value);
        }

        /// @todo This will make another copy of the values while constructing
        /// the EnumConstraint. Move semantics in C++11 should make it possible
        /// to avoid these copies without complicating the implementation of the
        /// EnumConstraint class.
        return constraint;
    }

    /**
     * @brief   Make a new ItemsConstraint object.
     *
     * @param   rootSchema           The Schema instance, and root subschema,
     *                               through which other subschemas can be 
     *                               created and modified
     * @param   rootNode             Reference to the node from which JSON
     *                               References will be resolved when they refer
     *                               to the current document; used for recursive
     *                               parsing of schemas
     * @param   items                Optional pointer to a JSON node containing
     *                               an object mapping property names to 
     *                               schemas.
     * @param   additionalItems      Optional pointer to a JSON node containing
     *                               an additional properties schema or a
     *                               boolean value.
     * @param   currentScope         URI for current resolution scope
     * @param   itemsPath            JSON Pointer representing the path to
     *                               the 'items' node
     * @param   additionalItemsPath  JSON Pointer representing the path to
     *                               the 'additionalItems' node
     * @param   fetchDoc             Function to fetch remote JSON documents
     *                               (optional)
     * @param   docCache             Cache of resolved and fetched remote
     *                               documents
     * @param   schemaCache          Cache of populated schemas
     *
     * @return  pointer to a new ItemsConstraint that belongs to the caller
     */
    template<typename AdapterType>
    constraints::LinearItemsConstraint makeLinearItemsConstraint(
        Schema &rootSchema,
        const AdapterType &rootNode,
        const AdapterType *items,
        const AdapterType *additionalItems,
        const boost::optional<std::string> currentScope,
        const std::string &itemsPath,
        const std::string &additionalItemsPath,
        const typename FunctionPtrs<AdapterType>::FetchDoc fetchDoc,
        typename DocumentCache<AdapterType>::Type &docCache,
        SchemaCache &schemaCache)
    {
        constraints::LinearItemsConstraint constraint;

        // Construct a Schema object for the the additionalItems constraint,
        // if the additionalItems property is present
        if (additionalItems) {
            if (additionalItems->maybeBool()) {
                // If the value of the additionalItems property is a boolean
                // and is set to true, then additional array items do not need
                // to satisfy any constraints.
                if (additionalItems->asBool()) {
                    constraint.setAdditionalItemsSubschema(
                            rootSchema.emptySubschema());
                }
            } else if (additionalItems->maybeObject()) {
                // If the value of the additionalItems property is an object,
                // then it should be parsed into a Schema object, which will be
                // used to validate additional array items.
                const Subschema *subschema = makeOrReuseSchema<AdapterType>(
                        rootSchema, rootNode, *additionalItems, currentScope,
                        additionalItemsPath, fetchDoc, NULL, NULL, docCache,
                        schemaCache);
                constraint.setAdditionalItemsSubschema(subschema);
            } else {
                // Any other format for the additionalItems property will result
                // in an exception being thrown.
                throw std::runtime_error(
                        "Expected bool or object value for 'additionalItems'");
            }
        } else {
            // The default value for the additionalItems property is an empty
            // object, which means that additional array items do not need to
            // satisfy any constraints.
            constraint.setAdditionalItemsSubschema(rootSchema.emptySubschema());
        }

        // Construct a Schema object for each item in the items array.
        // If the items constraint is not provided, then array items
        // will be validated against the additionalItems schema.
        if (items) {
            if (items->isArray()) {
                // If the items constraint contains an array, then it should
                // contain a list of child schemas which will be used to
                // validate the values at the corresponding indexes in a target
                // array.
                int index = 0;
                BOOST_FOREACH( const AdapterType v, items->getArray() ) {
                    const std::string childPath = itemsPath + "/" +
                            boost::lexical_cast<std::string>(index);
                    const Subschema *subschema = makeOrReuseSchema<AdapterType>(
                            rootSchema, rootNode, v, currentScope, childPath,
                            fetchDoc, NULL, NULL, docCache, schemaCache);
                    constraint.addItemSubschema(subschema);
                    index++;
                }
            } else {
                throw std::runtime_error(
                        "Expected array value for non-singular 'items' "
                        "constraint.");
            }
        }

        return constraint;
    }

    /**
     * @brief   Make a new ItemsConstraint object.
     *
     * @param   rootSchema           The Schema instance, and root subschema,
     *                               through which other subschemas can be 
     *                               created and modified
     * @param   rootNode             Reference to the node from which JSON
     *                               References will be resolved when they refer
     *                               to the current document; used for recursive
     *                               parsing of schemas
     * @param   items                Optional pointer to a JSON node containing
     *                               an object mapping property names to 
     *                               schemas.
     * @param   additionalItems      Optional pointer to a JSON node containing
     *                               an additional properties schema or a
     *                               boolean value.
     * @param   currentScope         URI for current resolution scope
     * @param   itemsPath            JSON Pointer representing the path to
     *                               the 'items' node
     * @param   additionalItemsPath  JSON Pointer representing the path to
     *                               the 'additionalItems' node
     * @param   fetchDoc             Function to fetch remote JSON documents
     *                               (optional)
     * @param   docCache             Cache of resolved and fetched remote
     *                               documents
     * @param   schemaCache          Cache of populated schemas
     *
     * @return  pointer to a new ItemsConstraint that belongs to the caller
     */
    template<typename AdapterType>
    constraints::SingularItemsConstraint makeSingularItemsConstraint(
        Schema &rootSchema,
        const AdapterType &rootNode,
        const AdapterType &items,
        const boost::optional<std::string> currentScope,
        const std::string &itemsPath,
        const typename FunctionPtrs<AdapterType>::FetchDoc fetchDoc,
        typename DocumentCache<AdapterType>::Type &docCache,
        SchemaCache &schemaCache)
    {
        constraints::SingularItemsConstraint constraint;

        // Construct a Schema object for each item in the items array, if an
        // array is provided, or a single Schema object, in an object value is
        // provided. If the items constraint is not provided, then array items
        // will be validated against the additionalItems schema.
        if (items.isObject()) {
            // If the items constraint contains an object value, then it
            // should contain a Schema that will be used to validate all
            // items in a target array. Any schema defined by the
            // additionalItems constraint will be ignored.
            const Subschema *subschema = makeOrReuseSchema<AdapterType>(
                    rootSchema, rootNode, items, currentScope, itemsPath,
                    fetchDoc, NULL, NULL, docCache, schemaCache);
            constraint.setItemsSubschema(subschema);

        } else if (items.maybeObject()) {
            // If a loosely-typed Adapter type is being used, then we'll
            // assume that an empty schema has been provided.
            constraint.setItemsSubschema(rootSchema.emptySubschema());

        } else {
            // All other formats will result in an exception being thrown.
            throw std::runtime_error(
                    "Expected object value for singular 'items' "
                    "constraint.");
        }

        return constraint;
    }

    /**
     * @brief   Make a new MaximumConstraint object.
     *
     * @param   rootSchema        The Schema instance, and root subschema,
     *                            through which other subschemas can be
     *                            created and modified
     * @param   rootNode          Reference to the node from which JSON
     *                            References will be resolved when they refer
     *                            to the current document; used for recursive
     *                            parsing of schemas
     * @param   node              JSON node containing the maximum value.
     * @param   exclusiveMaximum  Optional pointer to a JSON boolean value that
     *                            indicates whether maximum value is excluded
     *                            from the range of permitted values.
     *
     * @return  pointer to a new MaximumConstraint that belongs to the caller
     */
    template<typename AdapterType>
    constraints::MaximumConstraint makeMaximumConstraint(
        const AdapterType &node,
        const AdapterType *exclusiveMaximum)
    {
        if (!node.maybeDouble()) {
            throw std::runtime_error(
                    "Expected numeric value for maximum constraint.");
        }

        constraints::MaximumConstraint constraint;
        constraint.setMaximum(node.asDouble());

        if (exclusiveMaximum) {
            if (!exclusiveMaximum->maybeBool()) {
                throw std::runtime_error(
                        "Expected boolean value for exclusiveMaximum "
                        "constraint.");
            }

            constraint.setExclusiveMaximum(exclusiveMaximum->asBool());
        }

        return constraint;
    }

    /**
     * @brief   Make a new MaxItemsConstraint object.
     *
     * @param   node  JSON node containing an integer value representing the
     *                maximum number of items that may be contaned by an array.
     *
     * @return  pointer to a new MaxItemsConstraint that belongs to the caller.
     */
    template<typename AdapterType>
    constraints::MaxItemsConstraint makeMaxItemsConstraint(
        const AdapterType &node)
    {
        if (node.maybeInteger()) {
            const int64_t value = node.asInteger();
            if (value >= 0) {
                constraints::MaxItemsConstraint constraint;
                constraint.setMaxItems(value);
                return constraint;
            }
        }

        throw std::runtime_error(
                "Expected non-negative integer value for 'maxItems' "
                "constraint.");
    }

    /**
     * @brief   Make a new MaxLengthConstraint object.
     *
     * @param   node  JSON node containing an integer value representing the
     *                maximum length of a string.
     *
     * @return  pointer to a new MaxLengthConstraint that belongs to the caller
     */
    template<typename AdapterType>
    constraints::MaxLengthConstraint makeMaxLengthConstraint(
        const AdapterType &node)
    {
        if (node.maybeInteger()) {
            const int64_t value = node.asInteger();
            if (value >= 0) {
                constraints::MaxLengthConstraint constraint;
                constraint.setMaxLength(value);
                return constraint;
            }
        }

        throw std::runtime_error(
                "Expected a non-negative integer value for 'maxLength' "
                "constraint.");
    }

    /**
     * @brief   Make a new MaxPropertiesConstraint object.
     *
     * @param   node  JSON node containing an integer value representing the
     *                maximum number of properties that may be contained by an
     *                object.
     *
     * @return  pointer to a new MaxPropertiesConstraint that belongs to the
     *          caller
     */
    template<typename AdapterType>
    constraints::MaxPropertiesConstraint makeMaxPropertiesConstraint(
        const AdapterType &node)
    {
        if (node.maybeInteger()) {
            int64_t value = node.asInteger();
            if (value >= 0) {
                constraints::MaxPropertiesConstraint constraint;
                constraint.setMaxProperties(value);
                return constraint;
            }
        }

        throw std::runtime_error(
                "Expected a non-negative integer for 'maxProperties' "
                "constraint.");
    }

    /**
     * @brief  Make a new MinimumConstraint object.
     *
     * @param  node              JSON node containing an integer, representing
     *                           the minimum value.
     *
     * @param  exclusiveMaximum  Optional pointer to a JSON boolean value that
     *                           indicates whether the minimum value is
     *                           excluded from the range of permitted values.
     *
     * @return  pointer to a new MinimumConstraint that belongs to the caller
     */
    template<typename AdapterType>
    constraints::MinimumConstraint makeMinimumConstraint(
        const AdapterType &node,
        const AdapterType *exclusiveMinimum)
    {
        if (!node.maybeDouble()) {
            throw std::runtime_error(
                    "Expected numeric value for minimum constraint.");
        }

        constraints::MinimumConstraint constraint;
        constraint.setMinimum(node.asDouble());

        if (exclusiveMinimum) {
            if (!exclusiveMinimum->maybeBool()) {
                throw std::runtime_error(
                        "Expected boolean value for 'exclusiveMinimum' "
                        "constraint.");
            }

            constraint.setExclusiveMinimum(exclusiveMinimum->asBool());
        }

        return constraint;
    }

    /**
     * @brief  Make a new MinItemsConstraint object.
     *
     * @param  node  JSON node containing an integer value representing the
     *               minimum number of items that may be contained by an array.
     *
     * @return  pointer to a new MinItemsConstraint that belongs to the caller
     */
    template<typename AdapterType>
    constraints::MinItemsConstraint makeMinItemsConstraint(
        const AdapterType &node)
    {
        if (node.maybeInteger()) {
            const int64_t value = node.asInteger();
            if (value >= 0) {
                constraints::MinItemsConstraint constraint;
                constraint.setMinItems(value);
                return constraint;
            }
        }

        throw std::runtime_error(
                "Expected a non-negative integer value for 'minItems' "
                "constraint.");
    }

    /**
     * @brief  Make a new MinLengthConstraint object.
     *
     * @param  node  JSON node containing an integer value representing the
     *               minimum length of a string.
     *
     * @return  pointer to a new MinLengthConstraint that belongs to the caller
     */
    template<typename AdapterType>
    constraints::MinLengthConstraint makeMinLengthConstraint(
        const AdapterType &node)
    {
        if (node.maybeInteger()) {
            const int64_t value = node.asInteger();
            if (value >= 0) {
                constraints::MinLengthConstraint constraint;
                constraint.setMinLength(value);
                return constraint;
            }
        }

        throw std::runtime_error(
                "Expected a non-negative integer value for 'minLength' "
                "constraint.");
    }


    /**
     * @brief   Make a new MaxPropertiesConstraint object.
     *
     * @param   node  JSON node containing an integer value representing the
     *                minimum number of properties that may be contained by an
     *                object.
     *
     * @return  pointer to a new MinPropertiesConstraint that belongs to the
     *          caller
     */
    template<typename AdapterType>
    constraints::MinPropertiesConstraint makeMinPropertiesConstraint(
        const AdapterType &node)
    {
        if (node.maybeInteger()) {
            int64_t value = node.asInteger();
            if (value >= 0) {
                constraints::MinPropertiesConstraint constraint;
                constraint.setMinProperties(value);
                return constraint;
            }
        }

        throw std::runtime_error(
                "Expected a non-negative integer for 'minProperties' "
                "constraint.");
    }

    /**
     * @brief   Make a new MultipleOfDoubleConstraint object
     *
     * @param   node  JSON node containing an numeric value that a target value
     *                must divide by in order to satisfy this constraint
     *
     * @return  a MultipleOfConstraint
     */
    template<typename AdapterType>
    constraints::MultipleOfDoubleConstraint makeMultipleOfDoubleConstraint(
        const AdapterType &node)
    {
        constraints::MultipleOfDoubleConstraint constraint;
        constraint.setDivisor(node.asDouble());
        return constraint;
    }

    /**
     * @brief   Make a new MultipleOfIntConstraint object
     *
     * @param   node  JSON node containing a numeric value that a target value
     *                must divide by in order to satisfy this constraint
     *
     * @return  a MultipleOfIntConstraint
     */
    template<typename AdapterType>
    constraints::MultipleOfIntConstraint makeMultipleOfIntConstraint(
            const AdapterType &node)
    {
        constraints::MultipleOfIntConstraint constraint;
        constraint.setDivisor(node.asInteger());
        return constraint;
    }

    /**
     * @brief   Make a new NotConstraint object
     *
     * @param   rootSchema    The Schema instance, and root subschema, through
     *                        which other subschemas can be created and modified
     * @param   rootNode      Reference to the node from which JSON References
     *                        will be resolved when they refer to the current
     *                        document; used for recursive parsing of schemas
     * @param   node          JSON node containing a schema
     * @param   currentScope  URI for current resolution scope
     * @param   nodePath      JSON Pointer representing path to current node
     * @param   fetchDoc      Function to fetch remote JSON documents (optional)
     * @param   docCache      Cache of resolved and fetched remote documents
     * @param   schemaCache   Cache of populated schemas
     *
     * @return  pointer to a new NotConstraint object that belongs to the caller
     */
    template<typename AdapterType>
    constraints::NotConstraint makeNotConstraint(
        Schema &rootSchema,
        const AdapterType &rootNode,
        const AdapterType &node,
        const boost::optional<std::string> currentScope,
        const std::string &nodePath,
        const typename FunctionPtrs<AdapterType>::FetchDoc fetchDoc,
        typename DocumentCache<AdapterType>::Type &docCache,
        SchemaCache &schemaCache)
    {
        if (node.maybeObject()) {
            const Subschema *subschema = makeOrReuseSchema<AdapterType>(
                    rootSchema, rootNode, node, currentScope, nodePath,
                    fetchDoc, NULL, NULL, docCache, schemaCache);
            constraints::NotConstraint constraint;
            constraint.setSubschema(subschema);
            return constraint;
        }

        throw std::runtime_error("Expected object value for 'not' constraint.");
    }

    /**
     * @brief   Make a new OneOfConstraint object
     *
     * @param   rootSchema    The Schema instance, and root subschema, through
     *                        which other subschemas can be created and modified
     * @param   rootNode      Reference to the node from which JSON References
     *                        will be resolved when they refer to the current
     *                        document; used for recursive parsing of schemas
     * @param   node          JSON node containing an array of child schemas
     * @param   currentScope  URI for current resolution scope
     * @param   nodePath      JSON Pointer representing path to current node
     * @param   fetchDoc      Function to fetch remote JSON documents (optional)
     * @param   docCache      Cache of resolved and fetched remote documents
     * @param   schemaCache   Cache of populated schemas
     *
     * @return  pointer to a new OneOfConstraint that belongs to the caller
     */
    template<typename AdapterType>
    constraints::OneOfConstraint makeOneOfConstraint(
        Schema &rootSchema,
        const AdapterType &rootNode,
        const AdapterType &node,
        const boost::optional<std::string> currentScope,
        const std::string &nodePath,
        const typename FunctionPtrs<AdapterType>::FetchDoc fetchDoc,
        typename DocumentCache<AdapterType>::Type &docCache,
        SchemaCache &schemaCache)
    {
        constraints::OneOfConstraint constraint;

        int index = 0;
        BOOST_FOREACH ( const AdapterType schemaNode, node.getArray() ) {
            const std::string childPath = nodePath + "/" +
                    boost::lexical_cast<std::string>(index);
            const Subschema *subschema = makeOrReuseSchema<AdapterType>(
                rootSchema, rootNode, schemaNode, currentScope, childPath,
                fetchDoc, NULL, NULL, docCache, schemaCache);
            constraint.addSubschema(subschema);
            index++;
        }

        return constraint;
    }

    /**
     * @brief   Make a new PatternConstraint object.
     *
     * @param   node      JSON node containing a pattern string
     *
     * @return  pointer to a new PatternConstraint object that belongs to the
     *          caller
     */
    template<typename AdapterType>
    constraints::PatternConstraint makePatternConstraint(
        const AdapterType &node)
    {
        constraints::PatternConstraint constraint;
        constraint.setPattern(node.getString());
        return constraint;
    }

    /**
     * @brief   Make a new Properties object.
     *
     * @param   rootSchema                The Schema instance, and root
     *                                    subschema, through which other 
     *                                    subschemas can be created and modified
     * @param   rootNode                  Reference to the node from which JSON
     *                                    References will be resolved when they
     *                                    refer to the current document; used
     *                                    for recursive parsing of schemas
     * @param   properties                Optional pointer to a JSON node
     *                                    containing an object mapping property
     *                                    names to schemas.
     * @param   patternProperties         Optional pointer to a JSON node
     *                                    containing an object mapping pattern
     *                                    property names to schemas.
     * @param   additionalProperties      Optional pointer to a JSON node
     *                                    containing an additional properties
     *                                    schema or a boolean value.
     * @param   currentScope              URI for current resolution scope
     * @param   propertiesPath            JSON Pointer representing the path to
     *                                    the 'properties' node
     * @param   patternPropertiesPath     JSON Pointer representing the path to
     *                                    the 'patternProperties' node
     * @param   additionalPropertiesPath  JSON Pointer representing the path to
     *                                    the 'additionalProperties' node
     * @param   fetchDoc                  Function to fetch remote JSON
     *                                    documents (optional)
     * @param   parentSubschema           Optional pointer to the Schema of the
     *                                    parent object, needed to support the
     *                                    'required' keyword in Draft 3
     * @param   docCache                  Cache of resolved and fetched remote
     *                                    documents
     * @param   schemaCache               Cache of populated schemas
     *
     * @return  pointer to a new Properties that belongs to the caller
     */
    template<typename AdapterType>
    constraints::PropertiesConstraint makePropertiesConstraint(
        Schema &rootSchema,
        const AdapterType &rootNode,
        const AdapterType *properties,
        const AdapterType *patternProperties,
        const AdapterType *additionalProperties,
        const boost::optional<std::string> currentScope,
        const std::string &propertiesPath,
        const std::string &patternPropertiesPath,
        const std::string &additionalPropertiesPath,
        const typename FunctionPtrs<AdapterType>::FetchDoc fetchDoc,
        const Subschema *parentSubschema,
        typename DocumentCache<AdapterType>::Type &docCache,
        SchemaCache &schemaCache)
    {
        typedef typename AdapterType::ObjectMember Member;

        constraints::PropertiesConstraint constraint;

        // Create subschemas for 'properties' constraint
        if (properties) {
            BOOST_FOREACH( const Member m, properties->getObject() ) {
                const std::string &property = m.first;
                const std::string childPath = propertiesPath + "/" + property;
                const Subschema *subschema = makeOrReuseSchema<AdapterType>(
                        rootSchema, rootNode, m.second, currentScope, childPath,
                        fetchDoc, parentSubschema, &property, docCache,
                        schemaCache);
                constraint.addPropertySubschema(property, subschema);
            }
        }

        // Create subschemas for 'patternProperties' constraint
        if (patternProperties) {
            BOOST_FOREACH( const Member m, patternProperties->getObject() ) {
                const std::string &pattern = m.first;
                const std::string childPath = patternPropertiesPath + "/" +
                        pattern;
                const Subschema *subschema = makeOrReuseSchema<AdapterType>(
                        rootSchema, rootNode, m.second, currentScope, childPath,
                        fetchDoc, parentSubschema, &pattern, docCache,
                        schemaCache);
                constraint.addPatternPropertySubschema(pattern, subschema);
            }
        }

        // Create an additionalItems subschema if required
        if (additionalProperties) {
            // If additionalProperties has been set, check for a boolean value.
            // Setting 'additionalProperties' to true allows the values of
            // additional properties to take any form. Setting it false
            // prohibits the use of additional properties.
            // If additionalProperties is instead an object, it should be
            // parsed as a schema. If additionalProperties has any other type,
            // then the schema is not valid.
            if (additionalProperties->isBool() ||
                additionalProperties->maybeBool()) {
                // If it has a boolean value that is 'true', then an empty
                // schema should be used.
                if (additionalProperties->asBool()) {
                    constraint.setAdditionalPropertiesSubschema(
                            rootSchema.emptySubschema());
                }
            } else if (additionalProperties->isObject()) {
                // If additionalProperties is an object, it should be used as
                // a child schema.
                const Subschema *subschema = makeOrReuseSchema<AdapterType>(
                        rootSchema, rootNode, *additionalProperties,
                        currentScope, additionalPropertiesPath, fetchDoc, NULL,
                        NULL, docCache, schemaCache);
                constraint.setAdditionalPropertiesSubschema(subschema);
            } else {
                // All other types are invalid
                throw std::runtime_error(
                        "Invalid type for 'additionalProperties' constraint.");
            }
        } else {
            // If an additionalProperties constraint is not provided, then the
            // default value is an empty schema.
            constraint.setAdditionalPropertiesSubschema(
                    rootSchema.emptySubschema());
        }

        return constraint;
    }

    /**
     * @brief   Make a new RequiredConstraint.
     *
     * This function is used to create new RequiredContraint objects for
     * Draft 3 schemas.
     *
     * @param   node  Node containing a boolean value.
     * @param   name  Name of the required attribute.
     *
     * @return  pointer to a new RequiredConstraint object that belongs to the
     *          caller
     */
    template<typename AdapterType>
    boost::optional<constraints::RequiredConstraint>
            makeRequiredConstraintForSelf(const AdapterType &node,
                    const std::string &name)
    {
        if (!node.maybeBool()) {
            throw std::runtime_error("Expected boolean value for 'required' attribute.");
        }

        if (node.asBool()) {
            constraints::RequiredConstraint constraint;
            constraint.addRequiredProperty(name);
            return constraint;
        }

        return boost::none;
    }

    /**
     * @brief   Make a new RequiredConstraint.
     *
     * This function is used to create new RequiredContraint objects for
     * Draft 4 schemas.
     *
     * @param   node  Node containing an array of strings.
     *
     * @return  pointer to a new RequiredConstraint object that belongs to the
     *          caller
     */
    template<typename AdapterType>
    constraints::RequiredConstraint makeRequiredConstraint(
        const AdapterType &node)
    {
        constraints::RequiredConstraint constraint;

        BOOST_FOREACH( const AdapterType v, node.getArray() ) {
            if (!v.isString()) {
                throw std::runtime_error("Expected required property name to "
                        "be a string value");
            }

            constraint.addRequiredProperty(v.getString());
        }

        return constraint;
    }

    /**
     * @brief   Make a new TypeConstraint object
     *
     * @param   rootSchema    The Schema instance, and root subschema, through
     *                        which other subschemas can be created and modified
     * @param   rootNode      Reference to the node from which JSON References
     *                        will be resolved when they refer to the current
     *                        document; used for recursive parsing of schemas
     * @param   node          Node containing the name of a JSON type
     * @param   currentScope  URI for current resolution scope
     * @param   nodePath      JSON Pointer representing path to current node
     * @param   fetchDoc      Function to fetch remote JSON documents (optional)
     * @param   docCache      Cache of resolved and fetched remote documents
     * @param   schemaCache   Cache of populated schemas
     *
     * @return  pointer to a new TypeConstraint object.
     */
    template<typename AdapterType>
    constraints::TypeConstraint makeTypeConstraint(
        Schema &rootSchema,
        const AdapterType &rootNode,
        const AdapterType &node,
        const boost::optional<std::string> currentScope,
        const std::string &nodePath,
        const typename FunctionPtrs<AdapterType>::FetchDoc fetchDoc,
        typename DocumentCache<AdapterType>::Type &docCache,
        SchemaCache &schemaCache)
    {
        typedef constraints::TypeConstraint TypeConstraint;

        TypeConstraint constraint;

        if (node.isString()) {
            const TypeConstraint::JsonType type =
                    TypeConstraint::jsonTypeFromString(node.getString());

            if (type == TypeConstraint::kAny && version == kDraft4) {
                throw std::runtime_error(
                        "'any' type is not supported in version 4 schemas.");
            }

            constraint.addNamedType(type);

        } else if (node.isArray()) {
            int index = 0;
            BOOST_FOREACH( const AdapterType v, node.getArray() ) {
                if (v.isString()) {
                    const TypeConstraint::JsonType type =
                            TypeConstraint::jsonTypeFromString(v.getString());

                    if (type == TypeConstraint::kAny && version == kDraft4) {
                        throw std::runtime_error(
                                "'any' type is not supported in version 4 "
                                "schemas.");
                    }

                    constraint.addNamedType(type);

                } else if (v.isObject() && version == kDraft3) {
                    const std::string childPath = nodePath + "/" +
                            boost::lexical_cast<std::string>(index);
                    const Subschema *subschema = makeOrReuseSchema<AdapterType>(
                            rootSchema, rootNode, v, currentScope, childPath,
                            fetchDoc, NULL, NULL, docCache, schemaCache);
                    constraint.addSchemaType(subschema);

                } else {
                    throw std::runtime_error("Type name should be a string.");
                }

                index++;
            }

        } else if (node.isObject() && version == kDraft3) {
            const Subschema *subschema = makeOrReuseSchema<AdapterType>(
                    rootSchema, rootNode, node, currentScope, nodePath,
                    fetchDoc, NULL, NULL, docCache, schemaCache);
            constraint.addSchemaType(subschema);

        } else {
            throw std::runtime_error("Type name should be a string.");
        }

        return constraint;
    }

    /**
     * @brief   Make a new UniqueItemsConstraint object.
     *
     * @param   node  Node containing a boolean value.
     *
     * @return  pointer to a new UniqueItemsConstraint object that belongs to
     *          the caller, or NULL if the boolean value is false.
     */
    template<typename AdapterType>
    boost::optional<constraints::UniqueItemsConstraint>
            makeUniqueItemsConstraint(const AdapterType &node)
    {
        if (node.isBool() || node.maybeBool()) {
            // If the boolean value is true, this function will return a pointer
            // to a new UniqueItemsConstraint object. If it is value, then the
            // constraint is redundant, so NULL is returned instead.
            if (node.asBool()) {
                return constraints::UniqueItemsConstraint();
            } else {
                return boost::none;
            }
        }

        throw std::runtime_error(
                "Expected boolean value for 'uniqueItems' constraint.");
    }

};

}  // namespace valijson

#ifdef __clang__
#  pragma clang diagnostic pop
#endif

#endif
