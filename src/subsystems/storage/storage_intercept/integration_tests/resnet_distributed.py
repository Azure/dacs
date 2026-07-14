# Copyright (c) Microsoft Corporation.
# Licensed under the Apache License, Version 2.0.

import multiprocessing
import torch
from torchvision import datasets, transforms, models
import torch.distributed as dist
from torch.utils.data.sampler import SubsetRandomSampler
from torch.utils.data.distributed import DistributedSampler
from torch.nn.parallel import DistributedDataParallel as DDP
from torch.utils.data import DataLoader, Subset
from torch.cuda.amp import GradScaler, autocast
import torch.multiprocessing as mp
import random
from datetime import datetime
import time
import math
import os
import argparse

learning_rate = 1e-3
batch_size = 256
epochs = 2

def setup_dist(rank, world_size):
    # initialize the process group
    dist.init_process_group(                                   
    	backend='nccl',                                         
   		init_method='env://',                                   
    	world_size=world_size,
        group_name = 'vision',
    	rank=rank                                               
    )

# Train loop for a single epoch
def train_loop(dataloader, device, rank, model, loss_fn, optimizer, scaler):
    dataset_size = len(dataloader.dataset)
    for batch, (X, y) in enumerate(dataloader):
        X = X.to(device)
        y = y.to(device)

        # Running in FP16 mode (half precision wherever possible)
        if scaler is not None:
            with autocast():
                pred = model(X)
                loss = loss_fn(pred, y)

            # Backpropagation
            optimizer.zero_grad()
            scaler.scale(loss).backward()
            scaler.step(optimizer)

            scaler.update()

        # Running in FP32 (default, single precision) mode.
        else:
            pred = model(X)
            loss = loss_fn(pred, y)

            # Backpropagation
            optimizer.zero_grad()
            loss.backward()
            optimizer.step()

        loss, current = loss.item(), batch * len(X) * 4
        print(f"[RANK: {rank:>3}] Training loss: {loss:>7f}  [{current:>5d}/{dataset_size:>5d}]")

# Validation loop for a single epoch
def validation_loop(dataloader, device, rank, model, loss_fn, scaler):
    dataset_size = len(dataloader.dataset)
    test_loss = 0.0
    correct = 0.0
    with torch.no_grad():
        for batch, (X, y) in enumerate(dataloader):
            X = X.to(device)
            y = y.to(device)
            pred = model(X)
            loss = loss_fn(pred, y)
            test_loss += loss.item()
            correct += (pred.argmax(1) == y).type(torch.float).sum().item()


    test_loss /= dataset_size
    correct /= dataset_size
    print(f"[RANK: {rank:>3}] Test Error: \n Accuracy: {(100*correct):>0.1f}%, Avg loss: {test_loss:>8f} \n")

def dist_fit(gpu, args, train_dataset, validation_dataset):
    rank = args.nr * args.gpus + gpu
    world_size = args.world_size
    num_dataloader_workers = args.workers
    #print("Running a DDP example on rank: {}. with {} dataloader workers".format(rank, num_dataloader_workers))
    setup_dist(rank, world_size)
    device = torch.device('cuda:{}'.format(gpu))

    loss_fn = torch.nn.CrossEntropyLoss()
    model = ImageModel()
    #print(summary(model, (3, 200 ,200)))
    optimizer = torch.optim.Adam(model.parameters(), lr=learning_rate)
    if args.fp16:
        scaler = GradScaler()
    else:
        scaler = None

    loss_fn = loss_fn.to(device)

    train_sampler = DistributedSampler(train_dataset, num_replicas = world_size, rank = rank)
    validation_sampler = DistributedSampler(validation_dataset, num_replicas = world_size, rank = rank)

    train_dataloader = DataLoader(dataset = train_dataset, sampler = train_sampler, batch_size = batch_size, shuffle = False, pin_memory = True, num_workers=num_dataloader_workers)
    validation_dataloader = DataLoader(dataset = validation_dataset, sampler = validation_sampler, batch_size = batch_size, shuffle = False, pin_memory = True, num_workers=num_dataloader_workers)

    model = model.to(device)
    ddp_model = DDP(model, device_ids = [gpu])
    
    for epoch in range(epochs):
        # Run the training loop
        print(f"[RANK: {rank:>3}] Starting the epoch: [{epoch:>3}]")
        train_loop(train_dataloader, device, rank, ddp_model, loss_fn, optimizer, scaler)
        # Run the validation loop - Not needed for storage testing.
        # validation_loop(validation_dataloader, device, rank, ddp_model, loss_fn, scaler)


class ImageModel(torch.nn.Module):
    def __init__(self):
        super(ImageModel, self).__init__()
        # self.vgg_out_dim = 1000
        # self.vgg_layer = torchvision.models.vgg16(pretrained=True)
        #self.dense_layer = torch.nn.Linear(self.vgg_out_dim, self.num_classes)
        
        self.model = models.resnet18(pretrained=True)

    def forward(self, X):
        logits = self.model(X)
        return logits

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('-n', '--nodes', default=1, type=int, metavar='N',
                        help='number of nodes in distributed job')
    parser.add_argument('-w', '--workers', default=16, type=int,
                        help='number of dataloader workers')
    parser.add_argument('-g', '--gpus', default=1, type=int,
                        help='number of gpus per node')
    parser.add_argument('-fp16', '--fp16', default=False, type=bool,
                        help='half precision mode switch')
    parser.add_argument('-nr', '--nr', default=0, type=int,
                        help='ranking within the nodes')
    parser.add_argument('--epochs', default=2, type=int, metavar='N',
                        help='number of total epochs to run')
    args = parser.parse_args()

    args.world_size = args.gpus * args.nodes                #
    os.environ['MASTER_ADDR'] = 'localhost'
    os.environ['MASTER_PORT'] = '12355'                      #

    
    normalize = transforms.Normalize(mean=[0.485, 0.456, 0.406],
                                     std=[0.229, 0.224, 0.225])
    image_transform = transforms.Compose([
            transforms.RandomResizedCrop(224),
            transforms.RandomHorizontalFlip(),
            transforms.ToTensor(),
            normalize,
    ])

    time_before = time.time()
    # Load training dataset from path
    train_dataset = datasets.ImageFolder(root = '/mnt/quiverfs/imagenet_data/train', transform = image_transform)
    #print(set(imagenet_data_train.classes))
    print("Size of the train dataset provided => {}".format(len(train_dataset)))
    print("Number of classes in train dataset => {}".format(len(train_dataset.class_to_idx.items())))
    
    # Load validation dataset from path
    validation_dataset = datasets.ImageFolder(root = '/mnt/quiverfs/imagenet_data/val', transform = image_transform)

    print("Size of the validation dataset provided => {}".format(len(validation_dataset)))
    print("Number of classes in validation dataset => {}".format(len(validation_dataset.class_to_idx.items())))

    time_taken = time.time() - time_before
    print("Total time taken for the operation => {}".format(time_taken))
    # Sleep for some time.
    time.sleep(5.0)

    time_before = time.time()
    mp.spawn(dist_fit, nprocs=args.gpus, args=(args, train_dataset, validation_dataset))
    time_after = time.time()
    print("Total time taken for the end to end job to complete => {}".format(time_after - time_before))

if __name__ == '__main__':
    main()