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