//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
#include <memory>
#include <functional>
#include <string>
#include <infiniband/verbs.h>
//---------------------------------------------------------------------------------------------------------------------
namespace ibutils
{
    // Unique pointer wrappers for ibverbs types - code borrowed from dacs repo
    using unique_ibv_device_list = std::unique_ptr<ibv_device*, std::function<void(ibv_device**)>>;
    using unique_ibv_context = std::unique_ptr<ibv_context, std::function<void(ibv_context*)>>;
    using unique_ibv_comp_channel = std::unique_ptr<ibv_comp_channel, std::function<void(ibv_comp_channel*)>>;
    using unique_ibv_pd = std::unique_ptr<ibv_pd, std::function<void(ibv_pd*)>>;
    using unique_ibv_mr = std::unique_ptr<ibv_mr, std::function<void(ibv_mr*)>>;
    using unique_ibv_cq = std::unique_ptr<ibv_cq, std::function<void(ibv_cq*)>>;
    using unique_ibv_srq = std::unique_ptr<ibv_srq, std::function<void(ibv_srq*)>>;
    using unique_ibv_qp = std::unique_ptr<ibv_qp, std::function<void(ibv_qp*)>>;
    using shared_ibv_qp = std::shared_ptr<ibv_qp>;

    void FreeIbvDeviceList(/*[in]*/ ibv_device** ibvDevice);
    void FreeIbvContext(/*[in]*/ ibv_context* ibvContext);
    void FreeIbvCompChannel(/*[in]*/ ibv_comp_channel* ibvCompChannel);
    void FreeIbvPd(/*[in]*/ ibv_pd* ibvPd);
    void FreeIbvMr(/*[in]*/ ibv_mr* ibvMr);
    void FreeIbvCq(/*[in]*/ ibv_cq* ibvCq);
    void FreeIbvSrq(/*[in]*/ ibv_srq* ibvSrq);
    void FreeIbvQp(/*[in]*/ ibv_qp* ibvQp);

    // Fetch local IB device info.
    // Select the first available active IB device/port.
    // TODO: Add support to read a specific device from configuration
    bool GetIBDeviceInfo(
        /*[out]*/ uint32_t& ibvLid,
        /*[out]*/ uint8_t& ibvPort,
        /*[out]*/ ibv_mtu& ibvMtu,
        /*[out]*/ unique_ibv_context& ibvContext);

    // Initialize QP and transition it to duplex send and receive state
    bool SetupQPForSendAndReceive(
        /*[in]*/ ibv_qp* ibvQp,
        /*[in]*/ uint8_t localIBPort,
        /*[in]*/ ibv_mtu ibMtu,
        /*[in]*/ uint32_t remoteQpn,
        /*[in]*/ uint32_t remoteIBLid,
        /*[in]*/ uint8_t remoteIBPort);

    // RNR (receiver not ready) NACK timer 25->61.44 millis.
    constexpr int IB_MIN_RNR_TIMER = 25;

    // Timeout for retransmission 17->0.536 seconds.
    constexpr int IB_TIMEOUT = 17;

    // Retry count.
    constexpr int IB_RETRY_CNT = 7;

    // Number of retries on RNR from remote.
    constexpr int IB_RNR_RETRY = 6;

    // Buffer size for sending/receiving IB requests and responses.
    // This should be atleast as big as the largest permitted request/response size.
    // Allowing 4MB for chunk/data size and 10KB for request/response protobuf message.
    constexpr int IB_BUFFER_SIZE = 32ll * 1024 * 1024 + 10 * 1024;
}
//---------------------------------------------------------------------------------------------------------------------