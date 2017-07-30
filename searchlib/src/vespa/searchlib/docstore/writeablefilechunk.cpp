// Copyright 2017 Yahoo Holdings. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.

#include "writeablefilechunk.h"
#include "data_store_file_chunk_stats.h"
#include "summaryexceptions.h"
#include <vespa/vespalib/util/closuretask.h>
#include <vespa/vespalib/util/array.hpp>
#include <vespa/vespalib/data/fileheader.h>
#include <vespa/searchlib/common/fileheadercontext.h>
#include <vespa/vespalib/stllike/hash_map.hpp>
#include <vespa/vespalib/objects/nbostream.h>

#include <vespa/log/log.h>
LOG_SETUP(".search.writeablefilechunk");

using vespalib::makeTask;
using vespalib::makeClosure;
using vespalib::FileHeader;
using vespalib::make_string;
using vespalib::LockGuard;
using vespalib::MonitorGuard;
using vespalib::nbostream;
using vespalib::IllegalHeaderException;
using vespalib::GenerationHandler;
using search::common::FileHeaderContext;

namespace search {

namespace {

const uint64_t Alignment = 4096;
const uint64_t headerAlign = 4096;

}

/*
 * Information about serialized chunk written to .dat file but not yet
 * synced.
 */
class PendingChunk
{
    vespalib::nbostream _idx; // Serialized chunk for .idx file
    uint64_t _lastSerial;
    uint64_t _dataOffset;
    uint32_t _dataLen;
public:
    typedef std::shared_ptr<PendingChunk> SP;
    PendingChunk(uint64_t lastSerial, uint64_t dataOffset, uint32_t dataLen);
    ~PendingChunk();
    vespalib::nbostream & getSerializedIdx() { return _idx; }
    const vespalib::nbostream & getSerializedIdx() const { return _idx; }
    uint64_t getDataOffset() const { return _dataOffset; }
    uint32_t getDataLen() const { return _dataLen; }
    uint32_t getIdxLen() const { return _idx.size(); }
    uint64_t getLastSerial() const { return _lastSerial; }
};

class ProcessedChunk
{
public:
    typedef std::unique_ptr<ProcessedChunk> UP;
    ProcessedChunk(uint32_t chunkId, uint32_t alignment)
            : _chunkId(chunkId),
              _payLoad(0),
              _buf(0ul, alignment)
    { }
    void setPayLoad() { _payLoad = _buf.getDataLen(); }
    uint32_t getPayLoad() const { return _payLoad; }
    uint32_t getChunkId() const { return _chunkId; }
    const vespalib::DataBuffer & getBuf() const { return _buf; }
    vespalib::DataBuffer & getBuf() { return _buf; }
private:
    uint32_t             _chunkId;
    uint32_t             _payLoad;
    vespalib::DataBuffer _buf;
};

WriteableFileChunk::
WriteableFileChunk(vespalib::ThreadExecutor &executor,
                   FileId fileId, NameId nameId,
                   const vespalib::string &baseName,
                   SerialNum initialSerialNum,
                   uint32_t docIdLimit,
                   const Config &config,
                   const TuneFileSummary &tune,
                   const FileHeaderContext &fileHeaderContext,
                   const IBucketizer * bucketizer,
                   bool skipCrcOnRead)
    : FileChunk(fileId, nameId, baseName, tune, bucketizer, skipCrcOnRead),
      _config(config),
      _serialNum(initialSerialNum),
      _frozen(false),
      _lock(),
      _writeLock(),
      _flushLock(),
      _dataFile(_dataFileName.c_str()),
      _idxFile(_idxFileName.c_str()),
      _chunkMap(),
      _pendingChunks(),
      _pendingIdx(0),
      _pendingDat(0),
      _currentDiskFootprint(0),
      _nextChunkId(1),
      _active(new Chunk(0, Chunk::Config(config.getMaxChunkBytes()))),
      _alignment(1),
      _granularity(1),
      _maxChunkSize(0x100000),
      _firstChunkIdToBeWritten(0),
      _writeTaskIsRunning(false),
      _executor(executor),
      _bucketMap(bucketizer)
{
    _docIdLimit = docIdLimit;
    if (tune._write.getWantDirectIO()) {
        _dataFile.EnableDirectIO();
    }
    if (tune._write.getWantSyncWrites()) {
        _dataFile.EnableSyncWrites();
        _idxFile.EnableSyncWrites();
    }
    if (_dataFile.OpenReadWrite()) {
        readDataHeader();
        if (_dataHeaderLen == 0) {
            writeDataHeader(fileHeaderContext);
        }
        _dataFile.SetPosition(_dataFile.GetSize());
        if (tune._write.getWantDirectIO()) {
            if (!_dataFile.GetDirectIORestrictions(_alignment, _granularity, _maxChunkSize)) {
                LOG(debug, "Direct IO setup failed for file %s due to %s",
                           _dataFile.GetFileName(), _dataFile.getLastErrorString().c_str());
            }
        }
        if (_idxFile.OpenReadWrite()) {
            readIdxHeader();
            if (_idxHeaderLen == 0) {
                _idxHeaderLen = writeIdxHeader(fileHeaderContext, _docIdLimit, _idxFile);
            }
            _idxFile.SetPosition(_idxFile.GetSize());
        } else {
            _dataFile.Close();
            throw SummaryException("Failed opening idx file", _idxFile, VESPA_STRLOC);
        }
    } else {
        throw SummaryException("Failed opening data file", _dataFile, VESPA_STRLOC);
    }
    _firstChunkIdToBeWritten = _active->getId();
    updateCurrentDiskFootprint();
}

WriteableFileChunk::~WriteableFileChunk()
{
    if (!frozen()) {
        if (_active->size() || _active->count()) {
            flush(true, _serialNum);
        }
        freeze();
    }
    // This is a wild stab at fixing bug 6348143.
    // If it works it indicates something bad with the filesystem.
    if (_dataFile.IsOpened()) {
        if (! _dataFile.Sync()) {
            assert(false);
        }
    }
    if (_idxFile.IsOpened()) {
        if (! _idxFile.Sync()) {
            assert(false);
        }
    }
}

size_t
WriteableFileChunk::updateLidMap(const LockGuard &guard, ISetLid &ds, uint64_t serialNum, uint32_t docIdLimit)
{
    size_t sz = FileChunk::updateLidMap(guard, ds, serialNum, docIdLimit);
    _nextChunkId = _chunkInfo.size();
    _active.reset( new Chunk(_nextChunkId++, Chunk::Config(_config.getMaxChunkBytes())));
    _serialNum = getLastPersistedSerialNum();
    _firstChunkIdToBeWritten = _active->getId();
    setDiskFootprint(0);
    _chunkInfo.reserve(0x10000);
    return sz;
}

void
WriteableFileChunk::restart(uint32_t nextChunkId)
{
    _executor.execute(makeTask(makeClosure(this, &WriteableFileChunk::fileWriter, nextChunkId)));
}

namespace {

LidInfoWithLidV::const_iterator
find_first(LidInfoWithLidV::const_iterator begin, uint32_t chunkId) {
   for ( ; begin->getChunkId() != chunkId; ++begin);
   return begin;
}

LidInfoWithLidV::const_iterator
seek_past(LidInfoWithLidV::const_iterator begin, LidInfoWithLidV::const_iterator end, uint32_t chunkId) {
   for ( ; (begin < end) && (begin->getChunkId() == chunkId); begin++);
   return begin;
}

}

void
WriteableFileChunk::read(LidInfoWithLidV::const_iterator begin, size_t count, IBufferVisitor & visitor) const
{
    if (count == 0) { return; }
    if (!frozen()) {
        vespalib::hash_map<uint32_t, ChunkInfo> chunksOnFile;
        {
            LockGuard guard(_lock);
            for (size_t i(0); i < count; i++) {
                const LidInfoWithLid & li = *(begin + i);
                uint32_t chunk = li.getChunkId();
                if ((chunk >= _chunkInfo.size()) || !_chunkInfo[chunk].valid()) {
                    ChunkMap::const_iterator found = _chunkMap.find(chunk);
                    vespalib::ConstBufferRef buffer;
                    if (found != _chunkMap.end()) {
                        buffer = found->second->getLid(li.getLid());
                    } else {
                        assert(chunk == _active->getId());
                        buffer = _active->getLid(li.getLid());
                    }
                    visitor.visit(li.getLid(), buffer);
                } else {
                    chunksOnFile[chunk] = _chunkInfo[chunk];
                }
            }
        }
        for (auto & it : chunksOnFile) {
            LidInfoWithLidV::const_iterator first = find_first(begin, it.first);
            LidInfoWithLidV::const_iterator last = seek_past(first, begin + count, it.first);
            FileChunk::read(first, last - first, it.second, visitor);
        }
    } else {
        FileChunk::read(begin, count, visitor);
    }
}

ssize_t
WriteableFileChunk::read(uint32_t lid, SubChunkId chunkId, vespalib::DataBuffer & buffer) const
{
    ChunkInfo chunkInfo;
    if (!frozen()) {
        LockGuard guard(_lock);
        if ((chunkId >= _chunkInfo.size()) || !_chunkInfo[chunkId].valid()) {
            ChunkMap::const_iterator found = _chunkMap.find(chunkId);
            if (found != _chunkMap.end()) {
                return found->second->read(lid, buffer);
            } else {
                assert(chunkId == _active->getId());
                return _active->read(lid, buffer);
            }
        }
        chunkInfo = _chunkInfo[chunkId];
    } else {
        chunkInfo = _chunkInfo[chunkId];
    }
    return FileChunk::read(lid, chunkId, chunkInfo, buffer);
}

void
WriteableFileChunk::internalFlush(uint32_t chunkId, uint64_t serialNum)
{
    Chunk * active(NULL);
    {
        LockGuard guard(_lock);
        active = _chunkMap[chunkId].get();
    }

    ProcessedChunk::UP tmp(new ProcessedChunk(chunkId, _alignment));
    if (_alignment > 1) {
        tmp->getBuf().ensureFree(active->getMaxPackSize(_config.getCompression()) + _alignment - 1);
    }
    active->pack(serialNum, tmp->getBuf(), _config.getCompression());
    tmp->setPayLoad();
    if (_alignment > 1) {
        const size_t padAfter((_alignment - tmp->getPayLoad() % _alignment) % _alignment);
        memset(tmp->getBuf().getFree(), 0, padAfter);
        tmp->getBuf().moveFreeToData(padAfter);
    }
    {
        LockGuard innerGuard(_lock);
        setDiskFootprint(FileChunk::getDiskFootprint() + tmp->getBuf().getDataLen());
    }
    enque(std::move(tmp));
}

void
WriteableFileChunk::enque(ProcessedChunk::UP tmp)
{
    LOG(debug, "enqueing %p", tmp.get());
    MonitorGuard guard(_writeMonitor);
    _writeQ.push_back(std::move(tmp));
    if (_writeTaskIsRunning == false) {
        _writeTaskIsRunning = true;
        uint32_t nextChunkId = _firstChunkIdToBeWritten;
        guard.signal();
        guard.unlock();
        restart(nextChunkId);
    } else {
        guard.signal();
    }
}

namespace {

const std::vector<char> Padding(Alignment, '\0');

size_t
getAlignedStartPos(FastOS_File & file)
{
    ssize_t startPos(file.GetPosition());
    assert(startPos == file.GetSize());
    if (startPos & (Alignment-1)) {
        FastOS_File align(file.GetFileName());
        if (align.OpenWriteOnly()) {
            align.SetPosition(startPos);
            ssize_t toWrite(Alignment - (startPos & (Alignment-1)));
            ssize_t written = align.Write2(&Padding[0], toWrite);
            if (written == toWrite) {
                align.Sync();
                file.SetPosition(align.GetSize());
                startPos = file.GetPosition();
             } else {
                throw SummaryException(
                    make_string("Failed writing %ld bytes to dat file. Only %ld written", toWrite, written),
                    align, VESPA_STRLOC);
             }
        } else {
            throw SummaryException("Failed opening dat file for padding for direct io.", align, VESPA_STRLOC);
        }
    }
    assert((startPos & (Alignment-1)) == 0);
    return startPos;
}

}

WriteableFileChunk::ProcessedChunkQ
WriteableFileChunk::drainQ()
{
    ProcessedChunkQ newChunks;
    MonitorGuard guard(_writeMonitor);
    newChunks.swap(_writeQ);
    if ( ! newChunks.empty() ) {
        guard.broadcast();
    }
    return newChunks;
}

void
WriteableFileChunk::insertChunks(ProcessedChunkMap & orderedChunks, ProcessedChunkQ & newChunks, const uint32_t nextChunkId)
{
    (void) nextChunkId;
    for (auto &chunk : newChunks) {
        if (chunk.get() != 0) {
            assert(chunk->getChunkId() >= nextChunkId);
            assert(orderedChunks.find(chunk->getChunkId()) == orderedChunks.end());
            orderedChunks[chunk->getChunkId()] = std::move(chunk);
        } else {
            orderedChunks[std::numeric_limits<uint32_t>::max()] = ProcessedChunk::UP();
        }
    }
}

WriteableFileChunk::ProcessedChunkQ
WriteableFileChunk::fetchNextChain(ProcessedChunkMap & orderedChunks, const uint32_t firstChunkId)
{
    ProcessedChunkQ chunks;
    while (!orderedChunks.empty() &&
           ((orderedChunks.begin()->first == (firstChunkId+chunks.size())) ||
            (orderedChunks.begin()->second.get() == NULL)))
    {
        chunks.push_back(std::move(orderedChunks.begin()->second));
        orderedChunks.erase(orderedChunks.begin());
    }
    return chunks;
}

ChunkMeta
WriteableFileChunk::computeChunkMeta(const LockGuard & guard,
                                     const GenerationHandler::Guard & bucketizerGuard,
                                     size_t offset, const ProcessedChunk & tmp, const Chunk & active)
{
    (void) guard;
    size_t dataLen = tmp.getBuf().getDataLen();
    const ChunkMeta cmeta(offset, tmp.getPayLoad(), active.getLastSerial(), active.count());
    assert((size_t(tmp.getBuf().getData())%_alignment) == 0);
    assert((dataLen%_alignment) == 0);
    PendingChunk::SP pcsp;
    pcsp.reset(new PendingChunk(active.getLastSerial(), offset, dataLen));
    PendingChunk &pc(*pcsp.get());
    nbostream &os(pc.getSerializedIdx());
    cmeta.serialize(os);
    BucketDensityComputer bucketMap(_bucketizer);
    for (const Chunk::Entry & e : active.getLids()) {
        bucketMap.recordLid(bucketizerGuard, e.getLid(), e.netSize());
        _bucketMap.recordLid(bucketizerGuard, e.getLid(), e.netSize());
        LidMeta lm(e.getLid(), e.netSize());
        lm.serialize(os);
    }
    addNumBuckets(bucketMap.getNumBuckets());
    setNumUniqueBuckets(_bucketMap.getNumBuckets());

    _pendingDat += pc.getDataLen();
    _pendingIdx += pc.getIdxLen();
    _pendingChunks.push_back(pcsp);
    return cmeta;
}

ChunkMetaV
WriteableFileChunk::computeChunkMeta(ProcessedChunkQ & chunks, size_t startPos, size_t & sz, bool & done)
{
    ChunkMetaV cmetaV;
    cmetaV.reserve(chunks.size());
    uint64_t lastSerial(_lastPersistedSerialNum);
    (void) lastSerial;
    LockGuard guard(_lock);

    if (!_pendingChunks.empty()) {
        const PendingChunk::SP pcsp(_pendingChunks.back());
        const PendingChunk &pc(*pcsp.get());
        assert(pc.getLastSerial() >= lastSerial);
        lastSerial = pc.getLastSerial();
    }

    GenerationHandler::Guard bucketizerGuard = _bucketMap.getGuard();
    for (size_t i(0), m(chunks.size()); i < m; i++) {
        if (chunks[i].get() != 0) {
            const ProcessedChunk & chunk = *chunks[i];
            const ChunkMeta cmeta(computeChunkMeta(guard, bucketizerGuard, startPos + sz, chunk, *_chunkMap[chunk.getChunkId()]));
            sz += chunk.getBuf().getDataLen();
            cmetaV.push_back(cmeta);
            assert(cmeta.getLastSerial() >= lastSerial);
            lastSerial = cmeta.getLastSerial();
        } else {
            done = true;
            assert((i+1) == chunks.size());
            chunks.resize(i);
            assert(i == chunks.size());
        }
    }
    return cmetaV;
}

void
WriteableFileChunk::writeData(const ProcessedChunkQ & chunks, size_t sz)
{
    vespalib::DataBuffer buf(0ul, _alignment);
    buf.ensureFree(sz);
    for (const ProcessedChunk::UP & chunk : chunks) {
        buf.writeBytes(chunk->getBuf().getData(), chunk->getBuf().getDataLen());
    }

    LockGuard guard(_writeLock);
    ssize_t wlen = _dataFile.Write2(buf.getData(), buf.getDataLen());
    if (wlen != static_cast<ssize_t>(buf.getDataLen())) {
        throw SummaryException(make_string("Failed writing %ld bytes to dat file. Only %ld written",
                                           buf.getDataLen(), wlen),
                               _idxFile, VESPA_STRLOC);
    }
    updateCurrentDiskFootprint();
}

void
WriteableFileChunk::updateChunkInfo(const ProcessedChunkQ & chunks, const ChunkMetaV & cmetaV, size_t sz)
{
    MonitorGuard guard(_lock);
    size_t nettoSz(sz);
    for (size_t i(0); i < chunks.size(); i++) {
        const ProcessedChunk & chunk = *chunks[i];
        assert(_chunkMap.find(chunk.getChunkId()) == _chunkMap.begin());
        const Chunk & active = *_chunkMap.begin()->second;
        if (active.getId() >= _chunkInfo.size()) {
            _chunkInfo.resize(active.getId()+1);
        }
        const ChunkMeta & cmeta(cmetaV[i]);
        _chunkInfo[active.getId()] = ChunkInfo(cmeta.getOffset(), chunk.getPayLoad(), cmeta.getLastSerial());
        nettoSz += active.size();
        _chunkMap.erase(_chunkMap.begin());
    }
    setDiskFootprint(FileChunk::getDiskFootprint() - nettoSz);
    guard.broadcast();
}

void
WriteableFileChunk::fileWriter(const uint32_t firstChunkId)
{
    LOG(debug, "Starting the filewriter with chunkid = %d", firstChunkId);
    uint32_t nextChunkId(firstChunkId);
    bool done(false);
    {
        ProcessedChunkQ newChunks(drainQ());
        if ( ! newChunks.empty()) {
            insertChunks(_orderedChunks, newChunks, nextChunkId);
            ProcessedChunkQ chunks(fetchNextChain(_orderedChunks, nextChunkId));
            nextChunkId += chunks.size();
            
            size_t sz(0);
            ChunkMetaV cmetaV(computeChunkMeta(chunks, getAlignedStartPos(_dataFile), sz, done));
            writeData(chunks, sz);
            updateChunkInfo(chunks, cmetaV, sz);
            LOG(spam, "bucket spread = '%3.2f'", getBucketSpread());
        }
    }
    LOG(debug,
        "Stopping the filewriter with startchunkid = %d and ending chunkid = %d done=%d",
        firstChunkId, nextChunkId, done);
    MonitorGuard guard(_writeMonitor);
    if (done) {
        assert(_writeQ.empty());
        assert(_chunkMap.empty());
        for (const ChunkInfo & cm : _chunkInfo) {
            (void) cm;
            assert(cm.valid() && cm.getSize() != 0);
        }
        _writeTaskIsRunning = false;
        guard.broadcast();
    } else {
        if (_writeQ.empty()) {
            _firstChunkIdToBeWritten = nextChunkId;
            _writeTaskIsRunning = false;
        } else {
            _writeTaskIsRunning = true;
            guard.unlock();
            restart(nextChunkId);
        }
    }
}

fastos::TimeStamp
WriteableFileChunk::getModificationTime() const
{
    LockGuard guard(_lock);
    return _modificationTime;
}

void
WriteableFileChunk::freeze()
{
    if (!frozen()) {
        waitForAllChunksFlushedToDisk();
        enque(ProcessedChunk::UP());
        _executor.sync();
        {
            MonitorGuard guard(_writeMonitor);
            while (_writeTaskIsRunning) {
                guard.wait(10);
            }
            assert(_writeQ.empty());
        }
        {
            MonitorGuard guard(_lock);
            setDiskFootprint(getDiskFootprint(guard));
            _frozen = true;
        }
        _dataFile.Close();
        _idxFile.Close();
        _bucketMap = BucketDensityComputer(_bucketizer);
    }
}

size_t
WriteableFileChunk::getDiskFootprint() const
{
    if (frozen()) {
        return FileChunk::getDiskFootprint();
    } else {
        // Double checked locking.
        MonitorGuard guard(_lock);
        return getDiskFootprint(guard);
    }
}

size_t
WriteableFileChunk::getDiskFootprint(const vespalib::MonitorGuard & guard) const
{
    (void) guard;
    assert(guard.monitors(_lock));
    return frozen()
           ? FileChunk::getDiskFootprint()
           : _currentDiskFootprint + FileChunk::getDiskFootprint();
}

size_t
WriteableFileChunk::getMemoryFootprint() const
{
    size_t sz(0);
    LockGuard guard(_lock);
    for (const auto & it : _chunkMap) {
        sz += it.second->size();
    }
    sz += _pendingIdx + _pendingDat;
    return sz + FileChunk::getMemoryFootprint();
}

size_t
WriteableFileChunk::getMemoryMetaFootprint() const
{
    constexpr size_t mySizeWithoutMyParent(sizeof(*this) - sizeof(FileChunk));
    return mySizeWithoutMyParent + FileChunk::getMemoryMetaFootprint();
}

MemoryUsage
WriteableFileChunk::getMemoryUsage() const
{
    LockGuard guard(_lock);
    MemoryUsage result;
    for (const auto &chunk : _chunkMap) {
        result.merge(chunk.second->getMemoryUsage());
    }
    size_t pendingBytes = _pendingIdx + _pendingDat;
    result.incAllocatedBytes(pendingBytes);
    result.incUsedBytes(pendingBytes);
    result.merge(FileChunk::getMemoryUsage());
    return result;
}

int32_t WriteableFileChunk::flushLastIfNonEmpty(bool force)
{
    int32_t chunkId(-1);
    MonitorGuard guard(_lock);
    for (bool ready(false); !ready;) {
        if (_chunkMap.size() > 1000) {
            LOG(debug, "Summary write overload at least 1000 outstanding chunks. Suspending.");
            guard.wait();
            LOG(debug, "Summary write overload eased off. Commencing.");
        } else {
            ready = true;
        }
    }
    if ( force || ! _active->empty()) {
        chunkId = _active->getId();
        _chunkMap[chunkId] = std::move(_active);
        assert(_nextChunkId < LidInfo::getChunkIdLimit());
        _active.reset(new Chunk(_nextChunkId++, Chunk::Config(_config.getMaxChunkBytes())));
    }
    return chunkId;
}

void
WriteableFileChunk::flush(bool block, uint64_t syncToken)
{
    int32_t chunkId = flushLastIfNonEmpty(syncToken > _serialNum);
    if (chunkId >= 0) {
        setSerialNum(syncToken);
        _executor.execute(makeTask(makeClosure(this,
                                           &WriteableFileChunk::internalFlush,
                                           static_cast<uint32_t>(chunkId),
                                           _serialNum)));
    } else {
        if (block) {
            MonitorGuard guard(_lock);
            if (!_chunkMap.empty()) {
                chunkId = _chunkMap.rbegin()->first;
            }
        }
    }
    if (block) {
        _executor.sync();
        waitForChunkFlushedToDisk(chunkId);
    }
}

void
WriteableFileChunk::waitForDiskToCatchUpToNow() const
{
    int32_t chunkId(-1);
    {
        MonitorGuard guard(_lock);
        if (!_chunkMap.empty()) {
            chunkId = _chunkMap.rbegin()->first;
        }
    }
    waitForChunkFlushedToDisk(chunkId);
}

void
WriteableFileChunk::waitForChunkFlushedToDisk(uint32_t chunkId) const
{
    MonitorGuard guard(_lock);
    while( _chunkMap.find(chunkId) != _chunkMap.end() ) {
        guard.wait();
    }
}

void
WriteableFileChunk::waitForAllChunksFlushedToDisk() const
{
    MonitorGuard guard(_lock);
    while( ! _chunkMap.empty() ) {
        guard.wait();
    }
}

LidInfo
WriteableFileChunk::append(uint64_t serialNum,
                           uint32_t lid,
                           const void * buffer,
                           size_t len)
{
    assert( !frozen() );
    if ( ! _active->hasRoom(len)) {
        flush(false, _serialNum);
    }
    assert(serialNum >= _serialNum);
    _serialNum = serialNum;
    _addedBytes += adjustSize(len);
    size_t oldSz(_active->size());
    LidMeta lm = _active->append(lid, buffer, len);
    setDiskFootprint(FileChunk::getDiskFootprint() - oldSz + _active->size());
    return LidInfo(getFileId().getId(), _active->getId(), lm.size());
}


void
WriteableFileChunk::readDataHeader()
{
    int64_t fSize(_dataFile.GetSize());
    try {
        FileHeader h;
        _dataHeaderLen = h.readFile(_dataFile);
        _dataFile.SetPosition(_dataHeaderLen);
    } catch (IllegalHeaderException &e) {
        _dataFile.SetPosition(0);
        try {
            FileHeader::FileReader fr(_dataFile);
            uint32_t header2Len = FileHeader::readSize(fr);
            if (header2Len <= fSize)
                e.throwSelf(); // header not truncated
        } catch (IllegalHeaderException &e2) {
        }
        if (fSize > 0) {
            // Truncate file (dropping header) if cannot even read
            // header length, or if header has been truncated.
            _dataFile.SetPosition(0);
            _dataFile.SetSize(0);
            assert(_dataFile.GetSize() == 0);
            assert(_dataFile.GetPosition() == 0);
            LOG(warning,
                "Truncated file chunk data %s due to truncated file header",
                _dataFile.GetFileName());
        }
    }
}


void
WriteableFileChunk::readIdxHeader()
{
    int64_t fSize(_idxFile.GetSize());
    try {
        FileHeader h;
        _idxHeaderLen = h.readFile(_idxFile);
        _idxFile.SetPosition(_idxHeaderLen);
        _docIdLimit = readDocIdLimit(h);
    } catch (IllegalHeaderException &e) {
        _idxFile.SetPosition(0);
        try {
            FileHeader::FileReader fr(_idxFile);
            uint32_t header2Len = FileHeader::readSize(fr);
            if (header2Len <= fSize)
                e.throwSelf(); // header not truncated
        } catch (IllegalHeaderException &e2) {
        }
        if (fSize > 0) {
            // Truncate file (dropping header) if cannot even read
            // header length, or if header has been truncated.
            _idxFile.SetPosition(0);
            _idxFile.SetSize(0);
            assert(_idxFile.GetSize() == 0);
            assert(_idxFile.GetPosition() == 0);
            LOG(warning,
                "Truncated file chunk index %s due to truncated file header",
                _idxFile.GetFileName());
        }
    }
}


void
WriteableFileChunk::writeDataHeader(const FileHeaderContext &fileHeaderContext)
{
    typedef FileHeader::Tag Tag;
    FileHeader h(headerAlign);
    assert(_dataFile.IsOpened());
    assert(_dataFile.IsWriteMode());
    assert(_dataFile.GetPosition() == 0);
    fileHeaderContext.addTags(h, _dataFile.GetFileName());
    h.putTag(Tag("desc", "Log data store chunk data"));
    _dataHeaderLen = h.writeFile(_dataFile);
}


uint64_t
WriteableFileChunk::writeIdxHeader(const FileHeaderContext &fileHeaderContext, uint32_t docIdLimit, FastOS_FileInterface &file)
{
    typedef FileHeader::Tag Tag;
    FileHeader h;
    assert(file.IsOpened());
    assert(file.IsWriteMode());
    assert(file.GetPosition() == 0);
    fileHeaderContext.addTags(h, file.GetFileName());
    h.putTag(Tag("desc", "Log data store chunk index"));
    writeDocIdLimit(h, docIdLimit);
    return h.writeFile(file);
}


bool
WriteableFileChunk::needFlushPendingChunks(uint64_t serialNum, uint64_t datFileLen) {
    MonitorGuard guard(_lock);
    return needFlushPendingChunks(guard, serialNum, datFileLen);
}

bool
WriteableFileChunk::needFlushPendingChunks(const MonitorGuard & guard, uint64_t serialNum, uint64_t datFileLen)
{
    (void) guard;
    assert(guard.monitors(_lock));
    if (_pendingChunks.empty())
        return false;
    const PendingChunk::SP pcsp(_pendingChunks.front());
    const PendingChunk &pc(*pcsp.get());
    if (pc.getLastSerial() > serialNum)
        return false;
    bool datWritten = datFileLen >= pc.getDataOffset() + pc.getDataLen();
    if (pc.getLastSerial() < serialNum) {
        assert(datWritten);
        return true;
    }
    return datWritten;
}

void
WriteableFileChunk::updateCurrentDiskFootprint() {
    _currentDiskFootprint = _idxFile.getSize() + _dataFile.getSize();
}

/*
 * Called by writeExecutor thread for now.
 */
void
WriteableFileChunk::flushPendingChunks(uint64_t serialNum) {
    LockGuard flushGuard(_flushLock);
    if (frozen())
        return;
    uint64_t datFileLen = _dataFile.getSize();
    fastos::TimeStamp timeStamp(fastos::ClockSystem::now());
    if (needFlushPendingChunks(serialNum, datFileLen)) {
        timeStamp = unconditionallyFlushPendingChunks(flushGuard, serialNum, datFileLen);
    }
    LockGuard guard(_lock);
    _modificationTime = std::max(timeStamp, _modificationTime);
}

fastos::TimeStamp
WriteableFileChunk::unconditionallyFlushPendingChunks(const vespalib::LockGuard &flushGuard, uint64_t serialNum, uint64_t datFileLen)
{
    (void) flushGuard;
    assert(flushGuard.locks(_flushLock));
    if ( ! _dataFile.Sync()) {
        throw SummaryException("Failed fsync of dat file", _dataFile, VESPA_STRLOC);
    }
    nbostream os;
    uint64_t lastSerial = 0;
    {
        MonitorGuard guard(_lock);
        lastSerial = _lastPersistedSerialNum;
        for (;;) {
            if (!needFlushPendingChunks(guard, serialNum, datFileLen))
                break;
            PendingChunk::SP pcsp;
            pcsp.swap(_pendingChunks.front());
            _pendingChunks.pop_front();
            const PendingChunk &pc(*pcsp.get());
            assert(_pendingIdx >= pc.getIdxLen());
            assert(_pendingDat >= pc.getDataLen());
            assert(datFileLen >= pc.getDataOffset() + pc.getDataLen());
            assert(lastSerial <= pc.getLastSerial());
            _pendingIdx -= pc.getIdxLen();
            _pendingDat -= pc.getDataLen();
            lastSerial = pc.getLastSerial();
            const nbostream &os2(pc.getSerializedIdx());
            os.write(os2.c_str(), os2.size());
        }
    }
    fastos::TimeStamp timeStamp(fastos::ClockSystem::now());
    ssize_t wlen = _idxFile.Write2(os.c_str(), os.size());
    updateCurrentDiskFootprint();

    if (wlen != static_cast<ssize_t>(os.size())) {
        throw SummaryException(make_string("Failed writing %ld bytes to idx file. Only wrote %ld bytes ", os.size(), wlen), _idxFile, VESPA_STRLOC);
    }
    if ( ! _idxFile.Sync()) {
        throw SummaryException("Failed fsync of idx file", _idxFile, VESPA_STRLOC);
    }
    if (_lastPersistedSerialNum < lastSerial) {
        _lastPersistedSerialNum = lastSerial;
    }
    return timeStamp;
}

DataStoreFileChunkStats
WriteableFileChunk::getStats() const
{
    DataStoreFileChunkStats stats = FileChunk::getStats();
    uint64_t serialNum = getSerialNum();
    return DataStoreFileChunkStats(stats.diskUsage(), stats.diskBloat(), stats.maxBucketSpread(),
                                   serialNum, stats.lastFlushedSerialNum(), stats.docIdLimit(), stats.nameId());
};

PendingChunk::PendingChunk(uint64_t lastSerial, uint64_t dataOffset, uint32_t dataLen)
    : _idx(),
      _lastSerial(lastSerial),
      _dataOffset(dataOffset),
      _dataLen(dataLen)
{ }

PendingChunk::~PendingChunk() { }

} // namespace search
