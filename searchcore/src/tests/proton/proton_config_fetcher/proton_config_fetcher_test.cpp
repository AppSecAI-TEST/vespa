// Copyright 2017 Yahoo Holdings. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.

#include <map>
#include <vespa/config-attributes.h>
#include <vespa/config-imported-fields.h>
#include <vespa/config-indexschema.h>
#include <vespa/config-rank-profiles.h>
#include <vespa/config-summary.h>
#include <vespa/config-summarymap.h>
#include <vespa/fileacquirer/config-filedistributorrpc.h>
#include <vespa/searchcore/proton/server/bootstrapconfig.h>
#include <vespa/searchcore/proton/server/bootstrapconfigmanager.h>
#include <vespa/searchcore/proton/server/documentdbconfigmanager.h>
#include <vespa/searchcore/proton/server/proton_config_fetcher.h>
#include <vespa/searchcore/proton/server/proton_config_snapshot.h>
#include <vespa/searchcore/proton/server/i_proton_configurer.h>
#include <vespa/searchsummary/config/config-juniperrc.h>
#include <vespa/searchcore/config/config-ranking-constants.h>
#include <vespa/vespalib/testkit/testapp.h>
#include <vespa/vespalib/util/varholder.h>
#include <mutex>

using namespace config;
using namespace proton;
using namespace vespa::config::search::core;
using namespace vespa::config::search::summary;
using namespace vespa::config::search;
using namespace cloud::config::filedistribution;

using config::ConfigUri;
using document::DocumentTypeRepo;
using document::DocumenttypesConfig;
using document::DocumenttypesConfigBuilder;
using search::TuneFileDocumentDB;
using std::map;
using vespalib::VarHolder;

struct DoctypeFixture {
    using UP = std::unique_ptr<DoctypeFixture>;
    AttributesConfigBuilder attributesBuilder;
    RankProfilesConfigBuilder rankProfilesBuilder;
    RankingConstantsConfigBuilder rankingConstantsBuilder;
    IndexschemaConfigBuilder indexschemaBuilder;
    SummaryConfigBuilder summaryBuilder;
    SummarymapConfigBuilder summarymapBuilder;
    JuniperrcConfigBuilder juniperrcBuilder;
    ImportedFieldsConfigBuilder importedFieldsBuilder;
};

struct ConfigTestFixture {
    const std::string configId;
    ProtonConfigBuilder protonBuilder;
    DocumenttypesConfigBuilder documenttypesBuilder;
    FiledistributorrpcConfigBuilder filedistBuilder;
    map<std::string, DoctypeFixture::UP> dbConfig;
    ConfigSet set;
    IConfigContext::SP context;
    int idcounter;

    ConfigTestFixture(const std::string & id)
        : configId(id),
          protonBuilder(),
          documenttypesBuilder(),
          filedistBuilder(),
          dbConfig(),
          set(),
          context(new ConfigContext(set)),
          idcounter(-1)
    {
        set.addBuilder(configId, &protonBuilder);
        set.addBuilder(configId, &documenttypesBuilder);
        set.addBuilder(configId, &filedistBuilder);
        addDocType("_alwaysthere_");
    }

    DoctypeFixture *addDocType(const std::string &name, bool isGlobal = false) {
        DocumenttypesConfigBuilder::Documenttype dt;
        dt.bodystruct = -1270491200;
        dt.headerstruct = 306916075;
        dt.id = idcounter--;
        dt.name = name;
        dt.version = 0;
        documenttypesBuilder.documenttype.push_back(dt);

        ProtonConfigBuilder::Documentdb db;
        db.inputdoctypename = name;
        db.configid = configId + "/" + name;
        db.global = isGlobal;
        protonBuilder.documentdb.push_back(db);

        DoctypeFixture::UP fixture = std::make_unique<DoctypeFixture>();
        set.addBuilder(db.configid, &fixture->attributesBuilder);
        set.addBuilder(db.configid, &fixture->rankProfilesBuilder);
        set.addBuilder(db.configid, &fixture->rankingConstantsBuilder);
        set.addBuilder(db.configid, &fixture->indexschemaBuilder);
        set.addBuilder(db.configid, &fixture->summaryBuilder);
        set.addBuilder(db.configid, &fixture->summarymapBuilder);
        set.addBuilder(db.configid, &fixture->juniperrcBuilder);
        set.addBuilder(db.configid, &fixture->importedFieldsBuilder);
        return dbConfig.emplace(std::make_pair(name, std::move(fixture))).first->second.get();
    }

    void removeDocType(const std::string & name)
    {
        for (DocumenttypesConfigBuilder::DocumenttypeVector::iterator it(documenttypesBuilder.documenttype.begin()),
                                                                      mt(documenttypesBuilder.documenttype.end());
             it != mt;
             it++) {
            if ((*it).name.compare(name) == 0) {
                documenttypesBuilder.documenttype.erase(it);
                break;
            }
        }

        for (ProtonConfigBuilder::DocumentdbVector::iterator it(protonBuilder.documentdb.begin()),
                                                             mt(protonBuilder.documentdb.end());
             it != mt;
             it++) {
            if ((*it).inputdoctypename.compare(name) == 0) {
                protonBuilder.documentdb.erase(it);
                break;
            }
        }
    }

