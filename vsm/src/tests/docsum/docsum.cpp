// Copyright 2016 Yahoo Inc. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.
#include <vespa/vespalib/testkit/testapp.h>

#include <vector>
#include <vespa/document/fieldvalue/fieldvalues.h>
#include <vespa/vsm/common/docsum.h>
#include <vespa/vsm/vsm/flattendocsumwriter.h>
#include <vespa/vsm/vsm/slimefieldwriter.h>

using namespace document;

namespace vsm {

template <typename T>
class Vector : public std::vector<T>
{
public:
    Vector<T> & add(T v) { this->push_back(v); return *this; }
};

typedef Vector<std::string>  StringList;
typedef Vector<std::pair<std::string, int32_t> > WeightedStringList;


class TestDocument : public vsm::Document
{
private:
    std::vector<FieldValueContainer> _fields;

public:
    TestDocument(const search::DocumentIdT & docId, size_t numFields) : vsm::Document(docId, numFields), _fields(numFields) {}
    virtual bool setField(FieldIdT fId, document::FieldValue::UP fv) {
        if (fId < _fields.size()) {
            _fields[fId].reset(fv.release());
            return true;
        }
        return false;
    }
    virtual const document::FieldValue * getField(FieldIdT fId) const {
        if (fId < _fields.size()) {
            return _fields[fId].get();
        }
        return NULL;
    }
};


class DocsumTest : public vespalib::TestApp
{
private:
    ArrayFieldValue createFieldValue(const StringList & fv);
    WeightedSetFieldValue createFieldValue(const WeightedStringList & fv);

    void assertFlattenDocsumWriter(const FieldValue & fv, const std::string & exp) {
        FlattenDocsumWriter fdw;
        assertFlattenDocsumWriter(fdw, fv, exp);
    }
    void assertFlattenDocsumWriter(FlattenDocsumWriter & fdw, const FieldValue & fv, const std::string & exp);
    void assertSlimeFieldWriter(const FieldValue & fv, const std::string & exp) {
        SlimeFieldWriter jdw;
        assertSlimeFieldWriter(jdw, fv, exp);
    }
    void assertSlimeFieldWriter(SlimeFieldWriter & jdw, const FieldValue & fv, const std::string & exp);

