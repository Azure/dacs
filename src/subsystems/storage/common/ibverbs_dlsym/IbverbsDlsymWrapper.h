///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
///--------------------------------------------------------------------------------------------------------------------
#include <dlfcn.h>
#include <infiniband/verbs.h>
#include <sys/types.h>
//---------------------------------------------------------------------------------------------------------------------
class IbverbsDlsymWrapper
{
  private:
    void* m_pHandle;

    IbverbsDlsymWrapper();

    static constexpr int m_sDlopen_failure_exit_code = 27;

  public:
    static IbverbsDlsymWrapper& GetInstance();

    ~IbverbsDlsymWrapper();

    int ibv_fork_init(void);

    int ibv_get_cq_event(struct ibv_comp_channel* channel, struct ibv_cq** cq, void** cq_context);

    void ibv_ack_cq_events(struct ibv_cq* cq, unsigned int nevents);

    const char* ibv_wc_status_str(enum ibv_wc_status status);

    struct ibv_mr* ibv_reg_mr_iova2(struct ibv_pd* pd, void* addr, size_t length, uint64_t iova, unsigned int access);

    struct ibv_pd* ibv_alloc_pd(struct ibv_context* context);

    struct ibv_comp_channel* ibv_create_comp_channel(struct ibv_context* context);

    struct ibv_cq* ibv_create_cq(
        struct ibv_context* context, int cqe, void* cq_context, struct ibv_comp_channel* channel, int comp_vector);

    struct ibv_qp* ibv_create_qp(struct ibv_pd* pd, struct ibv_qp_init_attr* qp_init_attr);

    int ibv_close_device(struct ibv_context* context);

    int ibv_dealloc_pd(struct ibv_pd* pd);

    int ibv_dereg_mr(struct ibv_mr* mr);

    int ibv_destroy_comp_channel(struct ibv_comp_channel* channel);

    int ibv_destroy_cq(struct ibv_cq* cq);

    int ibv_destroy_qp(struct ibv_qp* qp);

    int ibv_destroy_srq(struct ibv_srq* srq);

    struct ibv_device** ibv_get_device_list(int* num_devices);

    void ibv_free_device_list(struct ibv_device** list);

    const char* ibv_get_device_name(struct ibv_device* device);

    int ibv_modify_qp(struct ibv_qp* qp, struct ibv_qp_attr* attr, int attr_mask);

    struct ibv_context* ibv_open_device(struct ibv_device* device);

    int ibv_query_device(struct ibv_context* context, struct ibv_device_attr* device_attr);

    int Ibv_query_port(struct ibv_context *context, uint8_t port_num, struct _compat_ibv_port_attr *port_attr);

    struct ibv_mr *Ibv_reg_mr(struct ibv_pd *pd, void *addr, size_t length, int access);

    ///--------------------------------------------------------------------------------------------------------------------
    // Function types
    ///--------------------------------------------------------------------------------------------------------------------

    typedef int (*Original_open)(const char* pathname, int flags, mode_t mode);
    typedef int (*Original_ibv_fork_init)(void);
    typedef int (*Original_ibv_req_notify_cq)(struct ibv_cq* cq, int solicited_only);
    typedef int (*Original_ibv_post_recv)(struct ibv_qp* qp, struct ibv_recv_wr* wr, struct ibv_recv_wr** bad_wr);
    typedef int (*Original_ibv_post_send)(struct ibv_qp* qp, struct ibv_send_wr* wr, struct ibv_send_wr** bad_wr);
    typedef int (*Original_ibv_get_cq_event)(struct ibv_comp_channel* channel, struct ibv_cq** cq, void** cq_context);
    typedef void (*Original_ibv_ack_cq_events)(struct ibv_cq* cq, unsigned int nevents);
    typedef int (*Original_ibv_poll_cq)(struct ibv_cq* cq, int num_entries, struct ibv_wc* wc);
    typedef const char* (*Original_ibv_wc_status_str)(enum ibv_wc_status status);
    typedef ibv_mr* (*Original_ibv_reg_mr)(ibv_pd* pd, void* addr, size_t length, int access);
    typedef ibv_mr* (*Original_ibv_reg_mr_iova2)(
        ibv_pd* pd, void* addr, size_t length, uint64_t iova, unsigned int access);
    typedef ibv_pd* (*Original_ibv_alloc_pd)(ibv_context* context);
    typedef ibv_comp_channel* (*Original_ibv_create_comp_channel)(ibv_context* context);
    typedef ibv_cq* (*Original_ibv_create_cq)(
        ibv_context* context, int cqe, void* cq_context, ibv_comp_channel* channel, int comp_vector);
    typedef ibv_qp* (*Original_ibv_create_qp)(ibv_pd* pd, ibv_qp_init_attr* qp_init_attr);
    typedef int (*Original_ibv_close_device)(ibv_context* context);
    typedef int (*Original_ibv_dealloc_pd)(ibv_pd* pd);
    typedef int (*Original_ibv_dereg_mr)(ibv_mr* mr);
    typedef int (*Original_ibv_destroy_comp_channel)(ibv_comp_channel* channel);
    typedef int (*Original_ibv_destroy_cq)(ibv_cq* cq);
    typedef int (*Original_ibv_destroy_qp)(ibv_qp* qp);
    typedef int (*Original_ibv_destroy_srq)(ibv_srq* srq);
    typedef ibv_device** (*Original_ibv_get_device_list)(int* num_devices);
    typedef void (*Original_ibv_free_device_list)(ibv_device** list);
    typedef const char* (*Original_ibv_get_device_name)(ibv_device* device);
    typedef int (*Original_ibv_modify_qp)(ibv_qp* qp, ibv_qp_attr* attr, int attr_mask);
    typedef ibv_context* (*Original_ibv_open_device)(ibv_device* device);
    typedef int (*Original_ibv_query_device)(ibv_context* context, ibv_device_attr* device_attr);
    typedef int (*Original_ibv_query_port)(ibv_context* context, uint8_t port_num, _compat_ibv_port_attr* port_attr);
};
///--------------------------------------------------------------------------------------------------------------------
