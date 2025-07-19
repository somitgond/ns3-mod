# author: Somit Gond
# date: 02/04/2025

"""
This file is used to simulate ns3 script and find global sync threshold
Flow:
run simulation T times
each time calculate avg throughput and average global synchronization
"""

import os
import random
import subprocess
import time
from pathlib import Path
from re import sub

import numpy as np

# saving trace results
def save_folder(src, dst):
    i = 0
    if Path(dst).exists() == False:
        subprocess.run(f"mkdir {dst}", shell=True)
    while Path(f"{dst}/{src}-{i}.gzip").exists():
        i += 1

    src_new = f"{src}-{i}"
    src_gzip = f"{src_new}.gzip"
    dst_gzip = dst + "/" + src_gzip

    # rename the src directory
    subprocess.run(f"cp -r {src} {src_new}", shell=True)

    # gzip it
    subprocess.run(f"tar -zcvf {src_gzip} {src_new}", shell=True)

    # remove source new directory
    subprocess.run(f"rm -r {src_new}", shell=True)

    # move it
    subprocess.run(f"mv {src_gzip} {dst_gzip}", shell=True)


if __name__ == "__main__":
    random.seed(2341)

    src_path = "result-clientServerRouter"
    folder_path = src_path + "/"
    dst_path = "results"
    number_of_simulations = 10
    random_seeds = [
            69713,
            56629,
            86799,
            42653,
            82842,
            72958,
            23256,
            14590,
            98472,
            8288,
            42653,
            42653,
            42653,
            42653,
            42653,
            42653,
            42653,
            42653,
            42653,
            42653,
            ]

    RTTs = []
    for i in range(number_of_simulations):
        RTTs.append((5 * i) + 198)

    num = 0
    # for one RTT, n number of random seeds
    for rs in random_seeds:
        print(f"Iteration: {num}")
        cmd_to_run = f'NS_GLOBAL_VALUE="RngRun={rs}" ./ns3 run scratch/clientServerRouter.cc -- --RTT="198ms"'

        # run the command
        subprocess.run(cmd_to_run, shell=True)
        time.sleep(2)

        # check that process exited successfully
        # assert subprocess.CompletedProcess.returncode == 0
        save_folder(src_path, dst_path)
        num += 1

    # for one random seed and n rtts
    random_seed = 42653
    for rtt in RTTs:
        print(f"Iteration: {num}")
        temp_rtt = f"{rtt}ms"
        cmd_to_run = f'NS_GLOBAL_VALUE="RngRun={random_seed}" ./ns3 run scratch/clientServerRouter.cc -- --RTT="{temp_rtt}"'

        # run the command
        subprocess.run(cmd_to_run, shell=True)
        time.sleep(2)

        # check that process exited successfully
        # assert subprocess.CompletedProcess.returncode == 0

        save_folder(src_path, dst_path)
        num += 1
