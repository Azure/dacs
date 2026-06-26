///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "IBUtils.h"
#include "CommonLogger.h"
//---------------------------------------------------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------------------------------------------------
namespace ibutils
{
    void FreeIbvDeviceList(ibv_device** ibvDevice)
    {
        if (ibvDevice)
        {
            ibv_free_device_list(ibvDevice);
        }
    }

    void FreeIbvContext(ibv_context* ibvContext)
    {
        if (ibvContext)
        {
            int rc = ibv_close_device(ibvContext);
            if (rc != 0)
            {
                LOG_WARN("Failed to close device={}, error={},{}",
                    ibv_get_device_name(ibvContext->device), rc, strerror(errno));
            }
        }
    }

    void FreeIbvCompChannel(ibv_comp_channel* ibvCompChannel)
    {
        if (ibvCompChannel)
        {
            int rc = ibv_destroy_comp_channel(ibvCompChannel);
            if (rc != 0)
            {
                LOG_WARN("ibv_destroy_comp_channel failed error={},{}", rc, strerror(errno));
            }
        }
    }

    void FreeIbvPd(ibv_pd* ibvPd)
    {
        if (ibvPd)
        {
            int rc = ibv_dealloc_pd(ibvPd);
            if (rc != 0)
            {
                LOG_WARN("ibv_dealloc_pd failed error={},{}", rc, strerror(errno));
            }
        }
    }

    void FreeIbvMr(ibv_mr* ibvMr)
    {
        if (ibvMr)
        {
            int rc = ibv_dereg_mr(ibvMr);
            if (rc != 0)
            {
                LOG_WARN("ibv_dereg_mr failed error={},{}", rc, strerror(errno));
            }
        }
    }

    void FreeIbvCq(ibv_cq* ibvCq)
    {
        if (ibvCq)
        {
            int rc = ibv_destroy_cq(ibvCq);
            if (rc != 0)
            {
                LOG_WARN("ibv_destroy_cq failed error={},{}", rc, strerror(errno));
            }
        }
    }

    void FreeIbvSrq(ibv_srq* ibvSrq)
    {
        if (ibvSrq)
        {
            int rc = ibv_destroy_srq(ibvSrq);
            if (rc != 0)
            {
                LOG_WARN("ibv_destroy_srq failed error={},{}", rc, strerror(errno));
            }
        }
    }

    void FreeIbvQp(ibv_qp* ibvQp)
    {
        if (ibvQp)
        {
            int rc = ibv_destroy_qp(ibvQp);
            if (rc != 0)
            {
                LOG_WARN("ibv_destroy_qp failed error={},{}", rc, strerror(errno));
            }
        }
    }

    bool GetIBDeviceInfo(
        uint32_t& ibvLid,
        uint8_t& ibvPort,
        ibv_mtu& ibvMtu,
        unique_ibv_context& ibvContext)
    {
        int numIbDevices = -1;
        unique_ibv_device_list devices(ibv_get_device_list(&numIbDevices), FreeIbvDeviceList);
        if (!devices)
        {
            LOG_ERROR("ibv_get_device_list failed error={}", strerror(errno));
            return false;
        }

        // Check all available IB devices and select the first one which is active
        // TODO: Allow user, via configuration, to select the device to use
        bool bDeviceFound = false;
        for(int deviceId = 0; deviceId < numIbDevices && !bDeviceFound; deviceId++)
        {
            unique_ibv_context context(ibv_open_device((devices.get())[deviceId]), FreeIbvContext);
            if (!context)
            {
                LOG_WARN("ibv_open_device failed error={}", strerror(errno));
                continue;
            }

            struct ibv_device_attr devAttr{};
            int rc = ibv_query_device(context.get(), &devAttr);
            if (rc != 0)
            {
                LOG_WARN("ibv_query_device failed error={},{}", rc, strerror(errno));
                continue;
            }

            for(int port = 0; port <= devAttr.phys_port_cnt; port++)
            {
                struct ibv_port_attr portAttr{};
                rc = ibv_query_port(context.get(), port, &portAttr);
                if (rc != 0) 
                {
                    LOG_WARN("ibv_query_port failed error={},{}", rc, strerror(errno));
                    continue;
                }

                if (portAttr.state != IBV_PORT_ACTIVE)
                {
                    LOG_WARN("port={} not active", port);
                    continue;
                }

                if (portAttr.link_layer != IBV_LINK_LAYER_INFINIBAND)
                {
                    LOG_WARN("Not IB port={}", port);
                    continue;                        
                };

                unique_ibv_pd pd(ibv_alloc_pd(context.get()), FreeIbvPd);
                if (!pd)
                {
                    LOG_WARN("ibv_alloc_pd failed error={}", strerror(errno));
                    continue;
                }

                ibvMtu = portAttr.active_mtu;
                ibvLid = portAttr.lid;
                ibvPort = port;
                ibvContext = std::move(context);
                bDeviceFound = true;
                LOG_INFO("Found active IB device: {},{}:{},{},{},{},{},{},{}",
                    deviceId, (devices.get())[deviceId]->name, port, devAttr.sys_image_guid, portAttr.active_speed,
                    portAttr.active_width, portAttr.active_mtu, devAttr.max_qp, portAttr.lid);
            }

            if (bDeviceFound)
            {
                break;
            }
        }

        if (!bDeviceFound)
        {
            LOG_INFO("No IB devices found");
            return false;
        }

        LOG_INFO("IB Init successful");
        return true;
    }

