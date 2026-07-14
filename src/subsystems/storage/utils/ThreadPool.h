//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include <thread>
#include <queue>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>
//---------------------------------------------------------------------------------------------------------------------
class ThreadPool
{
  typedef std::chrono::milliseconds chrono_ms_t;

  public:
    ThreadPool(/*[in]*/ int size);
    ~ThreadPool();

  public:
    /* Post lambda to be executed async by thread pool */
    void post(/*[in]*/ std::function<void()> &&func);

  private:
    bool isQueueEmpty();

  private:
    std::mutex                                      m_lock;
    std::condition_variable                         m_cv;
    std::queue<std::function<void()>>               m_queueTasks;
    std::vector<std::thread *>                      m_vecThreads;
    std::atomic<bool>                               m_bAlive;
};
//---------------------------------------------------------------------------------------------------------------------