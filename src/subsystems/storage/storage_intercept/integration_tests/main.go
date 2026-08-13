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
	int ret = SI_fstat(fd, &buf, errorPtr);
	if (ret == -1)
	{
		return -1;
	}

	return buf.st_size;
}

static int fileOpen(char* s, char** errorPtr)
{
	return SI_open(s, O_RDONLY, 0644, errorPtr);
}
*/
import "C"

import (
	"fmt"
	"unsafe"
	"os"
	"time"
)

func OpenRoutine(fileNameStr string) int {
	fileName := C.CString(fileNameStr)
	retValue := 1
	defer C.free(unsafe.Pointer(fileName))

	var errorPtr *C.char
	fileFd := C.fileOpen(fileName, &errorPtr)
	if fileFd == -1 {
		fmt.Println("open failed")
		if errorPtr != nil {
			fmt.Println("Error reported:", C.GoString(errorPtr))
			retValue = 0
		}
		C.free(unsafe.Pointer(errorPtr))
	}

	return retValue
}

func main() {
	// Assume that the intercept path is /mnt/quiverfs
	// Also we assume that the intercept config has an incorrect sas token
	go OpenRoutine("/mnt/quiverfs/dataset/testFileForUt")
	go OpenRoutine("/mnt/quiverfs/dataset/testDirForUt/file-1")

	time.Sleep(1 * time.Second)
	ret := OpenRoutine("/mnt/quiverfs/dataset/testFileForUt")
	os.Exit(ret)
}

	// errorPtr = nil
	// fileSize := int64(C.getFileSize(C.int(fileFd), &errorPtr))
	// if fileSize == -1 {
	// 	fmt.Println("getFileSize failed")
	// 	if errorPtr != nil {
	// 		fmt.Println("ErrorHandler reported:", C.GoString(errorPtr))
	// 	}
	// 	C.free(unsafe.Pointer(errorPtr))
	// 	os.Exit(1)
	// }

	// fmt.Println("fileSize:", fileSize)
	// errorPtr = nil
	// count := 10
	// offset := 0
	// buf := make([]byte, fileSize)

	// bytes_read := int64(C.filePread(C.int(fileFd), unsafe.Pointer(&buf[0]), C.ulong(count), C.long(offset), &errorPtr))
	// if bytes_read == -1 {
	// 	fmt.Println("pread failed")
	// 	if errorPtr != nil {
	// 		fmt.Println("ErrorHandler reported:", C.GoString(errorPtr))
	// 	}
	// 	C.free(unsafe.Pointer(errorPtr))
	// 	os.Exit(1)
	// }

	// fmt.Println("bytes_read:", bytes_read)
	// errorPtr = nil

	// errorPtr = C.SI_getLastError()
	// if errorPtr != nil {
	// 	fmt.Println("ErrorHandler reported:", C.GoString(errorPtr))
	// 	C.free(unsafe.Pointer(errorPtr))
	// }

	// errorPtr = nil
	// C.fileClose(C.int(fileFd), &errorPtr)
	// if errorPtr != nil {
	// 	fmt.Println("ErrorHandler reported:", C.GoString(errorPtr))
	// 	C.free(unsafe.Pointer(errorPtr))
	// }