# Initial Author: Somit Gond
# Date: 02/04/2025

"""
This file is used to simulate ns3 script and find ROC curve
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
    subprocess.run(f"cp -r {src} {src_new}", shell=True, stdout=subprocess.DEVNULL)

    # gzip it
    subprocess.run(f"tar -zcvf {src_gzip} {src_new}", shell=True, stdout=subprocess.DEVNULL)

    # remove source new directory
    subprocess.run(f"rm -r {src_new}", shell=True, stdout=subprocess.DEVNULL)

    # move it
    subprocess.run(f"mv {src_gzip} {dst_gzip}", shell=True, stdout=subprocess.DEVNULL)

    print("Successfully saved the results")


if __name__ == "__main__":
    random.seed(2341)

    src_path = "result-clientServerRouter"
    folder_path = src_path + "/"
    dst_path = "results-roc"
    # total data to transfer
    tot_bytes = 0 # 0 means infinte data
    # tot_bytes = 100 * 1000000

    file_to_run = "clientServerRouter.cc"
    aqm_enabled = 1
    # required queue disc
    qd = "ns3::FifoQueueDisc"
    rtt = 200

    num = 1
    rs = 42653
    for n in range(1, 61): # 1 to 60 hosts
        print(f"Simulation {num}: rng = {rs}, file = {file_to_run}, nNodes = {n} rtt = {rtt}ms, qd = {qd}, tot_bytes = {tot_bytes}, AQM_ENABLED={aqm_enabled}")
        print(f"Iteration: {num}")
        temp_rtt = f"{rtt}ms"
        cmd_to_run = f'NS_GLOBAL_VALUE="RngRun={rs}" ./ns3 run scratch/{file_to_run} -- --nNodes={n} --RTT="{temp_rtt}" --queue_disc={qd} --bytes_to_send={tot_bytes} --AQM_ENABLED={aqm_enabled}'

        # run the command
        start = time.time()
        subprocess.run(cmd_to_run, shell=True)
        end = time.time()
        print(f"Execution time: {end-start:.4f} seconds")
        time.sleep(2)

        # save final simulation data
        save_folder(src_path, dst_path)
        num += 1
