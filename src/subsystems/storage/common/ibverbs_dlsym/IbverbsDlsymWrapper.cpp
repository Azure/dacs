///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
//---------------------------------------------------------------------------------------------------------------------
#include "IbverbsDlsymWrapper.h"

#include <dlfcn.h>
#include <infiniband/verbs.h>
#include <stdlib.h>
//---------------------------------------------------------------------------------------------------------------------
IbverbsDlsymWrapper::IbverbsDlsymWrapper()
{
    void* pHandle = dlopen("libibverbs.so", RTLD_LAZY);
    if (pHandle == NULL)
    {
        exit(m_sDlopen_failure_exit_code);
    }
    else
    {
        m_pHandle = pHandle;
    }
}
//---------------------------------------------------------------------------------------------------------------------
IbverbsDlsymWrapper& IbverbsDlsymWrapper::GetInstance()
{
    static IbverbsDlsymWrapper s_instance;
    return s_instance;
}
//---------------------------------------------------------------------------------------------------------------------
IbverbsDlsymWrapper::~IbverbsDlsymWrapper()
{
    dlclose(m_pHandle);
}
//---------------------------------------------------------------------------------------------------------------------
int IbverbsDlsymWrapper::ibv_fork_init(void)
{
    static Original_ibv_fork_init m_originalIbvForkInit = (Original_ibv_fork_init)dlsym(m_pHandle, "ibv_fork_init");
    return m_originalIbvForkInit();
}
//---------------------------------------------------------------------------------------------------------------------
int IbverbsDlsymWrapper::ibv_get_cq_event(struct ibv_comp_channel* channel, struct ibv_cq** cq, void** cq_context)
{
    static Original_ibv_get_cq_event m_originalIbvGetCqEvent = (Original_ibv_get_cq_event)dlsym(m_pHandle, "ibv_get_cq_event");
    return m_originalIbvGetCqEvent(channel, cq, cq_context);
}
//---------------------------------------------------------------------------------------------------------------------
void IbverbsDlsymWrapper::ibv_ack_cq_events(struct ibv_cq* cq, unsigned int nevents)
{
    static Original_ibv_ack_cq_events m_originalIbvAckCqEvents =
        (Original_ibv_ack_cq_events)dlsym(m_pHandle, "ibv_ack_cq_events");
    return m_originalIbvAckCqEvents(cq, nevents);
}
//---------------------------------------------------------------------------------------------------------------------
const char* IbverbsDlsymWrapper::ibv_wc_status_str(enum ibv_wc_status status)
{
    static Original_ibv_wc_status_str m_originalIbvWcStatusStr =
        (Original_ibv_wc_status_str)dlsym(m_pHandle, "ibv_wc_status_str");
    return m_originalIbvWcStatusStr(status);
}
//---------------------------------------------------------------------------------------------------------------------
struct ibv_mr*
IbverbsDlsymWrapper::ibv_reg_mr_iova2(struct ibv_pd* pd, void* addr, size_t length, uint64_t iova, unsigned int access)
{
    static Original_ibv_reg_mr_iova2 m_originalIbvRegMrIova2 = (Original_ibv_reg_mr_iova2)dlsym(m_pHandle, "ibv_reg_mr_iova2");
    return m_originalIbvRegMrIova2(pd, addr, length, iova, access);
}
//---------------------------------------------------------------------------------------------------------------------
struct ibv_pd* IbverbsDlsymWrapper::ibv_alloc_pd(struct ibv_context* context)
{
    static Original_ibv_alloc_pd m_originalIbvAllocPd = (Original_ibv_alloc_pd)dlsym(m_pHandle, "ibv_alloc_pd");
    return m_originalIbvAllocPd(context);
}
//---------------------------------------------------------------------------------------------------------------------
struct ibv_comp_channel* IbverbsDlsymWrapper::ibv_create_comp_channel(struct ibv_context* context)
{
    static Original_ibv_create_comp_channel m_originalIbvCreateCompChannel =
        (Original_ibv_create_comp_channel)dlsym(m_pHandle, "ibv_create_comp_channel");
    return m_originalIbvCreateCompChannel(context);
}
//---------------------------------------------------------------------------------------------------------------------
struct ibv_cq* IbverbsDlsymWrapper::ibv_create_cq(
    struct ibv_context* context, int cqe, void* cq_context, struct ibv_comp_channel* channel, int comp_vector)
{
    static Original_ibv_create_cq m_originalIbvCreateCq = (Original_ibv_create_cq)dlsym(m_pHandle, "ibv_create_cq");
    return m_originalIbvCreateCq(context, cqe, cq_context, channel, comp_vector);
}
//---------------------------------------------------------------------------------------------------------------------
struct ibv_qp* IbverbsDlsymWrapper::ibv_create_qp(struct ibv_pd* pd, struct ibv_qp_init_attr* qp_init_attr)
{
    static Original_ibv_create_qp m_originalIbvCreateQp = (Original_ibv_create_qp)dlsym(m_pHandle, "ibv_create_qp");
    return m_originalIbvCreateQp(pd, qp_init_attr);
}
//---------------------------------------------------------------------------------------------------------------------
int IbverbsDlsymWrapper::ibv_close_device(struct ibv_context* context)
{
    static Original_ibv_close_device m_originalIbvCloseDevice =
        (Original_ibv_close_device)dlsym(m_pHandle, "ibv_close_device");
    return m_originalIbvCloseDevice(context);
}
//---------------------------------------------------------------------------------------------------------------------
int IbverbsDlsymWrapper::ibv_dealloc_pd(struct ibv_pd* pd)
{
    static Original_ibv_dealloc_pd m_originalIbvDeallocPd = (Original_ibv_dealloc_pd)dlsym(m_pHandle, "ibv_dealloc_pd");
    return m_originalIbvDeallocPd(pd);
}
//---------------------------------------------------------------------------------------------------------------------
int IbverbsDlsymWrapper::ibv_dereg_mr(struct ibv_mr* mr)
{
    static Original_ibv_dereg_mr m_originalIbvDeregMr = (Original_ibv_dereg_mr)dlsym(m_pHandle, "ibv_dereg_mr");
    return m_originalIbvDeregMr(mr);
}
//---------------------------------------------------------------------------------------------------------------------
int IbverbsDlsymWrapper::ibv_destroy_comp_channel(struct ibv_comp_channel* channel)
{
    static Original_ibv_destroy_comp_channel m_originalIbvDestroyCompChannel =
        (Original_ibv_destroy_comp_channel)dlsym(m_pHandle, "ibv_destroy_comp_channel");
    return m_originalIbvDestroyCompChannel(channel);
}
//---------------------------------------------------------------------------------------------------------------------
int IbverbsDlsymWrapper::ibv_destroy_cq(struct ibv_cq* cq)
{
    static Original_ibv_destroy_cq m_originalIbvDestroyCq = (Original_ibv_destroy_cq)dlsym(m_pHandle, "ibv_destroy_cq");
    return m_originalIbvDestroyCq(cq);
}
//---------------------------------------------------------------------------------------------------------------------
int IbverbsDlsymWrapper::ibv_destroy_qp(struct ibv_qp* qp)
{
    static Original_ibv_destroy_qp m_originalIbvDestroyQp = (Original_ibv_destroy_qp)dlsym(m_pHandle, "ibv_destroy_qp");
    return m_originalIbvDestroyQp(qp);
}
//---------------------------------------------------------------------------------------------------------------------
int IbverbsDlsymWrapper::ibv_destroy_srq(struct ibv_srq* srq)
{
    static Original_ibv_destroy_srq m_originalIbvDestroySrq = (Original_ibv_destroy_srq)dlsym(m_pHandle, "ibv_destroy_srq");
    return m_originalIbvDestroySrq(srq);
}
//---------------------------------------------------------------------------------------------------------------------
struct ibv_device** IbverbsDlsymWrapper::ibv_get_device_list(int* num_devices)
{
    static Original_ibv_get_device_list m_originalIbvGetDeviceList =
        (Original_ibv_get_device_list)dlsym(m_pHandle, "ibv_get_device_list");
    return m_originalIbvGetDeviceList(num_devices);
}
//---------------------------------------------------------------------------------------------------------------------
void IbverbsDlsymWrapper::ibv_free_device_list(struct ibv_device** list)
{
    static Original_ibv_free_device_list m_originalIbvFreeDeviceList =
        (Original_ibv_free_device_list)dlsym(m_pHandle, "ibv_free_device_list");
    return m_originalIbvFreeDeviceList(list);
}
//---------------------------------------------------------------------------------------------------------------------
const char* IbverbsDlsymWrapper::ibv_get_device_name(struct ibv_device* device)
{
    static Original_ibv_get_device_name m_originalIbvGetDeviceName =
        (Original_ibv_get_device_name)dlsym(m_pHandle, "ibv_get_device_name");
    return m_originalIbvGetDeviceName(device);
}
//---------------------------------------------------------------------------------------------------------------------
int IbverbsDlsymWrapper::ibv_modify_qp(struct ibv_qp* qp, struct ibv_qp_attr* attr, int attr_mask)
{
    static Original_ibv_modify_qp m_originalIbvModifyQp = (Original_ibv_modify_qp)dlsym(m_pHandle, "ibv_modify_qp");
    return m_originalIbvModifyQp(qp, attr, attr_mask);
}
//---------------------------------------------------------------------------------------------------------------------
struct ibv_context* IbverbsDlsymWrapper::ibv_open_device(struct ibv_device* device)
{
    static Original_ibv_open_device m_originalIbvOpenDevice = (Original_ibv_open_device)dlsym(m_pHandle, "ibv_open_device");
    return m_originalIbvOpenDevice(device);
}
//---------------------------------------------------------------------------------------------------------------------
int IbverbsDlsymWrapper::ibv_query_device(struct ibv_context* context, struct ibv_device_attr* device_attr)
{
    static Original_ibv_query_device m_originalIbvQueryDevice =
        (Original_ibv_query_device)dlsym(m_pHandle, "ibv_query_device");
    return m_originalIbvQueryDevice(context, device_attr);
}
//---------------------------------------------------------------------------------------------------------------------
int IbverbsDlsymWrapper::Ibv_query_port(struct ibv_context* context,
                                        uint8_t port_num,
                                        struct _compat_ibv_port_attr* port_attr)
{
    static Original_ibv_query_port m_originalIbvQueryPort = (Original_ibv_query_port)dlsym(m_pHandle, "ibv_query_port");
    return m_originalIbvQueryPort(context, port_num, port_attr);
}
//---------------------------------------------------------------------------------------------------------------------
struct ibv_mr* IbverbsDlsymWrapper::Ibv_reg_mr(struct ibv_pd* pd, void* addr, size_t length, int access)
{
    static Original_ibv_reg_mr m_originalIbvRegMr = (Original_ibv_reg_mr)dlsym(m_pHandle, "ibv_reg_mr");
    return m_originalIbvRegMr(pd, addr, length, access);
}
//---------------------------------------------------------------------------------------------------------------------