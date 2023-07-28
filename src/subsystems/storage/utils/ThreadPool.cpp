//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "ThreadPool.h"
#include <iostream>
#include <memory>
#include <chrono>
//---------------------------------------------------------------------------------------------------------------------
ThreadPool::ThreadPool(int size)
{
  m_bAlive = true;
  for (int i = 0; i < size; ++i)
  {
    auto t = new std::thread([this, i]()
    {
      while (m_bAlive || !isQueueEmpty())
      {
        std::unique_lock<std::mutex> lk(m_lock);
        m_cv.wait(
          lk,
          [this]
          {
            return (!m_bAlive || m_queueTasks.size() > 0);
          });

        std::function<void()> asyncTask;
        if(m_queueTasks.size() > 0)
        {
          asyncTask = m_queueTasks.front();
          m_queueTasks.pop();
        }
        lk.unlock();

        if(asyncTask)
        {
          asyncTask();
        }
      }
    });
    m_vecThreads.push_back(t);
  }
}
//---------------------------------------------------------------------------------------------------------------------
ThreadPool::~ThreadPool()
{
  m_bAlive = false;
  m_cv.notify_all();
  for (size_t i = 0; i < m_vecThreads.size(); ++i)
  {
    auto t = m_vecThreads[i];
    t->join();
    delete t;
  }
}
//---------------------------------------------------------------------------------------------------------------------
bool ThreadPool::isQueueEmpty()
{
  std::unique_lock<std::mutex>(m_lock);
  return m_queueTasks.empty();
}
//---------------------------------------------------------------------------------------------------------------------
void ThreadPool::post(std::function<void()> &&func)
{
  if (m_bAlive == false)
  {
    return;
  }
  std::unique_lock<std::mutex> lk(m_lock, std::defer_lock);
  lk.lock();
  m_queueTasks.push(func);
  lk.unlock();
  m_cv.notify_one();
}
//---------------------------------------------------------------------------------------------------------------------