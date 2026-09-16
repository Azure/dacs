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

#include <fstream>
#include <iostream>
#include <string>
#include <stdio.h>
#include <vector>
#include <chrono>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <atomic>
#include <stdint.h>
#include <cmath>
#include <errno.h>
#include <cstring>
#include <memory>
#include <experimental/filesystem>

#include "ThreadPool.h"
//---------------------------------------------------------------------------------------------------------------------
#define TIME_NOW std::chrono::high_resolution_clock::now();

#define SECS_INTERVAL(tStart, tEnd) std::chrono::duration_cast<std::chrono::seconds>(tEnd - tStart).count();

#define TIMED_EXEC(stmt, timer) { auto tS = TIME_NOW;  stmt; auto tE = TIME_NOW; timer = SECS_INTERVAL(tS, tE); }
//---------------------------------------------------------------------------------------------------------------------
using namespace std;
using namespace std::experimental::filesystem;
//---------------------------------------------------------------------------------------------------------------------
void WorkerFunction(
    const std::string& path,
    std::atomic_uint_fast64_t &numBytesRead)
{
    // Read the contents of the file
    std::ifstream ifs(path, std::ios::binary | ios::ate);

    std::size_t fileSize = 0;
    if(ifs.is_open())
    {
        const std::streamoff streamSize = ifs.tellg();
        if (streamSize < 0)
        {
            return;
        }
        fileSize = static_cast<std::size_t>(streamSize);
        std::unique_ptr<char[]> writeAddr = std::make_unique<char[]>(fileSize);
        ifs.seekg(0, ios::beg);
        ifs.read(writeAddr.get(), static_cast<std::streamsize>(fileSize));

        ifs.close();
    }

    numBytesRead += fileSize;
}
//---------------------------------------------------------------------------------------------------------------------
int main(int argc, char** argv)
{
    // Expects: Storage_path, dataset_name, num_workers
    if (argc < 4)
    {
        std::cout << "Too few arguments provided, terminating" << std::endl;
        exit(0);
    }

    path dir(argv[1]);
    path dataset(argv[2]);
    path full_path = dir / dataset;
    int num_workers = atoi(argv[3]);

    std::cout << "Running C++ client with " << num_workers << " workers, reading dataset " << dataset << std::endl;
    // Prepare a list of files to read, recursively iterate through all the files
    std::vector<string> work_queue;
    for(recursive_directory_iterator i(full_path), end; i != end; ++i)
    {
        if (!is_directory(i->path()))
        {
            work_queue.push_back(i->path().string());
        }
    }
    
    std::atomic_uint_fast64_t numBytesRead(0);

    int64_t fileOpsTimer;
    auto startTime = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    TIMED_EXEC(
    {
        std::unique_ptr<ThreadPool> jobPool = std::make_unique<ThreadPool>(num_workers);
        for(auto path : work_queue)
        {
            jobPool->post([path, &numBytesRead]()
            {
                WorkerFunction(path, numBytesRead);
            });
        }
    }, fileOpsTimer);
    auto endTime = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();

    double oneGB = 1024 * 1024 * 1024;
    double GBRead = static_cast<double>(numBytesRead.load()) / oneGB;
    double throughput = 0;
    if(fileOpsTimer == 0)
    {
        std::cout << "File ops took less than a second, cannot compute throughput" << std::endl;
    }
    else
    {
        throughput = GBRead / static_cast<double>(fileOpsTimer);
    }

    std::cout << "##########################################################" << std::endl;
    std::cout << "Client I/O test results summary" << std::endl;
    std::cout << "Bytes read: " << numBytesRead << " in " << fileOpsTimer << " seconds, throughput: " << throughput << " GBps Start time: " << startTime << " End time: " << endTime << std::endl;
    std::cout << "##########################################################" << std::endl;
    return 0;
}
//---------------------------------------------------------------------------------------------------------------------