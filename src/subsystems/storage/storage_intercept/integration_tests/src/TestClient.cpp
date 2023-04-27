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
#include <errno.h>
#include <cstring>
#include "ThreadPool.h"
#include <memory>
//---------------------------------------------------------------------------------------------------------------------
#define TIME_NOW std::chrono::high_resolution_clock::now();

#define NANO_INTERVAL(tStart, tEnd) std::chrono::duration_cast<std::chrono::nanoseconds>(tEnd - tStart).count();
#define MICRO_INTERVAL(tStart, tEnd) std::chrono::duration_cast<std::chrono::microseconds>(tEnd - tStart).count();
#define MILLI_INTERVAL(tStart, tEnd) std::chrono::duration_cast<std::chrono::milliseconds>(tEnd - tStart).count();

#define TIMED_EXEC(stmt, timer)       { auto tS = TIME_NOW;  stmt; auto tE = TIME_NOW; timer = NANO_INTERVAL(tS, tE); }
//---------------------------------------------------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------------------------------------------------
void TestFileReadWithSeek(const std::string& mountPath)
{
  std::string remoteFilePath = mountPath + "/imagenet_data/train/n01494475/n01494475_10008.JPEG";
  int expectedFileSize = 324089;

  int fd = ::open(remoteFilePath.c_str(), O_RDONLY, 0777);

  std::unique_ptr<char[]> writeAddr(new char[expectedFileSize]);
  size_t readSize = 0;
  for(int j = 0; j < 4; j ++)
  {
    readSize += ::read(fd, writeAddr.get() + readSize, 4096);
  }
  ::lseek64(fd, 0, SEEK_SET);

  readSize = 0;
  while (readSize < expectedFileSize)
  {
    const size_t toRead = expectedFileSize - readSize;
    ssize_t nBytesReadNow = ::read(fd, writeAddr.get(), toRead);
    if (nBytesReadNow > 0)
    {
      readSize += nBytesReadNow;
    }
    else if (nBytesReadNow < 0)
    {
      std::cout << "ERROR: failed to read() err: " << fd << "," << nBytesReadNow << "," << toRead
        << "," << strerror(errno) << std::endl;
      throw "failed to read file";
    }
    else
    {
      break;
    }
  }
  ::close(fd);

  if (expectedFileSize != readSize)
  {
    std::cout << "ERROR: read bytes mismatch expected: " << expectedFileSize << ", read: " << readSize << std::endl;
    throw "read file incomplete";
  }
}
//---------------------------------------------------------------------------------------------------------------------
void TestStatInterception(const std::string& mountPath)
{
  std::string remoteFilePath = mountPath + "/imagenet_data/train/n01494475/n01494475_10008.JPEG";
  int expectedFileSize = 324089;

  struct stat statBuf{};
  int ret = stat(remoteFilePath.c_str(), &statBuf);
  if (ret != 0)
  {
    std::cout << "ERROR: failed to stat file: " << remoteFilePath.c_str() << std::endl;
    throw "stat file failed";
  }

  if (statBuf.st_mode & S_IFREG != S_IFREG)
  {
    std::cout << "ERROR: expected regular file, got: " << statBuf.st_mode << std::endl;
    throw "stat file failed";
  }

  if (expectedFileSize != statBuf.st_size)
  {
    std::cout << "ERROR: filesize bytes mismatch expected: " << expectedFileSize << ", read: "
      << statBuf.st_size << std::endl;
    throw "stat file failed";
  }

  int fd = ::open(remoteFilePath.c_str(), O_RDONLY, 0777);
  if (fd < 0)
  {
    std::cout << "ERROR: failed to open file: " << remoteFilePath.c_str() << std::endl;
    throw "open file failed";
  }

  struct stat statBuf1{};
  ret = fstat(fd, &statBuf1);
  if (ret != 0)
  {
    std::cout << "ERROR: failed to fstat file: " << remoteFilePath.c_str() << std::endl;
    throw "fstat file failed";
  }

  if (statBuf1.st_mode & S_IFREG != S_IFREG)
  {
    std::cout << "ERROR: expected regular file, got: " << statBuf1.st_mode << std::endl;
    throw "stat file failed";
  }

  ::close(fd);

  if (expectedFileSize != statBuf1.st_size)
  {
    std::cout << "ERROR: filesize bytes mismatch expected: " << expectedFileSize << ", read: "
      << statBuf1.st_size << std::endl;
    throw "fstat file failed";
  }
}
//---------------------------------------------------------------------------------------------------------------------
void TestInterceptionFallback()
{
  char tmpFileName[] = "/tmp/interception_XXXXXX";
  int fd = mkstemp(tmpFileName);
  if (fd < 0)
  {
    std::cout << "mkstemp failed: "<< strerror(errno) <<endl;
    throw "mkstemp failed";
  }

  struct stat statBuf{};
  int ret = stat(tmpFileName, &statBuf);
  if (ret != 0)
  {
    std::cout << "stat failed: "<< strerror(errno) <<endl;
    throw "mkstemp failed";
  }

  struct stat statBuf1{};
  ret = fstat(fd, &statBuf1);
  if (ret != 0)
  {
    std::cout << "fstat failed: "<< strerror(errno) <<endl;
    throw "mkstemp failed";
  }

  ret = close(fd);
  if (ret < 0)
  {
    cout << "close failed ret=" << ret << "err=" << strerror(errno) << endl;
    throw "close failed";
  }

  FILE* tempFile = fopen64(tmpFileName, "w+");
  if (!tempFile)
  {
    std::cout << "fopen64 failed: "<< strerror(errno) <<endl;
    throw "fopen64 failed";
  }

  ret = setvbuf(tempFile, nullptr, _IONBF, 100);
  if (ret != 0)
  {
    cout << "setvbuf failed ret=" << ret << "err=" << strerror(errno) << endl;
    throw "setvbuf failed";
  }

  ret = fclose(tempFile);
  if (ret != 0)
  {
    cout << "fclose failed ret=" << ret << "err=" << strerror(errno) << endl;
    throw "fclose failed";
  }

  tempFile = fopen(tmpFileName, "r");
  if (!tempFile)
  {
    std::cout << "fopen failed: "<< strerror(errno) <<endl;
    throw "fopen failed";
  }

  char buffer[100];
  setbuf(tempFile, buffer);
  setbuffer(tempFile, buffer, 50);

  tempFile = freopen(nullptr, "w+", tempFile);
  if (!tempFile)
  {
    std::cout << "freopen failed: "<< strerror(errno) <<endl;
    throw "freopen failed";
  }

  setlinebuf(tempFile);

  ret = fprintf(tempFile, "test string: %s\n", "test");
  if (ret < 0)
  {
    cout << "fprintf failed ret=" << ret << "err=" << strerror(errno) << endl;
    throw "fprintf failed";
  }

  ret = fseek(tempFile, 0, SEEK_SET);
  if (ret != 0)
  {
    cout << "fseek failed ret=" << ret << "err=" << strerror(errno) << endl;
    throw "fseek failed";
  }

  char testStr[100] = {};
  ret = fscanf(tempFile, "test string: %s", testStr);
  if (ret != 1 || strcmp(testStr, "test") != 0)
  {
    cout << "fscanf failed ret=" << ret << "err=" << strerror(errno) << endl;
    throw "fscanf failed";
  }

  ret = ftell(tempFile);
  if (ret < 0)
  {
    cout << "ftell failed ret=" << ret << "err=" << strerror(errno) << endl;
    throw "ftell failed";
  }

  ret = fputs("another line", tempFile);
  if (ret < 0)
  {
    cout << "fputs failed ret=" << ret << "err=" << strerror(errno) << endl;
    throw "fputs failed";
  }

  fpos_t pos{};
  pos.__pos = 17;
  ret = fsetpos(tempFile, &pos);
  if (ret != 0)
  {
    cout << "fsetpos failed ret=" << ret << "err=" << strerror(errno) << endl;
    throw "fsetpos failed";
  }

  pos.__pos = 0;
  ret = fgetpos(tempFile, &pos);
  if (ret != 0 || pos.__pos != 17)
  {
    cout << "fgetpos failed ret=" << ret << "err=" << strerror(errno) << endl;
    throw "fgetpos failed";
  }

  char* retStr = fgets(testStr, 8, tempFile);
  if (retStr == nullptr || strcmp(testStr, "another") != 0)
  {
    cout << "fgets failed ret=" << ret << "err=" << strerror(errno) << endl;
    throw "fgets failed";
  }

  fpos64_t pos64{};
  pos64.__pos = 17;
  ret = fsetpos64(tempFile, &pos64);
  if (ret != 0)
  {
    cout << "fsetpos64 failed ret=" << ret << "err=" << strerror(errno) << endl;
    throw "fsetpos64 failed";
  }

  pos64.__pos = 0;
  ret = fgetpos64(tempFile, &pos64);
  if (ret != 0 || pos64.__pos != 17)
  {
    cout << "fgetpos64 failed ret=" << ret << "err=" << strerror(errno) << endl;
    throw "fgetpos64 failed";
  }

  retStr = fgets(testStr, 8, tempFile);
  if (retStr == nullptr || strcmp(testStr, "another") != 0)
  {
    cout << "fgets failed ret=" << ret << "err=" << strerror(errno) << endl;
    throw "fgets failed";
  }

  rewind(tempFile);
  retStr = fgets(testStr, 5, tempFile);
  if (retStr == nullptr || strcmp(testStr, "test") != 0)
  {
    cout << "fgets failed ret=" << ret << "err=" << strerror(errno) << endl;
    throw "fgets failed";
  }

  rewind(tempFile);
  ret = fputc('s', tempFile);
  if (ret != 's')
  {
    cout << "fputc failed ret=" << ret << "err=" << strerror(errno) << endl;
    throw "fputc failed";
  }

  rewind(tempFile);
  ret = fgetc(tempFile);
  if (ret != 's')
  {
    cout << "fgetc failed ret=" << ret << "err=" << strerror(errno) << endl;
    throw "fgetc failed";
  }

  ret = ungetc('s', tempFile);
  if (ret != 's')
  {
    cout << "ungetc failed ret=" << ret << "err=" << strerror(errno) << endl;
    throw "ungetc failed";
  }

  ret = fgetc(tempFile);
  if (ret != 's')
  {
    cout << "fgetc failed ret=" << ret << "err=" << strerror(errno) << endl;
    throw "fgetc failed";
  }

  rewind(tempFile);
  size_t retSize = fwrite("new", 3, 1, tempFile);
  if (retSize != 1)
  {
    cout << "fwrite failed ret=" << ret << "err=" << strerror(errno) << endl;
    throw "fwrite failed";
  }

  rewind(tempFile);
  retSize = fread(testStr, 3, 1, tempFile);
  if (retSize != 1 || strncmp(testStr, "new", 3) != 0)
  {
    cout << "fread failed ret=" << ret << "err=" << strerror(errno) << endl;
    throw "fread failed";
  }

  ret = fileno(tempFile);
  if (ret < 0)
  {
    cout << "fileno failed ret=" << ret << "err=" << strerror(errno) << endl;
    throw "fileno failed";
  }

  ret = fflush(tempFile);
  if (ret != 0)
  {
    cout << "fflush failed ret=" << ret << "err=" << strerror(errno) << endl;
    throw "fflush failed";
  }

  ret = ferror(tempFile);
  if (ret != 0)
  {
    cout << "ferror failed ret=" << ret << "err=" << strerror(errno) << endl;
    throw "ferror failed";
  }

  ret = feof(tempFile);
  if (ret != 0)
  {
    cout << "feof failed ret=" << ret << "err=" << strerror(errno) << endl;
    throw "feof failed";
  }

  clearerr(tempFile);
  ret = ferror(tempFile);
  if (ret != 0)
  {
    cout << "ferror failed ret=" << ret << "err=" << strerror(errno) << endl;
    throw "ferror failed";
  }

  ret = fclose(tempFile);
  if (ret != 0)
  {
    cout << "fclose failed ret=" << ret << "err=" << strerror(errno) << endl;
    throw "fclose failed";
  }

  ret = unlink(tmpFileName);
  if (ret != 0)
  {
    cout << "unlink failed ret=" << ret << "err=" << strerror(errno) << endl;
    throw "unlink failed";
  }
}
//---------------------------------------------------------------------------------------------------------------------
void TestFileReadPerf(
  const std::string& path,
  size_t size,
  std::atomic_uint_fast64_t &filesVisited,
  std::atomic_uint_fast64_t &numBytesRead)
{
  int fd = ::open(path.c_str(), O_RDONLY, 0777);

  static constexpr size_t bufferSize = 64 * 1024 * 1024;
  static thread_local std::unique_ptr<char[]> writeAddr(new char[bufferSize]);
  size_t readSize = 0;
  while (readSize < size)
  {
    size_t toRead = std::min(bufferSize, size - readSize);
    ssize_t nBytesReadNow = ::read(fd, writeAddr.get(), toRead);
    if (nBytesReadNow > 0)
    {
      readSize += nBytesReadNow;
    }
    else if (nBytesReadNow < 0)
    {
      std::cout << "ERROR: failed to read() err: " << fd << "," << nBytesReadNow << "," << toRead
        << "," << strerror(errno) << std::endl;
      throw "failed to read file";
    }
    else
    {
      break;
    }
  }
  ::close(fd);

  if (size != readSize)
  {
    std::cout << "ERROR: read bytes mismatch expected: " << size << ", read: " << readSize << std::endl;
    throw "read file incomplete";
  }

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
void TestIfStreamInterception(const std::string& mountPath)
{
  std::string remoteFilePath = mountPath + "/imagenet_data/train/n01494475/n01494475_10008.JPEG";
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
  if (argc < 4)
  {
    std::cout << "Too few arguments provided, terminating" << std::endl;
    exit(0);
  }

  std::string mountPath = argv[1];

  std::cout << "Testing the list directory calls" << std::endl;
  TestReaddir(mountPath + "/imagenet_data/", 4, 0);
  TestReaddir(mountPath + "/imagenet_data/train", 102, 0);
  TestReaddir(mountPath + "/imagenet_data/train/n01494475", 2, 1300);
  std::cout << "Tests for list directory calls done." << std::endl;

  TestIfStreamInterception(mountPath);
  std::cout << "Test using std::ifstream done." << std::endl;

  TestFileReadWithSeek(mountPath);
  std::cout << "Test read with seek done." << std::endl;

  TestStatInterception(mountPath);
  std::cout << "Test read with stat done." << std::endl;

  TestInterceptionFallback();
  std::cout << "Test APIs with fallback done." << std::endl;

  std::ifstream ifil(getenv("STORAGE_INTEG_TESTCASE_FILE"), std::ios::in);
  std::string numFiles;
  std::getline(ifil, numFiles);
  uint64_t numFilesInt = ::atoi(numFiles.c_str());
  std::vector<std::pair<std::string, size_t>> samples;

  uint64_t numIterations = ::atoi(argv[2]);
  for(uint64_t lineNo = 1; lineNo <= std::min(numIterations, numFilesInt); lineNo++)
  {
    std::string fileData;
    std::getline(ifil, fileData);
    int commaIdx = fileData.rfind(',');
    size_t fileSize = ::atoi(fileData.substr(commaIdx + 1).c_str());

    samples.push_back(make_pair(fileData.substr(0, commaIdx), fileSize));
  }
  
  std::cout << "Number of samples loaded from file : " << samples.size() << std::endl;
  ifil.close();

  std::cout << "Testing the file open / read / close calls" << std::endl;
  int parallelism = ::atoi(argv[3]);

  std::atomic_uint_least64_t filesVisited(0);
  std::atomic_uint_fast64_t numBytesRead(0);

  uint64_t fileOpsTimer;
  TIMED_EXEC(
  {
    std::unique_ptr<ThreadPool> jobPool = std::make_unique<ThreadPool>(parallelism);
    for(size_t i = 0; i < numIterations; i++)
    {
      std::string path = samples[i % numFilesInt].first;
      size_t size = samples[i % numFilesInt].second;

      jobPool->post([path, size, &filesVisited, &numBytesRead]()
      {
        TestFileReadPerf(path, size, filesVisited, numBytesRead);
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