    bool SetupQPForSendAndReceive(ibv_qp* ibvQp, uint8_t localIBPort, ibv_mtu ibMtu, uint32_t remoteQpn,
        uint32_t remoteIBLid, uint8_t remoteIBPort)
    {
        // Transition to INIT state
        struct ibv_qp_attr qpAttr{};
        qpAttr.qp_state = IBV_QPS_INIT;

        // We are not creating any partitions within the infiniband subnets
        qpAttr.pkey_index = 0;

        // Attach to local device port
        qpAttr.port_num = localIBPort;

        // We are not using any RDMA operations
        qpAttr.qp_access_flags = 0;

        // Setup all required parameters specified in the API doc
        int rc = ibv_modify_qp(ibvQp, &qpAttr, IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT
            | IBV_QP_ACCESS_FLAGS);
        if (rc != 0)
        {
            LOG_WARN("ibv_modify_qp failed error={},{}", rc, strerror(errno));
            return false;
        }

        // To setup bidirectional communication we need to transition queue to Ready To Receive (RTR) state first
        // and then to Ready To Send (RTS) state
        // Transition to RTR
        memset(&qpAttr, 0, sizeof(struct ibv_qp_attr));
        qpAttr.qp_state = IBV_QPS_RTR;
        qpAttr.path_mtu = ibMtu;

        // Attach to remote qpn
        qpAttr.dest_qp_num = remoteQpn;

        // Reset packet receive sequence number
        qpAttr.rq_psn = 0;

        // We are not using any RDMA atomic operations
        qpAttr.max_dest_rd_atomic = 0;

        // Setup receive not ready timer
        qpAttr.min_rnr_timer = IB_MIN_RNR_TIMER;

        // Setup remote path information
        qpAttr.ah_attr.is_global = 0;
        qpAttr.ah_attr.dlid = remoteIBLid;
        qpAttr.ah_attr.sl = 0;
        qpAttr.ah_attr.src_path_bits = 0;
        qpAttr.ah_attr.port_num = remoteIBPort;

        // Setup all required parameters specified in the API doc
        rc = ibv_modify_qp(ibvQp, &qpAttr, IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN
            | IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER);
        if (rc != 0)
        {
            LOG_WARN("ibv_modify_qp failed error={},{}", rc, strerror(errno));
            return false;
        }

        // Transition to RTS
        memset(&qpAttr, 0, sizeof(struct ibv_qp_attr));
        qpAttr.qp_state = IBV_QPS_RTS;

        // Setup retries and timeouts
        qpAttr.timeout = IB_TIMEOUT;
        qpAttr.retry_cnt = IB_RETRY_CNT;
        qpAttr.rnr_retry = IB_RNR_RETRY;

        // Reset packet send sequence number
        qpAttr.sq_psn = 0;

        // We are not using any RDMA atomic operations
        qpAttr.max_rd_atomic = 0;

        // Setup all required parameters specified in the API doc
        rc = ibv_modify_qp(ibvQp, &qpAttr, IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY
            | IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC);
        if (rc != 0)
        {
            LOG_WARN("ibv_modify_qp failed error={},{}", rc, strerror(errno));
            return false;
        }

        return true;
    }
}
//---------------------------------------------------------------------------------------------------------------------