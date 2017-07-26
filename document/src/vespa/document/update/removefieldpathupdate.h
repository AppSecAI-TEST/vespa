// Copyright 2017 Yahoo Holdings. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.
#pragma once

#include "fieldpathupdate.h"

namespace document {

class RemoveFieldPathUpdate : public FieldPathUpdate
{
public:
    /** For deserialization */
    RemoveFieldPathUpdate();

    RemoveFieldPathUpdate(stringref fieldPath, stringref whereClause = stringref());

    FieldPathUpdate* clone() const override { return new RemoveFieldPathUpdate(*this); }

    bool operator==(const FieldPathUpdate& other) const override;
    void print(std::ostream& out, bool verbose, const std::string& indent) const override;

    DECLARE_IDENTIFIABLE(RemoveFieldPathUpdate);
    ACCEPT_UPDATE_VISITOR;

private:
    uint8_t getSerializedType() const override { return RemoveMagic; }
    void deserialize(const DocumentTypeRepo& repo, const DataType& type,
                     ByteBuffer& buffer, uint16_t version) override;

    std::unique_ptr<fieldvalue::IteratorHandler> getIteratorHandler(Document &, const DocumentTypeRepo &) const override;
};

} // ns document
