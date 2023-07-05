package main

/*
#cgo CFLAGS: -Doff64_t=__off64_t
#cgo LDFLAGS: -lCacheClient -luuid
#include <CacheClientInterface.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

static size_t getFileSize(int fd)
{
	struct stat buf;
	int ret = SI_fstat(fd, &buf);
	if (ret != 0)
	{
		return -1;
	}

	return buf.st_size;
}

static int fileOpen(char* s)
{
	return SI_open(s, O_RDONLY, 0644);
}

static void fileClose(int fd)
{
	SI_close(fd);
}

static const char* checkError()
{
	return SI_getLastError();
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

	fileFd := C.fileOpen(fileName)
	if fileFd == -1 {
		fmt.Println("fileOpen failed")
		ret := C.checkError()
		if ret != nil {
			fmt.Println("ErrorHandler reported:", C.GoString(ret))
		}
		os.Exit(1)
	}

	fileSize := int64(C.getFileSize(C.int(fileFd)))
	if fileSize == -1 {
		fmt.Println("getFileSize failed")
		ret := C.checkError()
		if ret != nil {
			fmt.Println("ErrorHandler reported:", C.GoString(ret))
		}
		os.Exit(1)
	}
	fmt.Println("fileSize:", fileSize)
	count := 10
	offset := 0
	buf := make([]byte, fileSize)

	bytes_read := int64(C.SI_pread(C.int(fileFd), unsafe.Pointer(&buf[0]), C.ulong(count), C.long(offset)))
	if bytes_read == -1 {
		fmt.Println("pread failed")
		ret := C.checkError()
		if ret != nil {
			fmt.Println("ErrorHandler reported:", C.GoString(ret))
		}
		os.Exit(1)
	}

	fmt.Println("bytes_read:", bytes_read)

	C.checkError()
	C.fileClose(C.int(fileFd))
}