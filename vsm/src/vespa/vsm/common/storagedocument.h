// Copyright 2016 Yahoo Inc. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.
#pragma once

#include <vespa/document/fieldvalue/document.h>
#include <vespa/vsm/common/document.h>

namespace vsm {

typedef vespalib::CloneablePtr<document::FieldValue> FieldValueContainer;
typedef document::FieldPath FieldPath; // field path to navigate a field value
typedef std::vector<FieldPath> FieldPathMapT; // map from field id to field path
typedef std::shared_ptr<FieldPathMapT> SharedFieldPathMap;

class StorageDocument : public Document {
public:
    typedef vespalib::LinkedPtr<StorageDocument> LP;

    class SubDocument {
    public:
        SubDocument() : _fieldValue(NULL) {}
        SubDocument(document::FieldValue *fv, FieldPath::const_iterator it, FieldPath::const_iterator mt) :
                _fieldValue(fv),
                _it(it),
                _mt(mt)
        { }

        const document::FieldValue *getFieldValue() const { return _fieldValue; }
        void setFieldValue(document::FieldValue *fv) { _fieldValue = fv; }
        FieldPath::const_iterator begin() const { return _it; }
        FieldPath::const_iterator end() const { return _mt; }
        void swap(SubDocument &rhs) {
            std::swap(_fieldValue, rhs._fieldValue);
            std::swap(_it, rhs._it);
            std::swap(_mt, rhs._mt);
        }
    private:
        document::FieldValue *_fieldValue;
        FieldPath::const_iterator _it;
        FieldPath::const_iterator _mt;
    };
public:
    StorageDocument(document::Document::UP doc, const SharedFieldPathMap &fim, size_t fieldNoLimit);
    StorageDocument(const StorageDocument &) = delete;
    StorageDocument & operator = (const StorageDocument &) = delete;
    ~StorageDocument();

    const document::Document &docDoc() const { return *_doc; }
    bool valid() const { return _doc.get() != NULL; }
    const SubDocument &getComplexField(FieldIdT fId) const;
    const document::FieldValue *getField(FieldIdT fId) const override;
    bool setField(FieldIdT fId, document::FieldValue::UP fv) override ;
private:
    document::Document::UP _doc;
    SharedFieldPathMap     _fieldMap;
    mutable std::vector<SubDocument> _cachedFields;
    mutable std::vector<document::FieldValue::UP> _backedFields;
};

}

