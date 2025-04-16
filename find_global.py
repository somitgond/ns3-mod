# author: Somit Gond
# date: 02/04/2025

"""
This file is used to simulate ns3 script and find global sync threshold
Flow:
run simulation T times
each time calculate avg throughput and average global synchronization
"""

import csv
import os
import random
import subprocess
import time
import xml.etree.ElementTree as ET
from pathlib import Path
from re import sub

import numpy as np


def avg_throughput_calc(
    folder_path, individual_throughput_filename, fct=False, debug=0
):
    # os.chdir(folder_path)
    filename = folder_path + "dumbbell-flowmonitor.xml"
    # throughput calculation
    tree = ET.parse(filename)
    root = tree.getroot()
    flowstats = root[0]
    attri = []
    for flows in flowstats:
        attri.append(flows.attrib)

    flows_ip = {}
    for ips in root[1]:
        temp = ips.attrib
        flows_ip[temp["flowId"]] = [
            temp["sourceAddress"],
            temp["destinationAddress"],
        ]

    ## throughput per flow calculated
    throughputs_filename = folder_path + individual_throughput_filename

    data_to_write = [
        "Flow number",
        "Source IP",
        "Destination IP",
        "Total Time (s)",
        "Data (Mb)",
        "Throughput (Mbps)",
    ]

    with open(throughputs_filename, "w", newline="") as csvfile:
        writer = csv.writer(csvfile)
        writer.writerow(data_to_write)

    return calculate_throughput(attri, flows_ip, throughputs_filename, fct, debug)


def calculate_throughput(flow_data, flows_ip, throughputs_filename, fct=False, debug=0):
    throughput_data = []
    fct_data = []
    trasmitted_data = []

    for flow in flow_data:
        tx_bytes = int(flow["txBytes"])  # Transmitted bytes
        time_first_tx_ns = float(
            flow["timeFirstTxPacket"].replace("+", "").replace("ns", "")
        )  # First transmission time (ns)
        time_last_tx_ns = float(
            flow["timeLastTxPacket"].replace("+", "").replace("ns", "")
        )  # Last transmission time (ns)

        time_last_rx_ns = float(
            flow["timeLastRxPacket"].replace("+", "").replace("ns", "")
        )  # Last Recieved time (ns)

        # Calculate total time in seconds
        total_time_sec = (time_last_tx_ns - time_first_tx_ns) / 1e9

        total_time_sec_fct = (time_last_rx_ns - time_first_tx_ns) / 1e9

        # Calculate throughput in Mbps
        throughput_bps = tx_bytes / total_time_sec
        throughput_mbps = (throughput_bps * 8) / (1024 * 1024)

        data_sent = (tx_bytes * 8) / (1024 * 1024)

        flow_id = flow["flowId"]

        # write throughput and flow completion time to file
        if fct:
            with open(throughputs_filename, "a", newline="") as csvfile:
                writer = csv.writer(csvfile)
                writer.writerow(
                    [
                        flow_id,
                        flows_ip[flow_id][0],
                        flows_ip[flow_id][1],
                        total_time_sec_fct,
                        data_sent,
                        throughput_mbps,
                    ]
                )
        else:
            with open(throughputs_filename, "a", newline="") as csvfile:
                writer = csv.writer(csvfile)
                writer.writerow(
                    [
                        flow_id,
                        flows_ip[flow_id][0],
                        flows_ip[flow_id][1],
                        total_time_sec,
                        data_sent,
                        throughput_mbps,
                    ]
                )

        if debug == 1:
            print(f"Flow: {flow_id} throughput: {throughput_mbps}mbps")

        throughput_data.append(throughput_mbps)
        fct_data.append(total_time_sec_fct)
        trasmitted_data.append(data_sent)

    throughput_data = np.array(throughput_data)
    fct_data = np.array(fct_data)
    trasmitted_data = np.array(trasmitted_data)
    return np.mean(throughput_data), np.mean(fct_data), np.mean(trasmitted_data)


