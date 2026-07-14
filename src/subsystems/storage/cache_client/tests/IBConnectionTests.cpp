///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
//---------------------------------------------------------------------------------------------------------------------
// Unit tests for IBConnection direct-MR changes.
//
// Strategy: IBConnection calls ibv_post_send/ibv_post_recv through inline
// functions that dispatch via qp->context->ops function pointers (verbs.h).
// We set up a fake ibv_context with ops pointing to our test fakes.
// For non-inline ibverbs functions (ibv_reg_mr, ibv_dereg_mr, etc.) we provide
// C-level stubs that the linker resolves ahead of the IbverbsDlsym archive.
//---------------------------------------------------------------------------------------------------------------------
#include <gtest/gtest.h>
#include <cstring>
#include <atomic>
#include <vector>
#include <infiniband/verbs.h>

#include "IBConnection.h"
#include "IBUtils.h"

//---------------------------------------------------------------------------------------------------------------------
// Fake ibverbs state — these globals let us control stub behavior per-test.
//---------------------------------------------------------------------------------------------------------------------
namespace fake_ib {
    static std::atomic<int> reg_mr_call_count{0};
    static std::atomic<int> dereg_mr_call_count{0};
    static std::atomic<int> post_send_call_count{0};
    static std::atomic<int> post_recv_call_count{0};
    static std::atomic<int> post_send_num_sge{0};
    static std::atomic<int> reg_mr_fail_next{0};   // if > 0, next reg_mr returns null
    static std::atomic<int> dereg_mr_fail_next{0};  // if > 0, next dereg_mr returns -1
    static std::atomic<int> post_send_fail_next{0};
    static std::atomic<int> post_recv_fail_next{0};

    // Dummy MR returned by fake ibv_reg_mr
    static struct ibv_mr fake_external_mr{};
    // Dummy PD
    static struct ibv_pd fake_pd{};
    // Fake ibv_context with ops table
    static struct ibv_context fake_context{};

    void Reset()
    {
        reg_mr_call_count = 0;
        dereg_mr_call_count = 0;
        post_send_call_count = 0;
        post_recv_call_count = 0;
        post_send_num_sge = 0;
        reg_mr_fail_next = 0;
        dereg_mr_fail_next = 0;
        post_send_fail_next = 0;
        post_recv_fail_next = 0;
        memset(&fake_external_mr, 0, sizeof(fake_external_mr));
        fake_external_mr.lkey = 0x1234;
        fake_external_mr.pd = &fake_pd;
    }

    // Function pointers for the context ops table (called by inline ibv_post_send/recv)
    int fake_post_send(struct ibv_qp*, struct ibv_send_wr* wr, struct ibv_send_wr**)
    {
        post_send_call_count++;
        post_send_num_sge = wr->num_sge;
        if (post_send_fail_next > 0)
        {
            post_send_fail_next--;
            return EINVAL;
        }
        return 0;
    }

    int fake_post_recv(struct ibv_qp*, struct ibv_recv_wr*, struct ibv_recv_wr**)
    {
        post_recv_call_count++;
        if (post_recv_fail_next > 0)
        {
            post_recv_fail_next--;
            return EINVAL;
        }
        return 0;
    }

    void InitContext()
    {
        memset(&fake_context, 0, sizeof(fake_context));
        fake_context.ops.post_send = fake_post_send;
        fake_context.ops.post_recv = fake_post_recv;
    }
} // namespace fake_ib

//---------------------------------------------------------------------------------------------------------------------
// Non-inline ibverbs stubs (override IbverbsDlsym archive symbols)
// Parenthesized names prevent verbs.h macro expansion.
//---------------------------------------------------------------------------------------------------------------------
extern "C" {

struct ibv_mr* (ibv_reg_mr)(struct ibv_pd* pd, void* addr, size_t length, int access)
{
    fake_ib::reg_mr_call_count++;
    if (fake_ib::reg_mr_fail_next > 0)
    {
        fake_ib::reg_mr_fail_next--;
        errno = ENOMEM;
        return nullptr;
    }
    return &fake_ib::fake_external_mr;
}

struct ibv_mr* ibv_reg_mr_iova2(struct ibv_pd* pd, void* addr, size_t length,
                                uint64_t iova, unsigned int access)
{
    // Delegate to ibv_reg_mr stub
    return (ibv_reg_mr)(pd, addr, length, static_cast<int>(access));
}

int ibv_dereg_mr(struct ibv_mr* mr)
{
    fake_ib::dereg_mr_call_count++;
    if (fake_ib::dereg_mr_fail_next > 0)
    {
        fake_ib::dereg_mr_fail_next--;
        errno = EINVAL;
        return -1;
    }
    return 0;
}

// Stubs for functions used by IBConnection::Receive() (not tested here)
int ibv_get_cq_event(struct ibv_comp_channel*, struct ibv_cq**, void**) { return -1; }
void ibv_ack_cq_events(struct ibv_cq*, unsigned int) {}
const char* ibv_wc_status_str(enum ibv_wc_status) { return "fake"; }

} // extern "C"

