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
import xml.etree.ElementTree as ET

import numpy as np


def avg_throughput_calc(folder_path, debug=0):
    # os.chdir(folder_path)
    filename = folder_path + "dumbbell-flowmonitor.xml"
    # throughput calculation
    tree = ET.parse(filename)
    root = tree.getroot()
    flowstats = root[0]
    attri = []
    for flows in flowstats:
        attri.append(flows.attrib)

    ## throughput per flow calculated
    return calculate_throughput(attri, debug)


def calculate_throughput(flow_data, debug=0):
    throughput_data = 0
    number_of_flows = 0
    for flow in flow_data:
        tx_bytes = int(flow["txBytes"])  # Transmitted bytes
        time_first_tx_ns = float(
            flow["timeFirstTxPacket"].replace("+", "").replace("ns", "")
        )  # First transmission time (ns)
        time_last_tx_ns = float(
            flow["timeLastTxPacket"].replace("+", "").replace("ns", "")
        )  # Last transmission time (ns)

        # Calculate total time in seconds
        total_time_sec = (time_last_tx_ns - time_first_tx_ns) / 1e9

        # Calculate throughput in Mbps
        throughput_bps = tx_bytes / total_time_sec
        throughput_mbps = (throughput_bps * 8) / 1e6
        if debug == 1:
            print(f"Flow: {number_of_flows} throughput: {throughput_mbps}mbps")
        throughput_data += throughput_mbps
        number_of_flows += 1

    return throughput_data / number_of_flows


def global_sync_value(folder_path, debug=0):
    # define parameters
    window_size = 25  # 5 second window

    # synchrony calculation
    # os.chdir(folder_path)
    data = []
    for f in os.listdir(folder_path):
        if f.endswith(".cwnd"):
            if debug == 1:
                print(f"Reading file: {folder_path+f}")
            d = np.genfromtxt(folder_path + f, delimiter=8).reshape(-1, 2)
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
    avg_sync_rate = 0
    sync_value = 0
    for sr in sync_rate:
        if sr != 0:
            sync_value += 1
            avg_sync_rate += sr

    return avg_sync_rate / sync_value


# finding effective delay
def effective_delay(folder_path, debug=0):
    filename_rtt = folder_path + "RTTs.txt"
    filename_qsize = folder_path + "tc-qsizeTrace-dumbbell.txt"
    rtt_data = np.array(np.genfromtxt(filename_rtt, delimiter=8))
    queue_data = np.genfromtxt(filename_qsize, delimiter=8).reshape(-1, 2)
    if debug == 1:
        print(rtt_data)
        print(queue_data)
    avg_rtt = np.average(rtt_data)
    avg_rtt += (np.average(queue_data[:, 1]) * 8) / 10**5
    return avg_rtt


# flow completion time

if __name__ == "__main__":
    random.seed(2341)

    folder_path = "result-clientServerRouter/"
    data_filename = "results.csv"
    fields = [
        "Simulation_number",
        "Random Seed",
        "RTT",
        "Global Sync Value",
        "Average Throughput",
        "Effective Delay",
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
        RTTs.append(f"{(5*i) + 198}ms")

    num = 0
    for rtt in RTTs:
        for rs in random_seeds:
            cmd_to_run = f'NS_GLOBAL_VALUE="RngRun={rs}" ./ns3 run scratch/clientServerRouter-ri.cc -- --RTT="{rtt}"'

            # run the command
            subprocess.run(cmd_to_run, shell=True)

            # check that process exited successfully
            assert subprocess.CompletedProcess.returncode == 0

            # write data in output file
            data_to_write = [
                num,
                rs,
                rtt,
                global_sync_value(folder_path),
                avg_throughput_calc(folder_path),
                effective_delay(folder_path),
            ]
            with open(data_filename, "w", newline="") as csvfile:
                writer = csv.writer(csvfile)
                writer.writerow(data_to_write)
            num += 1