def global_sync_value(folder_path, debug=0):
    # define parameters
    window_size = 25  # 5 second window

    # synchrony calculation
    data = []
    for f in os.listdir(folder_path):
        if f.endswith(".cwnd"):
            if debug == 1:
                print(f"Reading file: {folder_path+f}")
            d = np.genfromtxt(folder_path + f, delimiter=" ").reshape(-1, 2)
            data.append(d[:, 1])

    ## convert array to numpy
    data = np.array(data)

    data_loss = np.zeros(data.shape)

    ## convert it into loss events
    for i in range(len(data)):
        for j in range(1, len(data[i])):
            if data[i][j - 1] > data[i][j]:
                data_loss[i][j] = 1

    ## global sync
    sync_rate = []
    for k in range(len(data_loss[0])):
        nij = 0
        low = max(0, k - window_size)
        high = min(k + window_size + 1, len(data_loss[0]))
        for i in range(len(data_loss)):
            for j in range(low, high):
                if data_loss[i][j] == 1:
                    nij += 1
                    break
        sync_rate.append(nij / len(data_loss))
    if debug == 1:
        print(sync_rate)
    sync_rate = np.array(sync_rate)
    return np.mean(sync_rate)


# finding effective delay
def effective_delay(folder_path, debug=0):
    filename_rtt = folder_path + "RTTs.txt"
    filename_qsize = folder_path + "tc-qsizeTrace-dumbbell.txt"

    # reading data
    rtt_data = np.genfromtxt(filename_rtt, delimiter=" ").reshape(-1, 2)
    queue_data = np.genfromtxt(filename_qsize, delimiter=" ").reshape(-1, 2)

    # if dubugging is on print data values
    if debug == 1:
        print(rtt_data)
        print(queue_data)

    # for 1 is added in queue buffer
    queue_data[:, 1] += 1

    # average effective delay
    avg_rtt = np.mean(rtt_data[:, 1])
    avg_rtt += (np.mean(queue_data[:, 1]) * 8) / 10**5
    avg_rtt += 2

    # find jitter
    combined = 2 + np.mean(rtt_data[:, 1]) + (queue_data[:, 1] * 8) / 10**5
    jitter_avg_rtt = np.var(combined)

    # queueing delay
    queueing_delay = (np.mean(queue_data[:, 1]) * 8) / 10**5

    return avg_rtt, jitter_avg_rtt, queueing_delay


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
    data_filename = "results.csv"
    individual_throughput_filename = "throughputs.csv"
    fields = [
        "Simulation_number",
        "Random Seed",
        "RTT",
        "Global Sync Value",
        "Average Throughput(Mbps)",
        "Flow Completion Time(s)",
        "Averate Data Sent(Mb)",
        "Effective Delay(ms)",
        "Jitter in RTT(ms)",
        "Qeueing Delay(ms)",
    ]
    with open(data_filename, "w", newline="") as csvfile:
        writer = csv.writer(csvfile)
        writer.writerow(fields)

    number_of_simulations = 10
    random_seeds = []

    for _ in range(number_of_simulations):
        random_seeds.append(random.randint(1, 99999))

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

        # write data in output file
        eff_rtt, jitter, queue_delay = effective_delay(folder_path)
        throughput_avg, fct_avg, data_avg = avg_throughput_calc(
            folder_path, individual_throughput_filename
        )
        data_to_write = [
            num,
            rs,
            200,
            global_sync_value(folder_path),
            throughput_avg,
            fct_avg,
            data_avg,
            eff_rtt,
            jitter,
            queue_delay,
        ]
        save_folder(src_path, dst_path)
        with open(data_filename, "a", newline="") as csvfile:
            writer = csv.writer(csvfile)
            writer.writerow(data_to_write)
        num += 1

    # for one random seed and n rtts
    random_seed = random.choice(random_seeds)
    for rtt in RTTs:
        print(f"Iteration: {num}")
        temp_rtt = f"{rtt}ms"
        cmd_to_run = f'NS_GLOBAL_VALUE="RngRun={random_seed}" ./ns3 run scratch/clientServerRouter.cc -- --RTT="{temp_rtt}"'

        # run the command
        subprocess.run(cmd_to_run, shell=True)
        time.sleep(2)

        # check that process exited successfully
        # assert subprocess.CompletedProcess.returncode == 0

        # write data in output file
        eff_rtt, jitter, queue_delay = effective_delay(folder_path)

        throughput_avg, fct_avg, data_avg = avg_throughput_calc(
            folder_path, individual_throughput_filename
        )
        data_to_write = [
            num,
            random_seed,
            rtt + 2,
            global_sync_value(folder_path),
            throughput_avg,
            fct_avg,
            data_avg,
            eff_rtt,
            jitter,
            queue_delay,
        ]
        save_folder(src_path, dst_path)
        with open(data_filename, "a", newline="") as csvfile:
            writer = csv.writer(csvfile)
            writer.writerow(data_to_write)
        num += 1
