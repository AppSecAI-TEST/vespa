// Copyright 2017 Yahoo Holdings. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.

#include "reference_attribute.h"
#include "reference_attribute_saver.h"
#include "attributesaver.h"
#include "readerbase.h"
#include <vespa/searchlib/datastore/unique_store_builder.h>
#include <vespa/searchlib/datastore/datastore.hpp>
#include <vespa/searchlib/datastore/unique_store.hpp>
#include <vespa/searchlib/common/i_gid_to_lid_mapper_factory.h>
#include <vespa/searchlib/common/i_gid_to_lid_mapper.h>
#include <vespa/vespalib/data/fileheader.h>

namespace search {
namespace attribute {

namespace {

// minimum dead bytes in unique store before consider compaction
constexpr size_t DEAD_BYTES_SLACK = 0x10000u;

const vespalib::string uniqueValueCountTag = "uniqueValueCount";

uint64_t
extractUniqueValueCount(const vespalib::GenericHeader &header)
{
    return (header.hasTag(uniqueValueCountTag)) ? header.getTag(uniqueValueCountTag).asInteger() : 0u;
}

}

ReferenceAttribute::ReferenceAttribute(const vespalib::stringref baseFileName,
                                       const Config & cfg)
    : NotImplementedAttribute(baseFileName, cfg),
      _store(),
      _indices(getGenerationHolder()),
      _cachedUniqueStoreMemoryUsage(),
      _gidToLidMapperFactory()
{
    setEnum(true);
    enableEnumeratedSave(true);
}

ReferenceAttribute::~ReferenceAttribute()
{
}

void
ReferenceAttribute::onAddDocs(DocId limit)
{
    _indices.reserve(limit);
}

bool
ReferenceAttribute::addDoc(DocId &doc)
{
    bool incGen = _indices.isFull();
    doc = _indices.size();
    _indices.push_back(EntryRef());
    incNumDocs();
    updateUncommittedDocIdLimit(doc);
    if (incGen) {
        incGeneration();
    } else {
        removeAllOldGenerations();
    }
    return true;
}

uint32_t
ReferenceAttribute::clearDoc(DocId doc)
{
    updateUncommittedDocIdLimit(doc);
    assert(doc < _indices.size());
    EntryRef oldRef = _indices[doc];
    if (oldRef.valid()) {
        _indices[doc] = EntryRef();
        _store.remove(oldRef);
        return 1u;
    } else {
        return 0u;
    }
}

void
ReferenceAttribute::removeOldGenerations(generation_t firstUsed)
{
    _store.trimHoldLists(firstUsed);
    getGenerationHolder().trimHoldLists(firstUsed);
}

void
ReferenceAttribute::onGenerationChange(generation_t generation)
{
    _store.freeze();
    _store.transferHoldLists(generation - 1);
    getGenerationHolder().transferHoldLists(generation - 1);
}

void
ReferenceAttribute::onCommit()
{
    // Note: Cost can be reduced if unneeded generation increments are dropped
    incGeneration();
    if (considerCompact(getConfig().getCompactionStrategy())) {
        incGeneration();
        updateStat(true);
    }
}

void
ReferenceAttribute::onUpdateStat()
{
    MemoryUsage total = _store.getMemoryUsage();
    _cachedUniqueStoreMemoryUsage = total;
    total.merge(_indices.getMemoryUsage());
    updateStatistics(getTotalValueCount(), getUniqueValueCount(),
                     total.allocatedBytes(),
                     total.usedBytes(), total.deadBytes(), total.allocatedBytesOnHold());
}

std::unique_ptr<AttributeSaver>
ReferenceAttribute::onInitSave()
{
    vespalib::GenerationHandler::Guard guard(this->getGenerationHandler().
                                             takeGuard());
    return std::make_unique<ReferenceAttributeSaver>
        (std::move(guard),
         createAttributeHeader(),
         getIndicesCopy(getCommittedDocIdLimit()),
         _store);
}

bool
ReferenceAttribute::onLoad()
{
    ReaderBase attrReader(*this);
    bool ok(attrReader.getHasLoadData());
    if (!ok) {
        return false;
    }
    setCreateSerialNum(attrReader.getCreateSerialNum());
    assert(attrReader.getEnumerated());
    assert(!attrReader.hasIdx());
    size_t numDocs(0);
    uint64_t numValues(0);
    numValues = attrReader.getEnumCount();
    numDocs = numValues;
    fileutil::LoadedBuffer::UP udatBuffer(loadUDAT());
    const GenericHeader &header = udatBuffer->getHeader();
    uint32_t uniqueValueCount = extractUniqueValueCount(header);
    assert(uniqueValueCount * sizeof(GlobalId) == udatBuffer->size());
    vespalib::ConstArrayRef<GlobalId> uniques(static_cast<const GlobalId *>(udatBuffer->buffer()), uniqueValueCount);

    auto builder = _store.getBuilder(uniqueValueCount);
    for (const auto &value : uniques) {
        builder.add(value);
    }
    builder.setupRefCounts();
    _indices.clear();
    _indices.unsafe_reserve(numDocs);
    for (uint32_t doc = 0; doc < numDocs; ++doc) {
        uint32_t enumValue = attrReader.getNextEnum();
        _indices.push_back(builder.mapEnumValueToEntryRef(enumValue));
    }
    builder.makeDictionary();
    setNumDocs(numDocs);
    setCommittedDocIdLimit(numDocs);
    incGeneration();
    return true;
}

void
ReferenceAttribute::update(DocId doc, const GlobalId &gid)
{
    updateUncommittedDocIdLimit(doc);
    assert(doc < _indices.size());
    EntryRef oldRef = _indices[doc];
    Reference refToAdd(gid);
    if (_gidToLidMapperFactory) {
        refToAdd.setLid(_gidToLidMapperFactory->getMapper()->mapGidToLid(gid));
    }
    EntryRef newRef = _store.add(refToAdd).ref();
    std::atomic_thread_fence(std::memory_order_release);
    _indices[doc] = newRef;
    if (oldRef.valid()) {
        _store.remove(oldRef);
    }
}

const ReferenceAttribute::Reference *
ReferenceAttribute::getReference(DocId doc)
{
    assert(doc < _indices.size());
    EntryRef ref = _indices[doc];
    if (!ref.valid()) {
        return nullptr;
    } else {
        return &_store.get(ref);
    }
}

bool
ReferenceAttribute::considerCompact(const CompactionStrategy &compactionStrategy)
{
    size_t usedBytes = _cachedUniqueStoreMemoryUsage.usedBytes();
    size_t deadBytes = _cachedUniqueStoreMemoryUsage.deadBytes();
    bool compactMemory = ((deadBytes >= DEAD_BYTES_SLACK) &&
                          (usedBytes * compactionStrategy.getMaxDeadBytesRatio() < deadBytes));
    if (compactMemory) {
        compactWorst();
        return true;
    }
    return false;
}

void
ReferenceAttribute::compactWorst()
{
    datastore::ICompactionContext::UP compactionContext(_store.compactWorst());
    if (compactionContext) {
        compactionContext->compact(vespalib::ArrayRef<EntryRef>(&_indices[0],
                                                                _indices.size()));
    }
}

uint64_t
ReferenceAttribute::getUniqueValueCount() const
{
    return _store.getNumUniques();
}

ReferenceAttribute::IndicesCopyVector
ReferenceAttribute::getIndicesCopy(uint32_t size) const
{
    assert(size <= _indices.size());
    return IndicesCopyVector(&_indices[0], &_indices[0] + size);
}

void
ReferenceAttribute::setGidToLidMapperFactory(std::shared_ptr<IGidToLidMapperFactory> gidToLidMapperFactory)
{
    _gidToLidMapperFactory = std::move(gidToLidMapperFactory);
}

ReferenceAttribute::DocId
ReferenceAttribute::getReferencedLid(DocId doc) const
{
    assert(doc < _indices.size());
    EntryRef ref = _indices[doc];
    if (!ref.valid()) {
        return 0;
    } else {
        return _store.get(ref).lid();
    }
}

void
ReferenceAttribute::notifyGidToLidChange(const GlobalId &gid, DocId referencedLid)
{
    EntryRef ref = _store.find(gid);
    if (ref.valid()) {
        _store.get(ref).setLid(referencedLid);
    }
}

void
ReferenceAttribute::populateReferencedLids()
{
    if (_gidToLidMapperFactory) {
        std::unique_ptr<IGidToLidMapper> mapperUP = _gidToLidMapperFactory->getMapper();
        const IGidToLidMapper &mapper = *mapperUP;
        const auto &store = _store;
        const auto saver = _store.getSaver();
        saver.foreach_key([&store,&mapper](EntryRef ref)
                          {   const Reference &entry = store.get(ref);
                              entry.setLid(mapper.mapGidToLid(entry.gid())); });
    }
}

void
ReferenceAttribute::clearDocs(DocId lidLow, DocId lidLimit)
{
    assert(lidLow <= lidLimit);
    assert(lidLimit <= getNumDocs());
    for (DocId lid = lidLow; lid < lidLimit; ++lid) {
        EntryRef oldRef = _indices[lid];
        if (oldRef.valid()) {
            _indices[lid] = EntryRef();
            _store.remove(oldRef);
        }
    }
}

void
ReferenceAttribute::onShrinkLidSpace()
{
    // References for lids > committedDocIdLimit have been cleared.
    uint32_t committedDocIdLimit = getCommittedDocIdLimit();
    assert(_indices.size() >= committedDocIdLimit);
    _indices.shrink(committedDocIdLimit);
    setNumDocs(committedDocIdLimit);
}

IMPLEMENT_IDENTIFIABLE_ABSTRACT(ReferenceAttribute, AttributeVector);

}

namespace datastore {

using Reference = attribute::ReferenceAttribute::Reference;

template class UniqueStore<Reference, EntryRefT<22>>;
template class UniqueStoreBuilder<Reference, EntryRefT<22>>;
template class UniqueStoreSaver<Reference, EntryRefT<22>>;

}
}