//---------------------------------------------------------------------------------------------------------------------
// Test fixture
//---------------------------------------------------------------------------------------------------------------------
class IBConnectionTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        fake_ib::Reset();
        fake_ib::InitContext();

        // Allocate fake ibverbs objects on the heap
        m_fakeQp = (struct ibv_qp*)calloc(1, sizeof(struct ibv_qp));
        m_fakeQp->qp_num = 42;
        m_fakeQp->context = &fake_ib::fake_context;  // ops table for inline dispatch

        m_fakeChannel = (struct ibv_comp_channel*)calloc(1, sizeof(struct ibv_comp_channel));
        m_fakeChannel->fd = -1;

        m_fakeCq = (struct ibv_cq*)calloc(1, sizeof(struct ibv_cq));

        m_mrBuffer = std::make_unique<char[]>(ibutils::IB_BUFFER_SIZE);
        m_fakeMrBlock = (struct ibv_mr*)calloc(1, sizeof(struct ibv_mr));
        m_fakeMrBlock->pd = &fake_ib::fake_pd;
        m_fakeMrBlock->lkey = 0xABCD;
    }

    std::unique_ptr<IBConnection> CreateConnection(bool directMrEnabled = true)
    {
        // Create a pipe so the event channel fd is valid for fcntl
        int pipeFds[2];
        EXPECT_EQ(0, pipe(pipeFds));
        m_fakeChannel->fd = pipeFds[0];
        m_pipeWriteEnd = pipeFds[1];

        ibutils::unique_ibv_qp qp(m_fakeQp, [](struct ibv_qp* q) { free(q); });
        m_fakeQp = nullptr;

        ibutils::unique_ibv_comp_channel ch(m_fakeChannel,
            [](struct ibv_comp_channel* c) { close(c->fd); free(c); });
        m_fakeChannel = nullptr;

        ibutils::unique_ibv_cq cq(m_fakeCq, [](struct ibv_cq* c) { free(c); });
        m_fakeCq = nullptr;

        ibutils::unique_ibv_mr mr(m_fakeMrBlock, [](struct ibv_mr* m) { free(m); });
        m_fakeMrBlock = nullptr;

        return std::make_unique<IBConnection>(
            std::move(qp), std::move(ch), std::move(cq), std::move(mr), std::move(m_mrBuffer),
            directMrEnabled);
    }

    void TearDown() override
    {
        if (m_pipeWriteEnd >= 0) close(m_pipeWriteEnd);
        if (m_fakeQp) free(m_fakeQp);
        if (m_fakeChannel) free(m_fakeChannel);
        if (m_fakeCq) free(m_fakeCq);
        if (m_fakeMrBlock) free(m_fakeMrBlock);
    }

    struct ibv_qp* m_fakeQp = nullptr;
    struct ibv_comp_channel* m_fakeChannel = nullptr;
    struct ibv_cq* m_fakeCq = nullptr;
    struct ibv_mr* m_fakeMrBlock = nullptr;
    std::unique_ptr<char[]> m_mrBuffer;
    int m_pipeWriteEnd = -1;
};

//---------------------------------------------------------------------------------------------------------------------
// Tests for SendAndFinalize routing (threshold-based)
//---------------------------------------------------------------------------------------------------------------------
TEST_F(IBConnectionTest, SendAndFinalize_SmallBuffer_UsesMemcpyPath)
{
    auto conn = CreateConnection();

    const char header[] = "test-header";
    EXPECT_EQ(static_cast<int>(sizeof(header)), conn->Send(header, sizeof(header)));

    // Buffer below DIRECT_REG_THRESHOLD (4 MB) should use memcpy path
    const uint64_t smallSize = 1024;
    std::vector<char> buf(smallSize, 'A');

    int result = conn->SendAndFinalize(buf.data(), smallSize);
    EXPECT_EQ(static_cast<int>(smallSize), result);

    // Small path does NOT call ibv_reg_mr for external buffer
    EXPECT_EQ(0, fake_ib::reg_mr_call_count.load());
    // FinalizeWrite uses 1 SGE
    EXPECT_EQ(1, fake_ib::post_send_num_sge.load());
    EXPECT_EQ(1, fake_ib::post_send_call_count.load());
    EXPECT_EQ(1, fake_ib::post_recv_call_count.load());
}

