import time
import math
import os
import argparse

import glob
from functools import partial
from infinibatch import datasets, iterators

import torch
from torch.utils.data import Dataset, DataLoader, IterableDataset
import torch.multiprocessing as mp
import torch.distributed as dist

class DataLoadTracker(): 
    def __init__(self):
        self.nBytes = 0

    def read_chunk(self, filename):
        self.nBytes += os.path.getsize(filename)
        return open(filename, "rb").read().decode(encoding='utf-8').splitlines()

def setup_dist(rank, world_size):
    # initialize the process group
    dist.init_process_group(                                   
    	backend='nccl',                                         
   		init_method='env://',                                   
    	world_size=world_size,
        group_name = 'vision',
    	rank=rank)

def dist_fit(gpu, args, extraArg):
    rank = args.nr * args.gpus + gpu
    world_size = args.world_size
    setup_dist(rank, world_size)
    glob_path = args.mount_path + '/XLME/500k-vocab/cc-100_shard_v1_bpe/en/en.*.txt'

    dlt = DataLoadTracker()

    print("Rank-{} Creating the dataset iterator.".format(rank))
    time_before = time.time()

    dataset_it = datasets.chunked_dataset_iterator(
        chunk_refs = glob.glob(glob_path),
        read_chunk_fn = dlt.read_chunk,
        buffer_size = 10000000,
        seed = 1337,
        num_instances=world_size,
        instance_rank=rank)
    time_after_dataset_it = time.time()
    time_for_dataset_it = time_after_dataset_it - time_before
    print("Rank-{} Created the dataset iterator. Time => {} nBytes => {}".format(
        rank,
        time_for_dataset_it,
        dlt.nBytes))

    it = iter(dataset_it)
    for i in range(30000):
        next(it)

    time_data_load = time.time() - time_after_dataset_it
    print("Rank-{} Time taken for data load => {} nBytes => {} throughtput Bps => {}".format(
        rank,
        time_data_load,
        dlt.nBytes,
        dlt.nBytes / time_data_load))

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('-m', '--mount-path', default='', type=str,
                        help="mount path of storage container")
    parser.add_argument('-n', '--nodes', default=1, type=int, metavar='N',
                        help='number of nodes in distributed job')
    parser.add_argument('-g', '--gpus', default=1, type=int,
                        help='number of gpus per node')
    parser.add_argument('-nr', '--nr', default=0, type=int,
                        help='ranking within the nodes')
    args = parser.parse_args()

    args.world_size = args.gpus * args.nodes
    os.environ['MASTER_ADDR'] = 'localhost'
    os.environ['MASTER_PORT'] = '12357'

    time_before = time.time()
    mp.spawn(dist_fit, nprocs=args.gpus, args=(args, ''))
    time_after = time.time()

    total_time = time_after - time_before
    print("Total time taken => {}".format(total_time))

if __name__ == '__main__':
    main()