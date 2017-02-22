// Copyright 2016 Yahoo Inc. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.

#include "positionsdfw.h"
#include "docsumstate.h"
#include "idocsumenvironment.h"
#include <vespa/searchlib/common/location.h>
#include <vespa/vespalib/stllike/asciistream.h>
#include <cmath>

#include <vespa/log/log.h>
LOG_SETUP(".searchlib.docsummary.positionsdfw");

namespace search {
namespace docsummary {

using search::attribute::IAttributeContext;
using search::attribute::IAttributeVector;
using search::attribute::BasicType;
using search::common::Location;

AbsDistanceDFW::AbsDistanceDFW(const vespalib::string & attrName) :
    AttrDFW(attrName)
{ }

uint64_t
AbsDistanceDFW::findMinDistance(uint32_t docid, GetDocsumsState *state)
{
    search::common::Location &location = *state->_parsedLocation;
    const IAttributeVector & attribute(vec(*state));

    uint64_t absdist = std::numeric_limits<int64_t>::max();
    int32_t docx = 0;
    int32_t docy = 0;
    std::vector<IAttributeVector::largeint_t> pos(16);
    uint32_t numValues = attribute.get(docid, &pos[0], pos.size());
    if (numValues > pos.size()) {
        pos.resize(numValues);
        numValues = attribute.get(docid, &pos[0], pos.size());
        assert(numValues <= pos.size());
    }
    for (uint32_t i = 0; i < numValues; i++) {
        int64_t docxy(pos[i]);
        vespalib::geo::ZCurve::decode(docxy, &docx, &docy);
        uint32_t dx;
        if (location.getX() > docx) {
            dx = location.getX() - docx;
        } else {
            dx = docx - location.getX();
        }
        if (location.getXAspect() != 0) {
            dx = ((uint64_t) dx * location.getXAspect()) >> 32;
        }
        uint32_t dy;
        if (location.getY() > docy) {
            dy = location.getY() - docy;
        } else {
            dy = docy - location.getY();
        }
        uint64_t dist2 = dx * (uint64_t) dx +
                         dy * (uint64_t) dy;
        if (dist2 < absdist) {
            absdist = dist2;
        }
    }
    return (uint64_t) std::sqrt((double) absdist);
}

void
AbsDistanceDFW::insertField(uint32_t docid, GeneralResult *, GetDocsumsState *state,
                            ResType type, vespalib::slime::Inserter &target)
{
    bool forceEmpty = true;

    const vespalib::string &locationStr = state->_args.getLocation();
    if (locationStr.size() > 0) {
        if (state->_parsedLocation.get() == NULL) {
            state->_callback.ParseLocation(state);
        }
        assert(state->_parsedLocation.get() != NULL);
        if (state->_parsedLocation->getParseError() == NULL) {
            forceEmpty = false;
        }
    }
    if (forceEmpty) return;

    uint64_t absdist = findMinDistance(docid, state);

    if (type == RES_INT) {
        target.insertLong(absdist);
    } else {
        vespalib::string value = vespalib::stringify(absdist);
        vespalib::Memory data(value.c_str(), value.size());

        if (type == RES_STRING      ||
            type == RES_LONG_STRING ||
            type == RES_XMLSTRING)
        {
            target.insertString(data);
        }
        if (type == RES_LONG_DATA ||
            type == RES_DATA)
        {
            target.insertData(data);
        }
    }
}

//--------------------------------------------------------------------------

PositionsDFW::PositionsDFW(const vespalib::string & attrName) :
    AttrDFW(attrName)
{
}

namespace {
vespalib::asciistream
formatField(const attribute::IAttributeVector &attribute, uint32_t docid, ResType type) {
    vespalib::asciistream target;
    int32_t docx = 0;
    int32_t docy = 0;

    std::vector<IAttributeVector::largeint_t> pos(16);
    uint32_t numValues = attribute.get(docid, &pos[0], pos.size());
    if (numValues > pos.size()) {
        pos.resize(numValues);
        numValues = attribute.get(docid, &pos[0], pos.size());
        assert(numValues <= pos.size());
    }
    LOG(debug, "docid=%d, numValues=%d", docid, numValues);

    bool isShort = !IDocsumFieldWriter::IsBinaryCompatible(type, RES_LONG_STRING);
    for (uint32_t i = 0; i < numValues; i++) {
        int64_t docxy(pos[i]);
        vespalib::geo::ZCurve::decode(docxy, &docx, &docy);
        if (docx == 0 && docy == INT_MIN) {
            LOG(spam, "skipping empty zcurve value");
            continue;
        }
        double degrees_ns = docy;
        degrees_ns /= 1000000.0;
        double degrees_ew = docx;
        degrees_ew /= 1000000.0;

        target << "<position x=\"" << docx << "\" y=\"" << docy << "\"";
        target << " latlong=\"";
        target << vespalib::FloatSpec::fixed;
        if (degrees_ns < 0) {
            target << "S" << (-degrees_ns);
        } else {
            target << "N" << degrees_ns;
        }
        target << ";";
        if (degrees_ew < 0) {
            target << "W" << (-degrees_ew);
        } else {
            target << "E" << degrees_ew;
        }
        target << "\" />";
        if (isShort && target.size() > 30000) {
            target << "<overflow />";
            break;
        }
    }
    return target;
}
}

void
PositionsDFW::insertField(uint32_t docid, GeneralResult *, GetDocsumsState * dsState,
                          ResType type, vespalib::slime::Inserter &target)
{
    vespalib::asciistream val(formatField(vec(*dsState), docid, type));
    target.insertString(vespalib::Memory(val.c_str(), val.size()));
}

//--------------------------------------------------------------------------

PositionsDFW::UP createPositionsDFW(const char *attribute_name,
                                    IAttributeManager *attribute_manager)
{
    PositionsDFW::UP ret;
    if (attribute_manager != NULL) {
        if (!attribute_name) {
            LOG(debug, "createPositionsDFW: missing attribute name '%p'", attribute_name);
            return ret;
        }
        IAttributeContext::UP context = attribute_manager->createContext();
        if (!context.get()) {
            LOG(debug, "createPositionsDFW: could not create context from attribute manager");
            return ret;
        }
        const IAttributeVector *attribute = context->getAttribute(attribute_name);
        if (!attribute) {
            LOG(debug, "createPositionsDFW: could not get attribute '%s' from context", attribute_name);
            return ret;
        }
    }
    ret.reset(new PositionsDFW(attribute_name));
    return ret;
}

AbsDistanceDFW::UP createAbsDistanceDFW(const char *attribute_name,
                                        IAttributeManager *attribute_manager)
{
    AbsDistanceDFW::UP ret;
    if (attribute_manager != NULL) {
        if (!attribute_name) {
            LOG(debug, "createAbsDistanceDFW: missing attribute name '%p'", attribute_name);
            return ret;
        }
        IAttributeContext::UP context = attribute_manager->createContext();
        if (!context.get()) {
            LOG(debug, "createAbsDistanceDFW: could not create context from attribute manager");
            return ret;
        }
        const IAttributeVector *attribute = context->getAttribute(attribute_name);
        if (!attribute) {
            LOG(debug, "createAbsDistanceDFW: could not get attribute '%s' from context", attribute_name);
            return ret;
        }
    }
    ret.reset(new AbsDistanceDFW(attribute_name));
    return ret;
}

}  // namespace docsummary
}  // namespace search
