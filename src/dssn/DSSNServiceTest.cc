/* Copyright (c) 2020 Futurewei Technologies, Inc.
 *
 * All rights reserved.
 */
#include "TestUtil.h"
#include "TestLog.h"
#include "DSSNService.h"
#include "Notifier.h"
#include "MockCluster.h"
#include "RamCloud.h"
#include "ServerId.h"

using namespace RAMCloud;

class DSSNServiceTest : public ::testing::Test {
  public:
    TestLog::Enable logEnabler;
    Context context;
    ServerId serverId;
    ServerList serverList;
    MockCluster cluster;
    Tub<RamCloud> ramcloud;
    ServerConfig dssnConfig;
    DSSN::DSSNService* service;
    Server* dssnServer;

    mutable std::mutex mutex;
    typedef std::unique_lock<std::mutex> Lock;

    explicit DSSNServiceTest()
        : logEnabler()
        , context()
	, serverId(1,1)
        , serverList(&context)
        , cluster(&context)
        , ramcloud()
        , dssnConfig(ServerConfig::forTesting())
        , service()
        , dssnServer()
    {
        Logger::get().setLogLevels(RAMCloud::SILENT_LOG_LEVEL);

        dssnConfig = ServerConfig::forTesting();
        dssnConfig.localLocator = "mock:host=master";
        dssnConfig.services = {WireFormat::MASTER_SERVICE,
				 WireFormat::DSSN_SERVICE,
				 WireFormat::ADMIN_SERVICE};
        dssnServer = cluster.addServer(dssnConfig);
        service = dssnServer->dssnMaster.get();
	//Adding itself to the server list
	serverList.testingAdd({serverId, dssnConfig.localLocator,
	      {WireFormat::DSSN_SERVICE},
	      100, ServerStatus::UP});


    }

    DISALLOW_COPY_AND_ASSIGN(DSSNServiceTest);
};

TEST_F(DSSNServiceTest, notification) {
    const string message = "0123456789abcdefghijklmnopqrstuvwxyz";
    TestLog::reset();
    Notifier::notify(&context, WireFormat::DSSN_NOTIFY_TEST,
		     const_cast<char*>(message.data()),
		     message.length(), serverId);
    EXPECT_EQ("dispatch: Received notify test message",
	      TestLog::get());
}

TEST_F(DSSNServiceTest, notification_invalid_serverid) {
    ServerId invalidId(99);
    const string message(100, 'x');
    TestLog::reset();
    Notifier::notify(&context, WireFormat::DSSN_NOTIFY_TEST,
		     const_cast<char*>(message.data()),
		     message.length(), invalidId);
    EXPECT_EQ("notify: Invalid participate server id: 99",
	      TestLog::get());
}

TEST_F(DSSNServiceTest, notification_send_dssn_info) {
    TestLog::reset();
    DSSN::TxEntry txEntry(1,1);
    WireFormat::DSSNRequestInfoAsync::Request req;
    req.cts = txEntry.getCTS();
    req.pstamp = txEntry.getPStamp();
    req.sstamp = txEntry.getSStamp();;
    req.senderPeerId = serverId.serverId;
    req.txState = txEntry.getTxState();
    char *msg = reinterpret_cast<char *>(&req) + sizeof(WireFormat::Notification::Request);
    Notifier::notify(&context, WireFormat::DSSN_REQUEST_INFO_ASYNC,
                msg, sizeof(req) - sizeof(WireFormat::Notification::Request), *new ServerId(serverId.serverId));
    //expect a reply is sent back to this sender
    EXPECT_NE(string::npos, TestLog::get().find("sendDSSNInfo"));
    EXPECT_NE(string::npos, TestLog::get().find(std::to_string(serverId.serverId)));

}
