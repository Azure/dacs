# Copyright (c) Microsoft Corporation.
# Licensed under the Apache License, Version 2.0.

import argparse
import subprocess
import os

def parse_args():
    parser = argparse.ArgumentParser(description='Launcher process for storage client')
    parser.add_argument('--storage_path', type=str, default=None, help='Path to mount of the storage directory')
    parser.add_argument('--name', type=str, default='books_wiki_en_corpus', help='Name of the dataset to read from')
    parser.add_argument('--num_workers', type=str, default='1', help='Parallelism of client')
    parser.add_argument('--client_type', type=str, default='python', help='Select type of client to launch')

    args = parser.parse_args()
    return args

def main():
    args = parse_args()

    assert args.storage_path is not None, 'Storage path must be provided'
    # Set the appropriate environment variables for our storage client
    process_env = os.environ.copy()
    process_env['LD_PRELOAD'] = '/app/lib/libStorageIntercept.so'
    process_env['SI_storagePath'] = args.storage_path

    # Now we can launch the storage client
    if args.client_type == 'python':
        cmd = ['python3', '/app/StorageClient.py', '--storage_path', args.storage_path, \
            '--name', args.name, '--num_workers', args.num_workers]

    elif args.client_type == 'cpp':
        cmd = ['/app/CacheStorageClient', args.storage_path, args.name, args.num_workers]

    elif args.client_type == 'delete':
        # No fallback on env variable reads, since we want to fail if they're not set
        serverHostNames = os.environ['SI_cacheServerList']
        serverPort = os.environ['SI_cacheServerPort']
        groupId = os.environ['SI_cacheKeyGroupId']
        cmd = ['/app/DeleteClient', serverHostNames, serverPort, groupId]

    else:
        raise Exception('Invalid client type: {}'.format(args.client_type))

    print('Launching storage client with command: {}'.format(' '.join(cmd)))
    subprocess.run(cmd, env=process_env)

if __name__ == '__main__':
    main()