    bool configEqual(const std::string & name, DocumentDBConfig::SP dbc) {
        auto itr = dbConfig.find(name);
        ASSERT_TRUE(itr != dbConfig.end());
        const auto *fixture = itr->second.get();
        return (fixture->attributesBuilder == dbc->getAttributesConfig() &&
                fixture->rankProfilesBuilder == dbc->getRankProfilesConfig() &&
                fixture->indexschemaBuilder == dbc->getIndexschemaConfig() &&
                fixture->summaryBuilder == dbc->getSummaryConfig() &&
                fixture->summarymapBuilder == dbc->getSummarymapConfig() &&
                fixture->juniperrcBuilder == dbc->getJuniperrcConfig());
    }

    bool configEqual(BootstrapConfig::SP bootstrapConfig) {
        return (protonBuilder == bootstrapConfig->getProtonConfig() &&
                documenttypesBuilder == bootstrapConfig->getDocumenttypesConfig());
    }

    BootstrapConfig::SP getBootstrapConfig(int64_t generation) const {
        return BootstrapConfig::SP(new BootstrapConfig(generation,
                                                       BootstrapConfig::DocumenttypesConfigSP(new DocumenttypesConfig(documenttypesBuilder)),
                                                       DocumentTypeRepo::SP(new DocumentTypeRepo(documenttypesBuilder)),
                                                       BootstrapConfig::ProtonConfigSP(new ProtonConfig(protonBuilder)),
                                                       std::make_shared<FiledistributorrpcConfig>(),
                                                       std::make_shared<TuneFileDocumentDB>()));
    }

    void reload() { context->reload(); }
};

struct ProtonConfigOwner : public proton::IProtonConfigurer
{
    mutable std::mutex _mutex;
    volatile bool _configured;
    VarHolder<std::shared_ptr<ProtonConfigSnapshot>> _config;

    ProtonConfigOwner() : _configured(false), _config() { }
    bool waitUntilConfigured(int timeout) {
        FastOS_Time timer;
        timer.SetNow();
        while (timer.MilliSecsToNow() < timeout) {
            if (getConfigured())
                return true;
            FastOS_Thread::Sleep(100);
        }
        return getConfigured();
    }
    virtual void reconfigure(std::shared_ptr<ProtonConfigSnapshot> cfg) override {
        std::unique_lock<std::mutex> guard(_mutex);
        _config.set(cfg);
        _configured = true;
    }
    bool getConfigured() const {
        std::unique_lock<std::mutex> guard(_mutex);
        return _configured;
    }
    BootstrapConfig::SP getBootstrapConfig() {
        auto snapshot = _config.get();
        return snapshot->getBootstrapConfig();
    }
    DocumentDBConfig::SP getDocumentDBConfig(const vespalib::string &name)
    {
        auto snapshot = _config.get();
        auto &dbcs = snapshot->getDocumentDBConfigs();
        auto dbitr = dbcs.find(DocTypeName(name));
        if (dbitr != dbcs.end()) {
            return dbitr->second;
        } else {
            return DocumentDBConfig::SP();
        }
    }
};

TEST_F("require that bootstrap config manager creats correct key set", BootstrapConfigManager("foo")) {
    const ConfigKeySet set(f1.createConfigKeySet());
    ASSERT_EQUAL(3u, set.size());
    ConfigKey protonKey(ConfigKey::create<ProtonConfig>("foo"));
    ConfigKey dtKey(ConfigKey::create<DocumenttypesConfig>("foo"));
    ASSERT_TRUE(set.find(protonKey) != set.end());
    ASSERT_TRUE(set.find(dtKey) != set.end());
}

TEST_FFF("require that bootstrap config manager updates config", ConfigTestFixture("search"),
                                                                 BootstrapConfigManager(f1.configId),
                                                                 ConfigRetriever(f2.createConfigKeySet(), f1.context)) {
    f2.update(f3.getBootstrapConfigs());
    ASSERT_TRUE(f1.configEqual(f2.getConfig()));
    f1.protonBuilder.rpcport = 9010;
    ASSERT_FALSE(f1.configEqual(f2.getConfig()));
    f1.reload();
    f2.update(f3.getBootstrapConfigs());
    ASSERT_TRUE(f1.configEqual(f2.getConfig()));

    f1.addDocType("foobar");
    ASSERT_FALSE(f1.configEqual(f2.getConfig()));
    f1.reload();
    f2.update(f3.getBootstrapConfigs());
    ASSERT_TRUE(f1.configEqual(f2.getConfig()));
}

