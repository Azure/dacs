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
#include "ThreadPool.h"
#include <memory>
//---------------------------------------------------------------------------------------------------------------------
#define TIME_NOW std::chrono::high_resolution_clock::now();

#define NANO_INTERVAL(tStart, tEnd) std::chrono::duration_cast<std::chrono::nanoseconds>(tEnd - tStart).count()
#define MICRO_INTERVAL(tStart, tEnd) std::chrono::duration_cast<std::chrono::microseconds>(tEnd - tStart).count()
#define MILLI_INTERVAL(tStart, tEnd) std::chrono::duration_cast<std::chrono::milliseconds>(tEnd - tStart).count()

#define TIMED_EXEC(stmt, timer)       { auto tS = TIME_NOW;  stmt; auto tE = TIME_NOW; timer = static_cast<uint64_t>(NANO_INTERVAL(tS, tE)); }
//---------------------------------------------------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------------------------------------------------
constexpr char PRIMARY_TEST_FILE[] = "/synthetic/small/class-b/primary.bin";
constexpr int PRIMARY_TEST_FILE_SIZE = 324089;
//---------------------------------------------------------------------------------------------------------------------
void TestFileReadWithSeek(const std::string& mountPath)
{
  std::string remoteFilePath = mountPath + PRIMARY_TEST_FILE;
  const size_t expectedFileSize = 324089;

  int fd = ::open(remoteFilePath.c_str(), O_RDONLY, 0777);
  if (fd < 0)
  {
    std::cout << "ERROR: failed to open file: " << remoteFilePath.c_str() << " err=" << strerror(errno) << std::endl;
    throw "open file failed";
  }

  std::unique_ptr<char[]> writeAddr(new char[expectedFileSize]);
  size_t readSize = 0;
  for(int j = 0; j < 4; j ++)
  {
    const ssize_t bytesRead = ::read(fd, writeAddr.get() + readSize, 4096);
    if (bytesRead < 0)
    {
      throw "failed to read file";
    }
    readSize += static_cast<size_t>(bytesRead);
  }
  ::lseek64(fd, 0, SEEK_SET);

  readSize = 0;
  while (readSize < expectedFileSize)
  {
    const size_t toRead = expectedFileSize - readSize;
    ssize_t nBytesReadNow = ::read(fd, writeAddr.get(), toRead);
    if (nBytesReadNow > 0)
    {
      readSize += static_cast<size_t>(nBytesReadNow);
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
void TestSubChunkRead(const std::string& mountPath)
{
  // Test that sub-chunk reads return correct data.
  // The sub-chunk optimization activates automatically for reads smaller than chunk size
  // when encryption and checksum validation are disabled.
  std::string remoteFilePath = mountPath + PRIMARY_TEST_FILE;
  constexpr size_t expectedFileSize = 324089;

  int fd = ::open(remoteFilePath.c_str(), O_RDONLY, 0777);
  if (fd < 0)
  {
    std::cout << "ERROR: SubChunkRead: failed to open file: " << remoteFilePath << " err=" << strerror(errno) << std::endl;
    throw "open file failed";
  }

  // 1. Read the entire file to warm the cache / PrefetchCache
  std::unique_ptr<char[]> fullData(new char[expectedFileSize]);
  size_t totalRead = 0;
  while (totalRead < expectedFileSize)
  {
    ssize_t n = ::read(fd, fullData.get() + totalRead, expectedFileSize - totalRead);
    if (n > 0)
      totalRead += static_cast<size_t>(n);
    else if (n < 0)
    {
      std::cout << "ERROR: SubChunkRead: warmup read failed err=" << strerror(errno) << std::endl;
      throw "warmup read failed";
    }
    else
      break;
  }

  if (totalRead != expectedFileSize)
  {
    std::cout << "ERROR: SubChunkRead: warmup read incomplete expected=" << expectedFileSize
              << " got=" << totalRead << std::endl;
    throw "warmup read incomplete";
  }

  // 2. Seek back to start and do a small 4KB read (sub-chunk candidate)
  constexpr size_t smallReadSize = 4096;
  std::unique_ptr<char[]> smallBuf(new char[smallReadSize]);

  ::lseek64(fd, 0, SEEK_SET);
  ssize_t bytesRead = ::read(fd, smallBuf.get(), smallReadSize);
  if (bytesRead != static_cast<ssize_t>(smallReadSize))
  {
    std::cout << "ERROR: SubChunkRead: small read at offset 0 returned " << bytesRead << std::endl;
    throw "small read failed";
  }
  if (::memcmp(fullData.get(), smallBuf.get(), smallReadSize) != 0)
  {
    std::cout << "ERROR: SubChunkRead: data mismatch at offset 0" << std::endl;
    throw "sub-chunk data mismatch";
  }

  // 3. Seek to mid-file and do another small read
  off64_t midOffset = expectedFileSize / 2;
  ::lseek64(fd, midOffset, SEEK_SET);
  bytesRead = ::read(fd, smallBuf.get(), smallReadSize);
  if (bytesRead != static_cast<ssize_t>(smallReadSize))
  {
    std::cout << "ERROR: SubChunkRead: small read at offset " << midOffset << " returned " << bytesRead << std::endl;
    throw "small read at offset failed";
  }
  if (::memcmp(fullData.get() + midOffset, smallBuf.get(), smallReadSize) != 0)
  {
    std::cout << "ERROR: SubChunkRead: data mismatch at offset " << midOffset << std::endl;
    throw "sub-chunk data mismatch at offset";
  }

  // 4. Close and reopen to test cache-server path (Close purges PrefetchCache)
  ::close(fd);
  fd = ::open(remoteFilePath.c_str(), O_RDONLY, 0777);
  if (fd < 0)
  {
    std::cout << "ERROR: SubChunkRead: failed to reopen file err=" << strerror(errno) << std::endl;
    throw "reopen file failed";
  }

  bytesRead = ::read(fd, smallBuf.get(), smallReadSize);
  if (bytesRead != static_cast<ssize_t>(smallReadSize))
  {
    std::cout << "ERROR: SubChunkRead: small read after reopen returned " << bytesRead << std::endl;
    throw "small read after reopen failed";
  }
  if (::memcmp(fullData.get(), smallBuf.get(), smallReadSize) != 0)
  {
    std::cout << "ERROR: SubChunkRead: data mismatch after reopen" << std::endl;
    throw "sub-chunk data mismatch after reopen";
  }

  ::close(fd);
}
//---------------------------------------------------------------------------------------------------------------------
void TestSubChunkBoundaryRead(const std::string& mountPath)
{
  (void) mountPath;
  // Test that a small 4KB read straddling a chunk boundary returns correct data.
  // Uses STORAGE_INTEG_TESTCASE_FILE to find a file larger than the chunk size.
  constexpr size_t defaultChunkSize = 4 * 1024 * 1024; // 4MB — matches code default
  constexpr size_t smallReadSize = 4096;
  constexpr size_t overlap = 2048;
  const size_t boundaryOffset = defaultChunkSize - overlap;
  const size_t minFileSize = boundaryOffset + smallReadSize;

  const char* testCaseFile = getenv("STORAGE_INTEG_TESTCASE_FILE");
  if (!testCaseFile)
  {
    std::cout << "SKIP: SubChunkBoundaryRead: STORAGE_INTEG_TESTCASE_FILE not set" << std::endl;
    return;
  }

  std::ifstream ifil(testCaseFile, std::ios::in);
  if (!ifil.is_open())
  {
    std::cout << "SKIP: SubChunkBoundaryRead: cannot open test case file" << std::endl;
    return;
  }

  // First line is number of files
  std::string header;
  std::getline(ifil, header);

  // Find the first file whose size exceeds the chunk boundary + read size
  std::string largeFilePath;
  size_t largeFileSize = 0;
  std::string line;
  while (std::getline(ifil, line))
  {
    auto commaIdx = line.rfind(',');
    if (commaIdx == std::string::npos)
      continue;
    size_t fileSize = static_cast<size_t>(std::atoll(line.substr(commaIdx + 1).c_str()));
    if (fileSize >= minFileSize)
    {
      largeFilePath = line.substr(0, commaIdx);
      largeFileSize = fileSize;
      break;
    }
  }
  ifil.close();

  if (largeFilePath.empty())
  {
    std::cout << "SKIP: SubChunkBoundaryRead: no file >= " << minFileSize
              << " bytes in test case file" << std::endl;
    return;
  }

  std::cout << "SubChunkBoundaryRead: using " << largeFilePath << " (" << largeFileSize << " bytes)" << std::endl;

  int fd = ::open(largeFilePath.c_str(), O_RDONLY, 0777);
  if (fd < 0)
  {
    std::cout << "ERROR: SubChunkBoundaryRead: failed to open file err=" << strerror(errno) << std::endl;
    throw "open file failed";
  }

  // 1. Read the region around the chunk boundary to warm the cache
  //    Read from offset 0 up to boundaryOffset + smallReadSize
  std::unique_ptr<char[]> fullData(new char[minFileSize]);
  size_t totalRead = 0;
  while (totalRead < minFileSize)
  {
    ssize_t n = ::read(fd, fullData.get() + totalRead, minFileSize - totalRead);
    if (n > 0)
      totalRead += static_cast<size_t>(n);
    else if (n < 0)
    {
      std::cout << "ERROR: SubChunkBoundaryRead: warmup read failed err=" << strerror(errno) << std::endl;
      ::close(fd);
      throw "warmup read failed";
    }
    else
      break;
  }

  if (totalRead < minFileSize)
  {
    std::cout << "ERROR: SubChunkBoundaryRead: warmup read incomplete expected=" << minFileSize
              << " got=" << totalRead << std::endl;
    ::close(fd);
    throw "warmup read incomplete";
  }

  // 2. Seek to chunk boundary - 2KB and read 4KB straddling the boundary
  std::unique_ptr<char[]> smallBuf(new char[smallReadSize]);
  ::lseek64(fd, boundaryOffset, SEEK_SET);
  ssize_t bytesRead = ::read(fd, smallBuf.get(), smallReadSize);
  if (bytesRead != static_cast<ssize_t>(smallReadSize))
  {
    std::cout << "ERROR: SubChunkBoundaryRead: straddling read at offset " << boundaryOffset
              << " returned " << bytesRead << std::endl;
    ::close(fd);
    throw "straddling read failed";
  }

  if (::memcmp(fullData.get() + boundaryOffset, smallBuf.get(), smallReadSize) != 0)
  {
    std::cout << "ERROR: SubChunkBoundaryRead: data mismatch at chunk boundary offset "
              << boundaryOffset << std::endl;
    ::close(fd);
    throw "sub-chunk boundary data mismatch";
  }

  // 3. Close and reopen to test cache-server path (Close purges PrefetchCache)
  ::close(fd);
  fd = ::open(largeFilePath.c_str(), O_RDONLY, 0777);
  if (fd < 0)
  {
    std::cout << "ERROR: SubChunkBoundaryRead: failed to reopen file err=" << strerror(errno) << std::endl;
    throw "reopen file failed";
  }

  ::lseek64(fd, boundaryOffset, SEEK_SET);
  bytesRead = ::read(fd, smallBuf.get(), smallReadSize);
  if (bytesRead != static_cast<ssize_t>(smallReadSize))
  {
    std::cout << "ERROR: SubChunkBoundaryRead: straddling read after reopen returned " << bytesRead << std::endl;
    ::close(fd);
    throw "straddling read after reopen failed";
  }

  if (::memcmp(fullData.get() + boundaryOffset, smallBuf.get(), smallReadSize) != 0)
  {
    std::cout << "ERROR: SubChunkBoundaryRead: data mismatch after reopen at offset "
              << boundaryOffset << std::endl;
    ::close(fd);
    throw "sub-chunk boundary data mismatch after reopen";
  }

  ::close(fd);
}
//---------------------------------------------------------------------------------------------------------------------
void TestStatInterception(const std::string& mountPath)
{
  std::string remoteFilePath = mountPath + PRIMARY_TEST_FILE;
  int expectedFileSize = PRIMARY_TEST_FILE_SIZE;

  struct stat statBuf{};
  int ret = stat(remoteFilePath.c_str(), &statBuf);
  if (ret != 0)
  {
    std::cout << "ERROR: failed to stat file: " << remoteFilePath.c_str() << std::endl;
    throw "stat file failed";
  }

  if ((statBuf.st_mode & S_IFREG) != S_IFREG)
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

  if ((statBuf1.st_mode & S_IFREG) != S_IFREG)
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

  struct stat statBuf1{};
  int ret = fstat(fd, &statBuf1);
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

  const long filePosition = ftell(tempFile);
  if (filePosition < 0)
  {
    cout << "ftell failed ret=" << filePosition << "err=" << strerror(errno) << endl;
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

  if (fd < 0)
  {
    std::cout << "ERROR: failed to open file: " << path.c_str() << " err=" << strerror(errno) << std::endl;
    throw "open file failed";
  }

  static constexpr size_t bufferSize = 64 * 1024 * 1024;
  static thread_local std::unique_ptr<char[]> writeAddr(new char[bufferSize]);
  size_t readSize = 0;
  while (readSize < size)
  {
    size_t toRead = std::min(bufferSize, size - readSize);
    ssize_t nBytesReadNow = ::read(fd, writeAddr.get(), toRead);
    if (nBytesReadNow > 0)
    {
      readSize += static_cast<size_t>(nBytesReadNow);
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
        if (dir->d_type != DT_UNKNOWN)
        {
          if ((dir->d_type & DT_DIR) == DT_DIR)
          {
            nDirs++;
          }
          else if ((dir->d_type & DT_REG) == DT_REG)
          {
            nFiles++;
          }
        }
        else
        {
          struct stat st;
          std::string fullPath = dirPath + "/" + dir->d_name;
          if (::stat(fullPath.c_str(), &st) == 0)
          {
            if (S_ISDIR(st.st_mode))
            {
              nDirs++;
            }
            else if (S_ISREG(st.st_mode))
            {
              nFiles++;
            }
          }
        }
      }
      ::closedir(d);
    }
  }, dirOpTimer);
  /* End directory listing */

  if (nDirs != expectedDirCount)
  {
    std::cout << "No of dir entries for path=" << dirPath << " nDirs=" << nDirs << " expectedDirCount=" << expectedDirCount << std::endl;
    throw "Incorrect number of dir entries returned";
  }
  if (nFiles != expectedFileCount)
  {
    std::cout << "No of dir entries for path=" << dirPath << " nFiles=" << nFiles << " expectedFileCount=" << expectedFileCount << std::endl;
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
  std::string remoteFilePath = mountPath + PRIMARY_TEST_FILE;
  int expectedFileSize = PRIMARY_TEST_FILE_SIZE;

  std::ifstream in(remoteFilePath, std::ios::in | std::ios::binary);
  if (!in)
  {
    std::cout << "couldn't open remote file: " << remoteFilePath.c_str() << " error:" << strerror(errno) << std::endl;
    throw "couldn't open remote file";
  }

  in.seekg(0, std::ios::end);

  if (in.fail())
  {
    std::cout << "seekg failed: " << strerror(errno) << std::endl;
    throw "seekg failed";
  }
  if (in.bad())
  {
    std::cout << "seekg failed: " << strerror(errno) << std::endl;
    throw "seekg failed";
  }
  if (in.eof())
  {
    std::cout << "seekg failed: " << strerror(errno) << std::endl;
    throw "seekg failed";
  }

  const std::streamoff size = in.tellg();
  if (size != static_cast<std::streamoff>(expectedFileSize))
  {
    std::cout << "remote file size mismatch. " << size << " != " << expectedFileSize << std::endl;
    throw "remote file size mismatch";
  }

  in.seekg(0, std::ios::beg);
  char* buffer = new char[static_cast<size_t>(size)];
  in.read(buffer, static_cast<std::streamsize>(size));
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
  TestReaddir(mountPath + "/synthetic", 4, 4);
  TestReaddir(mountPath + "/synthetic/small", 4, 0);
  TestReaddir(mountPath + "/synthetic/small/class-b", 4, 10);
  std::cout << "Tests for list directory calls done." << std::endl;

  TestIfStreamInterception(mountPath);
  std::cout << "Test using std::ifstream done." << std::endl;

  TestFileReadWithSeek(mountPath);
  std::cout << "Test read with seek done." << std::endl;

  TestSubChunkRead(mountPath);
  std::cout << "Test sub-chunk read done." << std::endl;

  TestSubChunkBoundaryRead(mountPath);
  std::cout << "Test sub-chunk boundary read done." << std::endl;

  TestStatInterception(mountPath);
  std::cout << "Test read with stat done." << std::endl;

  TestInterceptionFallback();
  std::cout << "Test APIs with fallback done." << std::endl;

  std::ifstream ifil(getenv("STORAGE_INTEG_TESTCASE_FILE"), std::ios::in);
  std::string numFiles;
  std::getline(ifil, numFiles);
  uint64_t numFilesInt = std::stoull(numFiles);
  std::vector<std::pair<std::string, size_t>> samples;

  uint64_t numIterations = std::stoull(argv[2]);
  for(uint64_t lineNo = 1; lineNo <= std::min(numIterations, numFilesInt); lineNo++)
  {
    std::string fileData;
    std::getline(ifil, fileData);
    const size_t commaIdx = fileData.rfind(',');
    if (commaIdx == std::string::npos)
    {
      throw "invalid test case file entry";
    }
    size_t fileSize = std::stoull(fileData.substr(commaIdx + 1));

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
  const double avgThroughput = static_cast<double>(numBytesRead.load()) * 1000000000.0
      / static_cast<double>(fileOpsTimer);

  /* Printing the test results */
  std::cout << "========================================================" << std::endl;
  std::cout << "Integration Tests Completed; Results: " << std::endl;
  std::cout << "========================================================" << std::endl;
  std::cout << "Time taken for file ops(ns) => " << fileOpsTimer  << std::endl;
  std::cout << "Num files visited           => " << filesVisited  << std::endl;
  std::cout << "Num bytes read              => " << numBytesRead  << std::endl;
  std::cout << "Average throughput obtained => " << avgThroughput <<  " B / sec"  << std::endl;
  std::cout << "========================================================" << std::endl;
  
  // Allow time for async cache uploads to complete before program exit.
  // File reads trigger background cache uploads in CacheUploadManager. 
  // Without this sleep, the ContentHandler destructor can run while upload threads
  // are still active, causing segfaults when threads access destroyed objects.
  std::this_thread::sleep_for(std::chrono::milliseconds(1000));

  return 0;
}
//---------------------------------------------------------------------------------------------------------------------