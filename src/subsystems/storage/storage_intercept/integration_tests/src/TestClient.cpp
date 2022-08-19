//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
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
#include "ThreadPool.h"
#include <ext/stdio_filebuf.h>
#include <memory>
//---------------------------------------------------------------------------------------------------------------------
#define TIME_NOW std::chrono::high_resolution_clock::now();

#define NANO_INTERVAL(tStart, tEnd) std::chrono::duration_cast<std::chrono::nanoseconds>(tEnd - tStart).count();
#define MICRO_INTERVAL(tStart, tEnd) std::chrono::duration_cast<std::chrono::microseconds>(tEnd - tStart).count();
#define MILLI_INTERVAL(tStart, tEnd) std::chrono::duration_cast<std::chrono::milliseconds>(tEnd - tStart).count();

#define TIMED_EXEC(stmt, timer)       { auto tS = TIME_NOW;  stmt; auto tE = TIME_NOW; timer = NANO_INTERVAL(tS, tE); }
//---------------------------------------------------------------------------------------------------------------------
void TestFileRead(
  std::string path,
  size_t size,
  std::atomic_uint_fast64_t &filesVisited,
  std::atomic_uint_fast64_t &numBytesRead)
{
  int fd = ::open(path.c_str(), O_RDONLY, 0777);

  ssize_t readSize = 0;
  std::string fileData;
  fileData.resize(size, '0');
  char* writeAddr = &(*fileData.begin());
  
  for(int j = 0; j < 4; j ++)
  {
    readSize += ::read(fd, writeAddr + readSize, 4096);
  }
  
  ::lseek64(fd, 0, SEEK_SET);
  readSize = 0;
  
  while (true)
  {
    uint64_t toRead = std::min(64UL * 1024UL * 1024UL, size);
    ssize_t nBytesReadNow = ::read(fd, writeAddr + readSize, toRead);
    if (nBytesReadNow > 0)
    {
      readSize += nBytesReadNow;
    }
    else
    {
      break;
    }
  }
  fileData.clear();
  ::close(fd);

  filesVisited++;
  numBytesRead += readSize;
}
//---------------------------------------------------------------------------------------------------------------------
void TestReaddir(
  const std::string& dirPath,
  unsigned int expectedDirCount,
  unsigned int expectedFileCount)
{
  DIR *d;
  struct dirent64 *dir;
  unsigned int nFiles = 0, nDirs = 0;
  
  /* Start directory read */
  uint64_t dirOpTimer;
  TIMED_EXEC(
  {
    d = ::opendir(dirPath.c_str());
    if (d)
    {
      while ((dir = ::readdir64(d)) != NULL)
      {
        if (dir->d_type == DT_DIR)
        {
          nDirs++;
        }
        else if (dir->d_type == DT_REG)
        {
          nFiles++;
        }
      }
      ::closedir(d);
    }
  }, dirOpTimer);
  /* End directory listing */

  if (nDirs != expectedDirCount)
  {
    throw "Incorrect number of dir entries returned";
  }
  if (nFiles != expectedFileCount)
  {
    throw "Incorrect number of file entries returned";
  }

  /* Printing the results */
  std::cout << "========================================================" << std::endl;
  std::cout << "List directory Test completed; Results:"                  << std::endl;
  std::cout << "========================================================" << std::endl;
  std::cout << "Remote test conducted on path => " << dirPath << std::endl;
  std::cout << "Number of sub-directories     => " << nDirs << std::endl;
  std::cout << "Number of files in directory  => " << nFiles << std::endl;
  std::cout << "Time taken for operation (ns) => " << dirOpTimer << std::endl;
  std::cout << "========================================================" << std::endl;
}
//---------------------------------------------------------------------------------------------------------------------
void TestIfStreamInterception()
{
  std::string remoteFilePath = "/mnt/quiverfs/imagenet_data/train/n01494475/n01494475_10008.JPEG";
  int expectedFileSize = 324089;

  std::ifstream in(remoteFilePath, std::ios::in | std::ios::binary);
  if (!in)
  {
    std::cout << "couldn't open remote file: " << remoteFilePath.c_str() << std::endl;
    throw "couldn't open remote file";
  }

  in.seekg(0, std::ios::end);
  int size = in.tellg();
  if (size != expectedFileSize)
  {
    std::cout << "remote file size mismatch. " << size << " != " << expectedFileSize << std::endl;
    throw "remote file size mismatch";
  }

  in.seekg(0, std::ios::beg);
  char* buffer = new char[size];
  in.read(buffer, size);
  in.close();

  std::cout << "Readfile using std::ifstream succeeded." << std::endl;
}
//---------------------------------------------------------------------------------------------------------------------
int main(int argc, char** argv)
{
  std::cout << "Testing the list directory calls" << std::endl;
  TestReaddir("/mnt/quiverfs/imagenet_data/", 4, 0);
  TestReaddir("/mnt/quiverfs/imagenet_data/train", 102, 0);
  TestReaddir("/mnt/quiverfs/imagenet_data/train/n01494475", 2, 1300);
  std::cout << "Tests for list directory calls done." << std::endl;

  TestIfStreamInterception();
  std::cout << "Test using std::ifstream done." << std::endl;

  std::ifstream ifil(getenv("STORAGE_INTEG_TESTCASE_FILE"), std::ios::in);
  std::string numFiles;
  std::getline(ifil, numFiles);
  uint64_t numFilesInt = ::atoi(numFiles.c_str());
  std::vector<std::pair<std::string, size_t>> samples;

  for(uint64_t lineNo = 1; lineNo <= numFilesInt; lineNo++)
  {
    std::string fileData;
    std::getline(ifil, fileData);
    int commaIdx = fileData.rfind(',');
    size_t fileSize = ::atoi(fileData.substr(commaIdx + 1).c_str());

    samples.push_back(make_pair(fileData.substr(0, commaIdx), fileSize));
  }
  
  std::cout << "Number of samples in the file : " << samples.size() << std::endl;  
  ifil.close();

  if (argc < 3)
  {
    std::cout << "Too few arguments provided, terminating" << std::endl;
    exit(0);
  }

  std::cout << "Testing the file open / read / close calls" << std::endl;
  uint64_t numIterations = ::atoi(argv[1]);
  int parallelism = ::atoi(argv[2]);

  std::atomic_uint_least64_t filesVisited(0);
  std::atomic_uint_fast64_t numBytesRead(0);
  
  uint64_t fileOpsTimer;
  TIMED_EXEC(
  {
    std::unique_ptr<ThreadPool> jobPool = std::make_unique<ThreadPool>(parallelism);
    for(size_t i = 0; i < numIterations; i++)
    {
      std::string path = samples[i].first;
      size_t size = samples[i].second;

      jobPool->post([path, size, &filesVisited, &numBytesRead]()
      {
        TestFileRead(path, size, filesVisited, numBytesRead);
      });
    }
  }, fileOpsTimer);

  std::cout << "Tests for file open / read / close done." << std::endl;
  const double avgThroughput = static_cast<double>((numBytesRead * 1000000000.000) / fileOpsTimer);

  /* Printing the test results */
  std::cout << "========================================================" << std::endl;
  std::cout << "Integration Tests Completed; Results: " << std::endl;
  std::cout << "========================================================" << std::endl;
  std::cout << "Time taken for file ops(ns) => " << fileOpsTimer  << std::endl;
  std::cout << "Num files visited           => " << filesVisited  << std::endl;
  std::cout << "Num bytes read              => " << numBytesRead  << std::endl;
  std::cout << "Average throughput obtained => " << avgThroughput <<  " B / sec"  << std::endl;
  std::cout << "========================================================" << std::endl;
  return 0;
}
//---------------------------------------------------------------------------------------------------------------------