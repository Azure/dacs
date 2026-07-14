//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
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