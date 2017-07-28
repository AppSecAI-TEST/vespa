// Copyright 2017 Yahoo Holdings. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.
/**
 * \class document::MapFieldValue
 * \ingroup fieldvalue
 *
 * \brief A fieldvalue containing fieldvalue <-> weight mappings.
 */
#pragma once

#include "fieldvalue.h"
#include <vespa/document/datatype/mapdatatype.h>
#include <vespa/vespalib/util/polymorphicarrays.h>

namespace document {

class MapFieldValue : public FieldValue
{
private:
    typedef vespalib::IArrayT<FieldValue> IArray;
    const MapDataType *_type;
    size_t             _count;
    IArray::UP         _keys;
    IArray::UP         _values;
    std::vector<bool>  _present;
    bool               _altered;

    virtual bool addValue(const FieldValue& fv);
    virtual bool containsValue(const FieldValue& fv) const { return contains(fv); }
    virtual bool removeValue(const FieldValue& fv) { return erase(fv); }
    bool checkAndRemove(const FieldValue& key, fieldvalue::ModificationStatus status,
                        bool wasModified, std::vector<const FieldValue*>& keysToRemove) const;
    fieldvalue::ModificationStatus onIterateNested(PathRange nested, fieldvalue::IteratorHandler & handler) const override;
    // Utility method to avoid constant explicit casting
    const MapDataType& getMapType() const { return *_type; }

    void verifyKey(const FieldValue & key) const __attribute__((noinline));
    void verifyValue(const FieldValue & value) const __attribute__((noinline));
    size_t nextPresent(size_t index) const {
        for (; index < _present.size() && !_present[index]; ++index);
        return index;
    }
public:
    typedef std::unique_ptr<MapFieldValue> UP;
    class iterator {
        typedef std::pair<FieldValue *, FieldValue *> pair;
    public:
        iterator(MapFieldValue & map, size_t index) : _map(&map), _index(index) { }
        bool operator == (const iterator & rhs) const { return _map == rhs._map && _index == rhs._index; }
        bool operator != (const iterator & rhs) const { return _map != rhs._map || _index != rhs._index; }
        iterator& operator++() { _index = _map->nextPresent(_index+1); return *this; }
        const pair & operator * () const { setCurr(); return _current; }
        const pair * operator -> () const { setCurr(); return &_current; }
        size_t offset() const { return _index; }
    private:
        void setCurr() const {
             _current.first  = &(*_map->_keys)[_index];
             _current.second = &(*_map->_values)[_index];
        }
        MapFieldValue *_map;
        size_t         _index;
        mutable pair   _current;
    };
    class const_iterator {
        typedef std::pair<const FieldValue *, const FieldValue *> pair;
    public:
        const_iterator(const MapFieldValue & map, size_t index) : _map(&map), _index(index) { }
        bool operator == (const const_iterator & rhs) const { return _map == rhs._map && _index == rhs._index; }
        bool operator != (const const_iterator & rhs) const { return _map != rhs._map || _index != rhs._index; }
        const_iterator& operator++() {  _index = _map->nextPresent(_index+1); return *this; }
        const pair & operator * () const { setCurr(); return _current; }
        const pair * operator -> () const { setCurr(); return &_current; }
    private:
        void setCurr() const {
             _current.first  = &(*_map->_keys)[_index];
             _current.second = &(*_map->_values)[_index];
        }
        const MapFieldValue *_map;
        size_t               _index;
        mutable pair         _current;
    };

    MapFieldValue(const DataType &mapType);
    virtual ~MapFieldValue();

    MapFieldValue(const MapFieldValue & rhs);
    MapFieldValue & operator = (const MapFieldValue & rhs);
    void swap(MapFieldValue & rhs);

    void accept(FieldValueVisitor &visitor) override { visitor.visit(*this); }
    void accept(ConstFieldValueVisitor &visitor) const override { visitor.visit(*this); }

    /**
     * These methods for insertion will check for uniqueness.
     **/
    bool put(FieldValue::UP key, FieldValue::UP value);
    bool put(const FieldValue& key, const FieldValue& value);
    bool insertVerify(const FieldValue& key, const FieldValue& value);
    bool insert(FieldValue::UP key, FieldValue::UP value);

    /**
     * This will just append the values to the set, assuming that the
     * new entry is not a duplicate.
     **/
    void push_back(FieldValue::UP key, FieldValue::UP value);
    void push_back(const FieldValue & key, const FieldValue & value);

    FieldValue::UP get(const FieldValue& key) const;
    bool erase(const FieldValue& key);
    bool contains(const FieldValue& key) const;

    // CollectionFieldValue methods kept for compatability's sake
    virtual bool isEmpty() const { return _count == 0; }
    virtual size_t size() const { return _count; }
    virtual void clear();
    void reserve(size_t sz);
    void resize(size_t sz);

    fieldvalue::ModificationStatus iterateNestedImpl(PathRange nested, fieldvalue::IteratorHandler & handler,
                                                     const FieldValue& complexFieldValue) const;

    // FieldValue implementation
    FieldValue& assign(const FieldValue&) override;
    MapFieldValue* clone() const override { return new MapFieldValue(*this); }
    int compare(const FieldValue&) const override;
    void print(std::ostream& out, bool verbose, const std::string& indent) const override;
    bool hasChanged() const override;
    const DataType *getDataType() const override { return _type; }
    void printXml(XmlOutputStream& out) const override;

    const_iterator begin() const { return const_iterator(*this, nextPresent(0)); }
    iterator begin() { return iterator(*this, nextPresent(0)); }

    const_iterator end() const { return const_iterator(*this, _present.size()); }
    iterator end() { return iterator(*this, _present.size()); }

    const_iterator find(const FieldValue& fv) const;
    iterator find(const FieldValue& fv);

    FieldValue::UP createKey() const {
        return getMapType().getKeyType().createFieldValue();
    }
    FieldValue::UP createValue() const {
        return getMapType().getValueType().createFieldValue();
    }

    DECLARE_IDENTIFIABLE_ABSTRACT(MapFieldValue);
};

}  // namespace document

