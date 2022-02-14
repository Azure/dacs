//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "TestBase.h"
#include <IRemoteClient.h>
//---------------------------------------------------------------------------------------------------------------------
class RemoteClientTest : public TestBase
{
    protected:
        RemoteClientTest();
        void SetUp() override;

        template<typename Base, typename T>
        bool IsInstanceOf(const T* pType);

        std::shared_ptr<IRemoteClient> m_RemoteClient;
        const std::string DIR_DELIMITER;
};
//---------------------------------------------------------------------------------------------------------------------