TEST_F(IBConnectionTest, SendAndFinalize_LargeBuffer_UsesDirectMRPath)
{
    auto conn = CreateConnection();

    const char header[] = "test-header";
    conn->Send(header, sizeof(header));

    // Buffer at exactly DIRECT_REG_THRESHOLD (4 MB) should use direct-MR path
    const uint64_t largeSize = 4 * 1024 * 1024;
    std::vector<char> buf(largeSize, 'B');

    int result = conn->SendAndFinalize(buf.data(), largeSize);
    EXPECT_EQ(static_cast<int>(largeSize), result);

    // Direct-MR path calls ibv_reg_mr for external buffer
    EXPECT_EQ(1, fake_ib::reg_mr_call_count.load());
    // FinalizeWriteWithExternalBuffer uses 2 SGEs
    EXPECT_EQ(2, fake_ib::post_send_num_sge.load());
    EXPECT_EQ(1, fake_ib::post_send_call_count.load());
    EXPECT_EQ(1, fake_ib::post_recv_call_count.load());
}

TEST_F(IBConnectionTest, SendAndFinalize_ThresholdBoundary_BelowUsesMemcpy)
{
    auto conn = CreateConnection();

    // Just below DIRECT_REG_THRESHOLD — should use memcpy path
    const uint64_t belowThreshold = 4 * 1024 * 1024 - 1;
    std::vector<char> buf(belowThreshold, 'C');

    int result = conn->SendAndFinalize(buf.data(), belowThreshold);
    EXPECT_EQ(static_cast<int>(belowThreshold), result);

    EXPECT_EQ(0, fake_ib::reg_mr_call_count.load());
    EXPECT_EQ(1, fake_ib::post_send_num_sge.load());
}

//---------------------------------------------------------------------------------------------------------------------
// Tests for FinalizeWriteWithExternalBuffer error handling
//---------------------------------------------------------------------------------------------------------------------
TEST_F(IBConnectionTest, DirectMR_RegMrFailure_ReturnsError)
{
    auto conn = CreateConnection();

    const char header[] = "header";
    conn->Send(header, sizeof(header));

    fake_ib::reg_mr_fail_next = 1;

    const uint64_t size = 4 * 1024 * 1024;
    std::vector<char> buf(size, 'D');

    int result = conn->SendAndFinalize(buf.data(), size);
    EXPECT_EQ(-1, result);
    EXPECT_EQ(1, fake_ib::reg_mr_call_count.load());
    EXPECT_EQ(0, fake_ib::post_send_call_count.load());
    EXPECT_FALSE(conn->IsValidConnection());
}

TEST_F(IBConnectionTest, DirectMR_PostRecvFailure_ReturnsError)
{
    auto conn = CreateConnection();

    const char header[] = "header";
    conn->Send(header, sizeof(header));

    fake_ib::post_recv_fail_next = 1;

    const uint64_t size = 4 * 1024 * 1024;
    std::vector<char> buf(size, 'E');

    int result = conn->SendAndFinalize(buf.data(), size);
    EXPECT_EQ(-1, result);
    EXPECT_EQ(1, fake_ib::reg_mr_call_count.load());
    EXPECT_EQ(1, fake_ib::post_recv_call_count.load());
    EXPECT_EQ(0, fake_ib::post_send_call_count.load());
    // External MR should be deregistered on failure
    EXPECT_EQ(1, fake_ib::dereg_mr_call_count.load());
    EXPECT_FALSE(conn->IsValidConnection());
}

TEST_F(IBConnectionTest, DirectMR_PostSendFailure_ReturnsError)
{
    auto conn = CreateConnection();

    const char header[] = "header";
    conn->Send(header, sizeof(header));

    fake_ib::post_send_fail_next = 1;

    const uint64_t size = 4 * 1024 * 1024;
    std::vector<char> buf(size, 'F');

    int result = conn->SendAndFinalize(buf.data(), size);
    EXPECT_EQ(-1, result);
    EXPECT_EQ(1, fake_ib::reg_mr_call_count.load());
    EXPECT_EQ(1, fake_ib::post_recv_call_count.load());
    EXPECT_EQ(1, fake_ib::post_send_call_count.load());
    // External MR should be deregistered on failure
    EXPECT_EQ(1, fake_ib::dereg_mr_call_count.load());
    EXPECT_FALSE(conn->IsValidConnection());
}