    void testFlattenDocsumWriter();
    void testSlimeFieldWriter();
    void requireThatSlimeFieldWriterHandlesMap();
    void testDocSumCache();

public:
    int Main();
};

ArrayFieldValue
DocsumTest::createFieldValue(const StringList & fv)
{

    static ArrayDataType type(*DataType::STRING);
    ArrayFieldValue afv(type);
    for (size_t i = 0; i < fv.size(); ++i) {
        afv.add(StringFieldValue(fv[i]));
    }
    return afv;
}

WeightedSetFieldValue
DocsumTest::createFieldValue(const WeightedStringList & fv)
{
    static WeightedSetDataType type(*DataType::STRING, false, false);
    WeightedSetFieldValue wsfv(type);
    for (size_t i = 0; i < fv.size(); ++i) {
        wsfv.add(StringFieldValue(fv[i].first), fv[i].second);
    }
    return wsfv;
}

void
DocsumTest::assertFlattenDocsumWriter(FlattenDocsumWriter & fdw, const FieldValue & fv, const std::string & exp)
{
    FieldPath empty;
    fv.iterateNested(empty.begin(), empty.end(), fdw);
    std::string actual(fdw.getResult().getBuffer(), fdw.getResult().getPos());
    EXPECT_EQUAL(actual, exp);
}

void
DocsumTest::assertSlimeFieldWriter(SlimeFieldWriter & jdw, const FieldValue & fv, const std::string & exp)
{
    jdw.convert(fv);

    vespalib::Slime gotSlime;
    vespalib::Memory serialized(jdw.out());
    size_t decodeRes = vespalib::slime::BinaryFormat::decode(serialized, gotSlime);
    ASSERT_EQUAL(decodeRes, serialized.size);

    vespalib::Slime expSlime;
    size_t used = vespalib::slime::JsonFormat::decode(exp, expSlime);
    EXPECT_EQUAL(exp.size(), used);
    if (!(expSlime == gotSlime)) {
        fprintf(stderr, "exp type: %u\n", expSlime.get().type().getId());
        fprintf(stderr, "got type: %u\n", gotSlime.get().type().getId());
        fprintf(stderr, "exp double: %.17g\n", expSlime.get().asDouble());
        fprintf(stderr, "got double: %.17g\n", gotSlime.get().asDouble());
    }
    EXPECT_EQUAL(expSlime, gotSlime);
}

void
DocsumTest::testFlattenDocsumWriter()
{
    { // basic tests
        assertFlattenDocsumWriter(StringFieldValue("foo bar"), "foo bar");
        assertFlattenDocsumWriter(RawFieldValue("foo bar"), "foo bar");
        assertFlattenDocsumWriter(LongFieldValue(123456789), "123456789");
        assertFlattenDocsumWriter(createFieldValue(StringList().add("foo bar").add("baz").add(" qux ")),
                                  "foo bar baz  qux ");
    }
    { // test mulitple invokations
        FlattenDocsumWriter fdw("#");
        assertFlattenDocsumWriter(fdw, StringFieldValue("foo"), "foo");
        assertFlattenDocsumWriter(fdw, StringFieldValue("bar"), "foo#bar");
        fdw.clear();
        assertFlattenDocsumWriter(fdw, StringFieldValue("baz"), "baz");
        assertFlattenDocsumWriter(fdw, StringFieldValue("qux"), "baz qux");
    }
    { // test resizing
        FlattenDocsumWriter fdw("#");
        EXPECT_EQUAL(fdw.getResult().getPos(), 0u);
        EXPECT_EQUAL(fdw.getResult().getLength(), 32u);
        assertFlattenDocsumWriter(fdw, StringFieldValue("aaaabbbbccccddddeeeeffffgggghhhh"),
                                                        "aaaabbbbccccddddeeeeffffgggghhhh");
        EXPECT_EQUAL(fdw.getResult().getPos(), 32u);
        EXPECT_EQUAL(fdw.getResult().getLength(), 32u);
        assertFlattenDocsumWriter(fdw, StringFieldValue("aaaa"),
                                                        "aaaabbbbccccddddeeeeffffgggghhhh#aaaa");
        EXPECT_EQUAL(fdw.getResult().getPos(), 37u);
        EXPECT_TRUE(fdw.getResult().getLength() >= 37u);
        fdw.clear();
        EXPECT_EQUAL(fdw.getResult().getPos(), 0u);
        EXPECT_TRUE(fdw.getResult().getLength() >= 37u);
    }
}

void
DocsumTest::testSlimeFieldWriter()
{
    { // basic types
        assertSlimeFieldWriter(LongFieldValue(123456789), "123456789");
        assertSlimeFieldWriter(DoubleFieldValue(12.34), "12.34");
        assertSlimeFieldWriter(StringFieldValue("foo bar"), "\"foo bar\"");
    }
    { // collection field values
        assertSlimeFieldWriter(createFieldValue(StringList().add("foo").add("bar").add("baz")),
                               "[\"foo\",\"bar\",\"baz\"]");
        assertSlimeFieldWriter(createFieldValue(WeightedStringList().add(std::make_pair("bar", 20)).
                                                                     add(std::make_pair("baz", 30)).
                                                                     add(std::make_pair("foo", 10))),
                               "[{item:\"bar\",weight:20},{item:\"baz\",weight:30},{item:\"foo\",weight:10}]");
    }
    { // struct field value
        StructDataType subType("substruct");
        Field fd("d", 0, *DataType::STRING, true);
        Field fe("e", 1, *DataType::STRING, true);
        subType.addField(fd);
        subType.addField(fe);
        StructFieldValue subValue(subType);
        subValue.setValue(fd, StringFieldValue("baz"));
        subValue.setValue(fe, StringFieldValue("qux"));

        StructDataType type("struct");
        Field fa("a", 0, *DataType::STRING, true);
        Field fb("b", 1, *DataType::STRING, true);
        Field fc("c", 2, subType, true);
        type.addField(fa);
        type.addField(fb);
        type.addField(fc);
        StructFieldValue value(type);
        value.setValue(fa, StringFieldValue("foo"));
        value.setValue(fb, StringFieldValue("bar"));
        value.setValue(fc, subValue);


        { // select a subset and then all
            SlimeFieldWriter jdw;
            DocsumFieldSpec::FieldIdentifierVector fields;
            fields.push_back(DocsumFieldSpec::FieldIdentifier(
                            0, *type.buildFieldPath("a")));
            fields.push_back(DocsumFieldSpec::FieldIdentifier(
                            0, *type.buildFieldPath("c.e")));
            jdw.setInputFields(fields);
            assertSlimeFieldWriter(jdw, value, "{\"a\":\"foo\",\"c\":{\"e\":\"qux\"}}");
            jdw.clear();
            assertSlimeFieldWriter(jdw, value, "{\"a\":\"foo\",\"b\":\"bar\",\"c\":{\"d\":\"baz\",\"e\":\"qux\"}}");
        }

    { // multiple invocations
        SlimeFieldWriter jdw;
        assertSlimeFieldWriter(jdw, StringFieldValue("foo"), "\"foo\"");
        jdw.clear();
        assertSlimeFieldWriter(jdw, StringFieldValue("bar"), "\"bar\"");
        jdw.clear();
        assertSlimeFieldWriter(jdw, StringFieldValue("baz"), "\"baz\"");
    }

    }
}

void
DocsumTest::requireThatSlimeFieldWriterHandlesMap()
{
    { // map<string, string>
        MapDataType mapType(*DataType::STRING, *DataType::STRING);
        MapFieldValue mapfv(mapType);
        EXPECT_TRUE(mapfv.put(StringFieldValue("k1"), StringFieldValue("v1")));
        EXPECT_TRUE(mapfv.put(StringFieldValue("k2"), StringFieldValue("v2")));
        assertSlimeFieldWriter(mapfv, "[{\"key\":\"k1\",\"value\":\"v1\"},{\"key\":\"k2\",\"value\":\"v2\"}]");
    }
    { // map<string, struct>
        StructDataType structType("struct");
        Field fa("a", 0, *DataType::STRING, true);
        Field fb("b", 1, *DataType::STRING, true);
        structType.addField(fa);
        structType.addField(fb);
        StructFieldValue structValue(structType);
        structValue.setValue(fa, StringFieldValue("foo"));
        structValue.setValue(fb, StringFieldValue("bar"));
        MapDataType mapType(*DataType::STRING, structType);
        MapFieldValue mapfv(mapType);
        EXPECT_TRUE(mapfv.put(StringFieldValue("k1"), structValue));
        { // select a subset and then all
            SlimeFieldWriter jdw;
            DocsumFieldSpec::FieldIdentifierVector fields;
            fields.push_back(DocsumFieldSpec::FieldIdentifier(0, *mapType.buildFieldPath("value.b")));
            jdw.setInputFields(fields);
            assertSlimeFieldWriter(jdw, mapfv, "[{\"key\":\"k1\",\"value\":{\"b\":\"bar\"}}]");
            fields[0] = DocsumFieldSpec::FieldIdentifier(0, *mapType.buildFieldPath("{k1}.a"));
            jdw.clear();
            jdw.setInputFields(fields);
            assertSlimeFieldWriter(jdw, mapfv, "[{\"key\":\"k1\",\"value\":{\"a\":\"foo\"}}]");
            jdw.clear(); // all fields implicit
            assertSlimeFieldWriter(jdw, mapfv, "[{\"key\":\"k1\",\"value\":{\"a\":\"foo\",\"b\":\"bar\"}}]");
        }
    }
}

int
DocsumTest::Main()
{
    TEST_INIT("docsum_test");

    testFlattenDocsumWriter();
    testSlimeFieldWriter();
    requireThatSlimeFieldWriterHandlesMap();

    TEST_DONE();
}

}

TEST_APPHOOK(vsm::DocsumTest);

