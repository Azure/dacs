# Copyright (c) Microsoft Corporation.
# Licensed under the Apache License, Version 2.0.

import argparse
import io
import os
import glob
import zipfile
import secrets
import shutil
import string
import tarfile
import fileinput
from pathlib import Path
from concurrent.futures import ProcessPoolExecutor
import time
import h5py

def timed_exec(lambdaExecutor, args):
    timeBefore = time.time()
    execResult = lambdaExecutor(args)
    timeAfter = time.time()
    timeDiff = timeAfter - timeBefore

    if 'message' in execResult:
        messageArgs = execResult['messageArgs']
        if 'timeTaken' in messageArgs:
            messageArgs['timeTaken'] = timeDiff
        message = execResult['message'].format(**messageArgs)
        return {"message": message}
    
    moduleName, bytesRead = execResult['moduleName'], execResult['bytesRead']
    return  {
                "bytesRead": bytesRead,
                "timeTaken": timeDiff,
                "moduleName": moduleName
            }

def read_folder_os(args):
    bytesRead = 0
    fileList = os.listdir(args.folder_path)
    fileList = fileList[0:args.max_file_count]
    moduleInfo = "[os.listdir]"
    for filePathRelative in fileList:
        filePathFull = os.path.join(args.folder_path, filePathRelative)
        if os.path.isfile(filePathFull):
            print(f'{moduleInfo:<16} Opening and reading {filePathFull}')
            file = open(filePathFull, 'rb')
            bytesRead += len(file.read())
            file.close()

    return  {
                "moduleName": "os.listdir",
                "bytesRead": bytesRead
            }

def read_folder_pathlib(args):
    bytesRead = 0
    folderPath = Path(args.folder_path)
    fileList = [x for x in folderPath.iterdir() if x.is_file()]
    fileList = fileList[0:args.max_file_count]
    moduleInfo = "[pathlib]"
    for fileEntry in fileList:
        print(f'{moduleInfo:<16} Opening and reading {str(fileEntry)}')
        f = open(fileEntry, 'rb')
        bytesRead += len(f.read())
        f.close()

    return  {
                "moduleName": "pathlib",
                "bytesRead": bytesRead
            }

def read_folders_glob(args):
    bytesRead = 0
    fileList = [x for x in glob.glob("{}/**/*.*".format(args.folder_path), recursive=True)]
    fileList = fileList[0:args.max_file_count]
    moduleInfo = "[glob]"
    for filePath in fileList:
        if os.path.isfile(filePath):
            print(f'{moduleInfo:<16} Opening and reading {str(filePath)}')
            f = open(filePath, 'rb')
            bytesRead += len(f.read())
            f.close()

    return  {
                "moduleName": "glob",
                "bytesRead": bytesRead
            }

def read_folders_in_tarfile(args):
    moduleInfo = "[tarfile]"
    print(f'{moduleInfo:<16} Fetching the compressed file at intercept path {args.compressed_tar_path}')
    tarObj = tarfile.open(args.compressed_tar_path, 'r:gz')
    tarMembersList = [x for x in tarObj.getmembers() if x.isfile()]
    tarMembersList = tarMembersList[0:args.max_file_count]

    # Create a tmp directory to extract tarfile's contents
    randomDirectory = ''.join(secrets.choice(string.ascii_uppercase + string.digits)
                                                  for i in range(7))

    destDirectory = os.path.join("/tmp/{}".format(randomDirectory))
    os.mkdir(destDirectory)

    # Extract using the built-in 'data' filter, which rejects tar-slip path
    # traversal (absolute paths, '..', links escaping the destination) from a
    # malicious or corrupt archive.
    tarObj.extractall(destDirectory, members=tarMembersList, filter='data')
    for member in tarMembersList:
        extractedTargetPath = os.path.join(destDirectory, member.name)
        print(f'{moduleInfo:<16} Extracted file {member.name} into {extractedTargetPath}')
        assert os.stat(extractedTargetPath).st_size == member.size

    # Cleanup temp directory
    for root, dirs, files in os.walk(destDirectory, topdown=False):
        for filePath in files:
            os.remove(os.path.join(root, filePath))
        for dirPath in dirs:
            os.rmdir(os.path.join(root, dirPath))
    os.rmdir(destDirectory)
    tarObj.close()

    return  {
                "message": "{moduleInfo:<16} Extracted {nFiles} files in {timeTaken} seconds using tarfile module",
                "messageArgs": {"moduleInfo": moduleInfo, "nFiles": len(tarMembersList), "timeTaken": ""}
            }

def read_folders_in_zipfile(args):
    bytesRead = 0
    moduleInfo = "[zipfile]"
    print(f'{moduleInfo:<16} Fetching the compressed file at intercept path {args.compressed_zip_path}')
    zipFileObj = zipfile.ZipFile(args.compressed_zip_path, 'r')
    fileList = [x.filename for x in zipFileObj.infolist() if not x.is_dir()]
    fileList = fileList[0:args.max_file_count]

    for filePathRelative in fileList:
        filePathFull = os.path.join(args.compressed_zip_path, filePathRelative)
        print(f'{moduleInfo:<16} Opening and reading {filePathFull}')
        with zipFileObj.open(filePathRelative) as zipFile:
            bytesRead += len(zipFile.read())
    zipFileObj.close()

    return  {
                "moduleName": "zipfile",
                "bytesRead": bytesRead
            }