//---------------------------------------------------------------------------------------------------------------------
// Tests for DeregisterExternalMr
//---------------------------------------------------------------------------------------------------------------------
TEST_F(IBConnectionTest, ResetConnection_DeregistersExternalMr)
{
    auto conn = CreateConnection();

    const char header[] = "header";
    conn->Send(header, sizeof(header));

    const uint64_t size = 4 * 1024 * 1024;
    std::vector<char> buf(size, 'G');
    conn->SendAndFinalize(buf.data(), size);

    EXPECT_EQ(1, fake_ib::reg_mr_call_count.load());
    EXPECT_EQ(0, fake_ib::dereg_mr_call_count.load());

    conn->ResetConnection();
    EXPECT_EQ(1, fake_ib::dereg_mr_call_count.load());
}

TEST_F(IBConnectionTest, ResetConnection_NoExternalMr_DoesNotCallDereg)
{
    auto conn = CreateConnection();

    const uint64_t size = 1024;
    std::vector<char> buf(size, 'H');
    conn->SendAndFinalize(buf.data(), size);

    conn->ResetConnection();

    // No external MR was registered, so dereg should not be called
    EXPECT_EQ(0, fake_ib::dereg_mr_call_count.load());
}

TEST_F(IBConnectionTest, DeregMrFailure_ConnectionStillUsable)
{
    auto conn = CreateConnection();

    const char header[] = "header";
    conn->Send(header, sizeof(header));

    const uint64_t size = 4 * 1024 * 1024;
    std::vector<char> buf(size, 'I');
    conn->SendAndFinalize(buf.data(), size);

    fake_ib::dereg_mr_fail_next = 1;
    conn->ResetConnection();

    EXPECT_EQ(1, fake_ib::dereg_mr_call_count.load());
    // Dereg failure is logged but not fatal
    EXPECT_TRUE(conn->IsValidConnection());
}

//---------------------------------------------------------------------------------------------------------------------
// Tests for destructor ordering (QP destroyed before external MR deregistered)
//---------------------------------------------------------------------------------------------------------------------
TEST_F(IBConnectionTest, Destructor_DeregistersExternalMr)
{
    {
        auto conn = CreateConnection();

        const char header[] = "header";
        conn->Send(header, sizeof(header));

        const uint64_t size = 4 * 1024 * 1024;
        std::vector<char> buf(size, 'J');
        conn->SendAndFinalize(buf.data(), size);

        EXPECT_EQ(0, fake_ib::dereg_mr_call_count.load());
    }

    // Destructor should have deregistered the external MR
    EXPECT_EQ(1, fake_ib::dereg_mr_call_count.load());
}

//---------------------------------------------------------------------------------------------------------------------
// Tests for Send overflow protection
//---------------------------------------------------------------------------------------------------------------------
TEST_F(IBConnectionTest, Send_ExceedsBufferSize_ReturnsError)
{
    auto conn = CreateConnection();

    const uint64_t hugeSize = ibutils::IB_BUFFER_SIZE + 1;
    std::vector<char> buf(hugeSize, 'K');

    int result = conn->Send(buf.data(), hugeSize);
    EXPECT_EQ(-1, result);
    EXPECT_FALSE(conn->IsValidConnection());
}

//---------------------------------------------------------------------------------------------------------------------
// Tests for double-send prevention
//---------------------------------------------------------------------------------------------------------------------
TEST_F(IBConnectionTest, SendAndFinalize_CalledTwice_SecondCallFails)
{
    auto conn = CreateConnection();

    const uint64_t size = 1024;
    std::vector<char> buf(size, 'L');

    int result1 = conn->SendAndFinalize(buf.data(), size);
    EXPECT_EQ(static_cast<int>(size), result1);

    // Second call should fail (send already attempted)
    int result2 = conn->SendAndFinalize(buf.data(), size);
    EXPECT_EQ(-1, result2);
}

//---------------------------------------------------------------------------------------------------------------------
// Tests for directMrEnabled=false fallback (device with max_sge=1)
//---------------------------------------------------------------------------------------------------------------------
TEST_F(IBConnectionTest, DirectMrDisabled_LargeBuffer_UsesMemcpyPath)
{
    auto conn = CreateConnection(/*directMrEnabled=*/false);

    // Even with large buffer, should use memcpy + 1-SGE path when direct-MR is disabled
    const uint64_t largeSize = 4 * 1024 * 1024;
    std::vector<char> buf(largeSize, 'M');

    int result = conn->SendAndFinalize(buf.data(), largeSize);
    EXPECT_EQ(static_cast<int>(largeSize), result);

    // Should NOT register external MR
    EXPECT_EQ(0, fake_ib::reg_mr_call_count.load());
    // Should use 1-SGE FinalizeWrite path
    EXPECT_EQ(1, fake_ib::post_send_num_sge.load());
}
//---------------------------------------------------------------------------------------------------------------------