DocumentDBConfig::SP
getDocumentDBConfig(ConfigTestFixture &f, DocumentDBConfigManager &mgr)
{
    ConfigRetriever retriever(mgr.createConfigKeySet(), f.context);
    mgr.forwardConfig(f.getBootstrapConfig(1));
    mgr.update(retriever.getBootstrapConfigs()); // Cheating, but we only need the configs
    return mgr.getConfig();
}

TEST_FF("require that documentdb config manager subscribes for config",
        ConfigTestFixture("search"),
        DocumentDBConfigManager(f1.configId + "/typea", "typea")) {
    f1.addDocType("typea");
    const ConfigKeySet keySet(f2.createConfigKeySet());
    ASSERT_EQUAL(8u, keySet.size());
    ASSERT_TRUE(f1.configEqual("typea", getDocumentDBConfig(f1, f2)));
}

TEST_FF("require that documentdb config manager builds schema with imported attribute fields",
        ConfigTestFixture("search"),
        DocumentDBConfigManager(f1.configId + "/typea", "typea"))
{
    auto *docType = f1.addDocType("typea");
    docType->importedFieldsBuilder.attribute.resize(1);
    docType->importedFieldsBuilder.attribute[0].name = "imported";

    const auto &schema = getDocumentDBConfig(f1, f2)->getSchemaSP();
    EXPECT_EQUAL(1u, schema->getNumImportedAttributeFields());
    EXPECT_EQUAL("imported", schema->getImportedAttributeFields()[0].getName());
}

TEST_FFF("require that proton config fetcher follows changes to bootstrap",
         ConfigTestFixture("search"),
         ProtonConfigOwner(),
         ProtonConfigFetcher(ConfigUri(f1.configId, f1.context), f2, 60000)) {
    f3.start();
    ASSERT_TRUE(f2._configured);
    ASSERT_TRUE(f1.configEqual(f2.getBootstrapConfig()));
    f2._configured = false;
    f1.protonBuilder.rpcport = 9010;
    f1.reload();
    ASSERT_TRUE(f2.waitUntilConfigured(120000));
    ASSERT_TRUE(f1.configEqual(f2.getBootstrapConfig()));
    f3.close();
}

TEST_FFF("require that proton config fetcher follows changes to doctypes",
         ConfigTestFixture("search"),
         ProtonConfigOwner(),
         ProtonConfigFetcher(ConfigUri(f1.configId, f1.context), f2, 60000)) {
    f3.start();

    f2._configured = false;
    f1.addDocType("typea");
    f1.reload();
    ASSERT_TRUE(f2.waitUntilConfigured(60000));
    ASSERT_TRUE(f1.configEqual(f2.getBootstrapConfig()));

    f2._configured = false;
    f1.removeDocType("typea");
    f1.reload();
    ASSERT_TRUE(f2.waitUntilConfigured(60000));
    ASSERT_TRUE(f1.configEqual(f2.getBootstrapConfig()));
    f3.close();
}

TEST_FFF("require that proton config fetcher reconfigures dbowners",
         ConfigTestFixture("search"),
         ProtonConfigOwner(),
         ProtonConfigFetcher(ConfigUri(f1.configId, f1.context), f2, 60000)) {
    f3.start();
    ASSERT_FALSE(f2.getDocumentDBConfig("typea"));

    // Add db and verify that config for db is provided
    f2._configured = false;
    f1.addDocType("typea");
    f1.reload();
    ASSERT_TRUE(f2.waitUntilConfigured(60000));
    ASSERT_TRUE(f1.configEqual(f2.getBootstrapConfig()));
    ASSERT_TRUE(static_cast<bool>(f2.getDocumentDBConfig("typea")));
    ASSERT_TRUE(f1.configEqual("typea", f2.getDocumentDBConfig("typea")));

    // Remove and verify that config for db is no longer provided
    f2._configured = false;
    f1.removeDocType("typea");
    f1.reload();
    ASSERT_TRUE(f2.waitUntilConfigured(60000));
    ASSERT_FALSE(f2.getDocumentDBConfig("typea"));
    f3.close();
}

TEST_FF("require that lid space compaction is disabled for globally distributed document type",
        ConfigTestFixture("search"),
        DocumentDBConfigManager(f1.configId + "/global", "global"))
{
    f1.addDocType("global", true);
    auto config = getDocumentDBConfig(f1, f2);
    EXPECT_TRUE(config->getMaintenanceConfigSP()->getLidSpaceCompactionConfig().isDisabled());
}

TEST_FF("require that prune removed documents interval can be set based on age",
        ConfigTestFixture("test"),
        DocumentDBConfigManager(f1.configId + "/test", "test"))
{
    f1.protonBuilder.pruneremoveddocumentsage = 2000;
    f1.protonBuilder.pruneremoveddocumentsinterval = 0;
    f1.addDocType("test");
    auto config = getDocumentDBConfig(f1, f2);
    EXPECT_EQUAL(20, config->getMaintenanceConfigSP()->getPruneRemovedDocumentsConfig().getInterval());
}

TEST_MAIN() { TEST_RUN_ALL(); }