def read_folders_and_copy_stats_shutil(args):
    """
    [Disabled] Currently listxattr and other extended stat operations
               are not supported by the Storage Interceptor.
    """
    randomDirectory = ''.join(secrets.choice(string.ascii_uppercase + string.digits)
                                                  for i in range(7))

    destDirectory = os.path.join("/tmp/{}".format(randomDirectory))
    os.mkdir(destDirectory)

    fileList = [os.path.join(args.folder_path, x) for x in os.listdir(args.folder_path)]
    fileList = fileList[0:args.max_file_count]

    fileIdx = 0
    moduleInfo ="[shutil]"
    for filePath in fileList:
        if os.path.isfile(filePath):
            fileIdx += 1
            tmpFilePath = os.path.join(destDirectory, "tmpFile_{}".format(fileIdx))

            # Copy file content and stats from folder_path directory into the temporary destination directory
            print(f'{moduleInfo:<16} Copying file from {filePath} to {tmpFilePath}')
            shutil.copy2(filePath, tmpFilePath)

            # Assert stat and content equality
            assert os.stat(filePath).st_size  == os.stat(tmpFilePath).st_size
            assert os.stat(filePath).st_mtime == os.stat(tmpFilePath).st_mtime
            assert os.stat(filePath).st_ctime == os.stat(tmpFilePath).st_ctime
            assert os.stat(filePath).st_atime == os.stat(tmpFilePath).st_atime
            with open(filePath, 'rb') as srcFile:
                with open(tmpFilePath, 'rb') as destFile:
                    assert srcFile.read() == destFile.read()

    shutil.rmtree(destDirectory)
    return  {
                "message": "{moduleInfo:<16} Copied {nFiles} files\' content & stats in {timeTaken} seconds using shutil module",
                "messageArgs": {"nFiles": fileIdx, "timeTaken": "", "moduleInfo": "[shutil]"}
            }

def read_folders_os_walk(args):
    bytesRead = 0
    fileList = []
    for root, dirs, files in os.walk(args.folder_path, topdown=True):
        for filePath in files:
            fullFilePath = os.path.join(root, filePath)
            if os.path.isfile(fullFilePath):
                fileList.append(fullFilePath)
    fileList = fileList[0:args.max_file_count]
    moduleInfo = "[os.walk]"
    for filePath in fileList:
        print(f'{moduleInfo:<16} Opening and reading {filePath}')
        file = open(filePath, 'rb')
        bytesRead += len(file.read())
        file.close()

    return  {
                "moduleName": "os.walk",
                "bytesRead": bytesRead
            }

def read_folder_hdf5(args):
    nFiles = 0
    fileList = os.listdir(args.folder_path)
    moduleInfo="[hdf5]"
    for filePathRelative in fileList:
        if filePathRelative.lower().endswith('.hdf5') and nFiles < args.max_file_count:
            filePathFull = os.path.join(args.folder_path, filePathRelative)
            print(f'{moduleInfo:<16} Opening and reading {filePathFull}')
            hf = h5py.File(filePathFull, 'r')
            list(hf.keys())
            nFiles += 1
            hf.close()

    return  {
                "message": "{moduleInfo:<16} Read {nFiles} files in {timeTaken} seconds using the hdf5 module",
                "messageArgs": {"nFiles": nFiles, "timeTaken": "", "moduleInfo": "[hdf5]"}
            }

def main():
    parser = argparse.ArgumentParser(description='read folder test')
    parser.add_argument('--folder_path', type=str, default='', help='The folder whose files need to be read')
    parser.add_argument('--compressed_tar_path', type=str, default='', help='The folder, as a compressed tar file to be read')
    parser.add_argument('--compressed_zip_path', type=str, default='', help='The folder, as a compressed zip file to be read')
    parser.add_argument('--max_file_count', type=int, default=5, help='Max number of files to read')
    args = parser.parse_args()
    
    io_test_executor = ProcessPoolExecutor(max_workers=10)
    asyncTasks = []
    asyncTasks.append(io_test_executor.submit(timed_exec, read_folder_os, args))
    asyncTasks.append(io_test_executor.submit(timed_exec, read_folder_pathlib, args))
    asyncTasks.append(io_test_executor.submit(timed_exec, read_folders_glob, args))
    asyncTasks.append(io_test_executor.submit(timed_exec, read_folders_in_tarfile, args))
    asyncTasks.append(io_test_executor.submit(timed_exec, read_folders_in_zipfile, args))
    asyncTasks.append(io_test_executor.submit(timed_exec, read_folders_os_walk, args))
    asyncTasks.append(io_test_executor.submit(timed_exec, read_folder_hdf5, args))
    # [Disabled] - Currently listxattr and other extended stat operations are not supported by the Storage Interceptor
    # asyncTasks.append(io_test_executor.submit(timed_exec, read_folders_and_copy_stats_shutil, args))

    io_test_executor.shutdown(wait=True)

    print("===================================================================================================")
    print("Python I/O test results summary")
    print("===================================================================================================")
    for task in asyncTasks:
        taskResult = task.result()

        if 'message' in taskResult:
            print(taskResult['message'])
        
        else:
            moduleName, bytesRead, timeTaken = taskResult['moduleName'], taskResult['bytesRead'], taskResult['timeTaken']
            moduleInfo = '[' + moduleName + ']'
            print(f'{moduleInfo:<16} Read {bytesRead} bytes in {timeTaken} seconds using {moduleName} module')

    print("===================================================================================================")

if __name__=='__main__':
    main()
