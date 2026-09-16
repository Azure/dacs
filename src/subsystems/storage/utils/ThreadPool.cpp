// Copyright (c) Microsoft Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

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
    auto t = new std::thread([this]()
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
  std::unique_lock<std::mutex> lock(m_lock);
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