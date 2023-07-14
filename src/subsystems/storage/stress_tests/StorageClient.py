import argparse
import multiprocessing as mp
import time
import os

def parse_args():
    parser = argparse.ArgumentParser(description='Storage client for stress tests')
    parser.add_argument('--storage_path', type=str, default=None)
    parser.add_argument('--name', type=str, default=None)
    parser.add_argument('--num_workers', type=int, default=1)

    args = parser.parse_args()
    return args

def worker_function(work_unit):
    assert type(work_unit) is str, "Work unit must be a path to a file"

    with open(work_unit, 'rb') as f:
        bytes_read = len(f.read())
    
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
        work_queue.extend(list(map(lambda f: os.path.join(root, f), files)))

    oneGB = 1024 * 1024 * 1024
    # Now we have the work queue, we can start the workers
    with mp.Pool(args.num_workers) as pool:
        start_time = time.time()
        result = pool.map(worker_function, work_queue)
        end_time = time.time()
        elapsed_time = end_time - start_time
        total_bytes_read = 0

        for r in result.get():
            total_bytes_read += r

        print('##########################################################')
        print("Client I/O test results summary")
        print('Bytes read: {} in {} seconds, throughput: {} GBps Start time: {} End time: {}'.format(total_bytes_read, elapsed_time, (total_bytes_read / oneGB) / elapsed_time), start_time, end_time)
        print('##########################################################')

if __name__ == '__main__':
    main()