package main

/*
#cgo CFLAGS: -Doff64_t=__off64_t
#cgo LDFLAGS: -lCacheClient -luuid
#include <CacheClientInterface.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

static size_t getFileSize(int fd, char** errorPtr)
{
	struct stat buf;
	int ret = SI_fstat(fd, &buf);
	if (ret == -1)
	{
		if (errorPtr != NULL)
			*errorPtr = SI_getLastError();
		return -1;
	}

	return buf.st_size;
}

static int fileOpen(char* s, char** errorPtr)
{
	int fd = SI_open(s, O_RDONLY, 0644);
	if (fd == -1)
	{
		if (errorPtr != NULL)
			*errorPtr = SI_getLastError();
	}

	return fd;
}

static void fileClose(int fd, char** errorPtr)
{
	int ret = SI_close(fd);
	if (ret == -1)
	{
		if (errorPtr != NULL)
			*errorPtr = SI_getLastError();
	}
}

static ssize_t filePread(int fd, void *buf, size_t count, off_t offset, char** errorPtr)
{
	ssize_t ret = SI_pread(fd, buf, count, offset);
	if(ret == -1)
	{
		if (errorPtr != NULL)
			*errorPtr = SI_getLastError();
	}

	return ret;
}
*/
import "C"

import (
	"fmt"
	"unsafe"
	"os"
)

func main() {
	fileName := C.CString("sample_file.txt")
	defer C.free(unsafe.Pointer(fileName))

	var errorPtr *C.char
	fileFd := C.fileOpen(fileName, &errorPtr)
	if fileFd == -1 {
		fmt.Println("fileOpen failed")
		if errorPtr != nil {
			fmt.Println("ErrorHandler reported:", C.GoString(errorPtr))
		}
		C.free(unsafe.Pointer(errorPtr))
		os.Exit(1)
	}

	errorPtr = nil
	fileSize := int64(C.getFileSize(C.int(fileFd), &errorPtr))
	if fileSize == -1 {
		fmt.Println("getFileSize failed")
		if errorPtr != nil {
			fmt.Println("ErrorHandler reported:", C.GoString(errorPtr))
		}
		C.free(unsafe.Pointer(errorPtr))
		os.Exit(1)
	}

	fmt.Println("fileSize:", fileSize)
	errorPtr = nil
	count := 10
	offset := 0
	buf := make([]byte, fileSize)

	bytes_read := int64(C.filePread(C.int(fileFd), unsafe.Pointer(&buf[0]), C.ulong(count), C.long(offset), &errorPtr))
	if bytes_read == -1 {
		fmt.Println("pread failed")
		if errorPtr != nil {
			fmt.Println("ErrorHandler reported:", C.GoString(errorPtr))
		}
		C.free(unsafe.Pointer(errorPtr))
		os.Exit(1)
	}

	fmt.Println("bytes_read:", bytes_read)
	errorPtr = nil

	errorPtr = C.SI_getLastError()
	if errorPtr != nil {
		fmt.Println("ErrorHandler reported:", C.GoString(errorPtr))
		C.free(unsafe.Pointer(errorPtr))
	}

	errorPtr = nil
	C.fileClose(C.int(fileFd), &errorPtr)
	if errorPtr != nil {
		fmt.Println("ErrorHandler reported:", C.GoString(errorPtr))
		C.free(unsafe.Pointer(errorPtr))
	}
}