# Copyright (c) Microsoft Corporation.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import argparse
import multiprocessing as mp
import time
import os
import hashlib

def parse_args():
    parser = argparse.ArgumentParser(description='Storage client for stress tests')
    parser.add_argument('--storage_path', type=str, default=None)
    parser.add_argument('--name', type=str, default=None)
    parser.add_argument('--num_workers', type=int, default=1)
    parser.add_argument('--rank_filter', type=int, default=None)
    parser.add_argument('--world_size', type=int, default=1)
    parser.add_argument('--read_pattern', type=str, choices=['whole', 'sequential', 'random'], default='whole')
    parser.add_argument('--read_size_mb', type=int, default=100)

    args = parser.parse_args()
    return args

def get_deterministic_hash(s: str):
    hashNum = int(hashlib.md5(s.encode()).hexdigest(), 16) # CodeQL [SM02167] stress test client not used in production 
    return hashNum

def worker_function(work_unit, args):
    assert type(work_unit) is str, "Work unit must be a path to a file"

    with open(work_unit, 'rb') as f:
        if args.read_pattern == 'whole':
            bytes_read = len(f.read())
        elif args.read_pattern == 'sequential':
            bytes_read = 0
            while True:
                readSize = args.read_size_mb * 1024 * 1024
                data = f.read(readSize)
                if not data:
                    break
                bytes_read += len(data)
                if len(data) < readSize:
                    break

    if args.read_pattern == 'random':
        bytes_read = 0
        readSize = args.read_size_mb * 1024 * 1024
        fileSize = os.path.getsize(work_unit)
        while bytes_read < fileSize:
            offset = hash(str(time.time())) % fileSize
            with open(work_unit, 'rb') as f:
                f.seek(offset)
                data = f.read(readSize)
                if data:
                    bytes_read += len(data)
 
    print('Read file: {} with read_pattern: {}. Bytes read: {}'.format(work_unit, args.read_pattern, bytes_read))
    return bytes_read

def main():
    args = parse_args()
    assert args.name is not None, 'Dataset name must be provided'
    assert args.storage_path is not None, 'Storage path must be provided'

    print("Starting storage client with {} workers, reading dataset: {}".format(args.num_workers, args.name))
    full_path = os.path.join(args.storage_path, args.name)
    # Recursively scan through the directory and get all the files
    work_queue = []
    for root, _, files in os.walk(full_path):
        filesToRead = files
        if args.rank_filter is not None:
            filesToRead = list(filter(lambda fileName: get_deterministic_hash(fileName) % args.world_size == args.rank_filter, files))
        work_queue.extend(list(map(lambda f: os.path.join(root, f), filesToRead)))

    oneMB = 1024 * 1024
    # Now we have the work queue, we can start the workers
    with mp.Pool(args.num_workers) as pool:
        start_time = time.time()
        result = pool.starmap(worker_function, [(work_unit, args) for work_unit in work_queue])
        end_time = time.time()
        elapsed_time = end_time - start_time
        total_bytes_read = 0

        for r in list(result):
            total_bytes_read += r

        print('##########################################################')
        print("Client I/O test results summary")
        print('Bytes read: {} in {} seconds, throughput: {} MB/s Start time: {} End time: {}'.format(total_bytes_read, elapsed_time, ((total_bytes_read / oneMB) / elapsed_time), start_time, end_time))
        print('##########################################################')

if __name__ == '__main__':
    main()