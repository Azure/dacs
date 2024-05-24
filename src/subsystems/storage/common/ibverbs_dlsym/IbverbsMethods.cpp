///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "IbverbsDlsymWrapper.h"
#include <infiniband/verbs.h>
//---------------------------------------------------------------------------------------------------------------------
extern "C"
{
    //---------------------------------------------------------------------------------------------------------------------
    int ibv_fork_init(void)
    {
        return IbverbsDlsymWrapper::GetInstance().ibv_fork_init();
    }
    //---------------------------------------------------------------------------------------------------------------------
    int ibv_get_cq_event(struct ibv_comp_channel* channel, struct ibv_cq** cq, void** cq_context)
    {
        return IbverbsDlsymWrapper::GetInstance().ibv_get_cq_event(channel, cq, cq_context);
    }
    //---------------------------------------------------------------------------------------------------------------------
    void ibv_ack_cq_events(struct ibv_cq* cq, unsigned int nevents)
    {
        return IbverbsDlsymWrapper::GetInstance().ibv_ack_cq_events(cq, nevents);
    }
    //---------------------------------------------------------------------------------------------------------------------
    const char* ibv_wc_status_str(enum ibv_wc_status status)
    {
        return IbverbsDlsymWrapper::GetInstance().ibv_wc_status_str(status);
    }
    //---------------------------------------------------------------------------------------------------------------------
    struct ibv_mr* ibv_reg_mr_iova2(struct ibv_pd* pd, void* addr, size_t length, uint64_t iova, unsigned int access)
    {
        return IbverbsDlsymWrapper::GetInstance().ibv_reg_mr_iova2(pd, addr, length, iova, access);
    }
    //---------------------------------------------------------------------------------------------------------------------
    struct ibv_pd* ibv_alloc_pd(struct ibv_context* context)
    {
        return IbverbsDlsymWrapper::GetInstance().ibv_alloc_pd(context);
    }
    //---------------------------------------------------------------------------------------------------------------------
    struct ibv_comp_channel* ibv_create_comp_channel(struct ibv_context* context)
    {
        return IbverbsDlsymWrapper::GetInstance().ibv_create_comp_channel(context);
    }
    //---------------------------------------------------------------------------------------------------------------------
    struct ibv_cq* ibv_create_cq(
        struct ibv_context* context, int cqe, void* cq_context, struct ibv_comp_channel* channel, int comp_vector)
    {
        return IbverbsDlsymWrapper::GetInstance().ibv_create_cq(context, cqe, cq_context, channel, comp_vector);
    }
    //---------------------------------------------------------------------------------------------------------------------
    struct ibv_qp* ibv_create_qp(struct ibv_pd* pd, struct ibv_qp_init_attr* qp_init_attr)
    {
        return IbverbsDlsymWrapper::GetInstance().ibv_create_qp(pd, qp_init_attr);
    }
    //---------------------------------------------------------------------------------------------------------------------
    int ibv_close_device(struct ibv_context* context)
    {
        return IbverbsDlsymWrapper::GetInstance().ibv_close_device(context);
    }
    //---------------------------------------------------------------------------------------------------------------------
    int ibv_dealloc_pd(struct ibv_pd* pd)
    {
        return IbverbsDlsymWrapper::GetInstance().ibv_dealloc_pd(pd);
    }
    //---------------------------------------------------------------------------------------------------------------------
    int ibv_dereg_mr(struct ibv_mr* mr)
    {
        return IbverbsDlsymWrapper::GetInstance().ibv_dereg_mr(mr);
    }
    //---------------------------------------------------------------------------------------------------------------------
    int ibv_destroy_comp_channel(struct ibv_comp_channel* channel)
    {
       return IbverbsDlsymWrapper::GetInstance().ibv_destroy_comp_channel(channel);
    }
    //---------------------------------------------------------------------------------------------------------------------
    int ibv_destroy_cq(struct ibv_cq* cq)
    {
        return IbverbsDlsymWrapper::GetInstance().ibv_destroy_cq(cq);
    }
    //---------------------------------------------------------------------------------------------------------------------
    int ibv_destroy_qp(struct ibv_qp* qp)
    {
        return IbverbsDlsymWrapper::GetInstance().ibv_destroy_qp(qp);
    }
    //---------------------------------------------------------------------------------------------------------------------
    int ibv_destroy_srq(struct ibv_srq* srq)
    {
        return IbverbsDlsymWrapper::GetInstance().ibv_destroy_srq(srq);
    }
    //---------------------------------------------------------------------------------------------------------------------
    struct ibv_device** ibv_get_device_list(int* num_devices)
    {
        return IbverbsDlsymWrapper::GetInstance().ibv_get_device_list(num_devices);
    }
    //---------------------------------------------------------------------------------------------------------------------
    void ibv_free_device_list(struct ibv_device** list)
    {
        return IbverbsDlsymWrapper::GetInstance().ibv_free_device_list(list);
    }
    //---------------------------------------------------------------------------------------------------------------------
    const char* ibv_get_device_name(struct ibv_device* device)
    {
        return IbverbsDlsymWrapper::GetInstance().ibv_get_device_name(device);
    }
    //---------------------------------------------------------------------------------------------------------------------
    int ibv_modify_qp(struct ibv_qp* qp, struct ibv_qp_attr* attr, int attr_mask)
    {
        return IbverbsDlsymWrapper::GetInstance().ibv_modify_qp(qp, attr, attr_mask);
    }
    //---------------------------------------------------------------------------------------------------------------------
    struct ibv_context* ibv_open_device(struct ibv_device* device)
    {
        return IbverbsDlsymWrapper::GetInstance().ibv_open_device(device);
    }
    //---------------------------------------------------------------------------------------------------------------------
    int ibv_query_device(struct ibv_context* context, struct ibv_device_attr* device_attr)
    {
        return IbverbsDlsymWrapper::GetInstance().ibv_query_device(context, device_attr);
    }
    //---------------------------------------------------------------------------------------------------------------------
    int (ibv_query_port)(struct ibv_context *context, uint8_t port_num, struct _compat_ibv_port_attr *port_attr)
    {
        return IbverbsDlsymWrapper::GetInstance().Ibv_query_port(context, port_num, port_attr);
    }
    //---------------------------------------------------------------------------------------------------------------------
    struct ibv_mr *(ibv_reg_mr)(struct ibv_pd *pd, void *addr, size_t length, int access)
    {
        return IbverbsDlsymWrapper::GetInstance().Ibv_reg_mr(pd, addr, length, access);
    }
}
//---------------------------------------------------------------------------------------------------------------------
