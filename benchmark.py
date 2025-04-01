# Author: Somit Gond

'''
Program to benchmark threshold based queuing policy against droptail
Four benchmarks:
1. effective delay: propagation delay + qeueu delay (queue * link capacity)
2. flow completion time: set a fixed file size to send, them get flow completion time from traces
3. throughput: use flow-monitor to collect data => calculate throughput using that 
4. synchrony: after simulation, calculate syncrony from data
'''

import numpy as np
import os
import xml.etree.ElementTree as ET # to read xml file

def calculate_throughput(flow_data):
    throughput_data = []
    for flow in flow_data:
        tx_bytes = int(flow['txBytes'])  # Transmitted bytes
        time_first_tx_ns = float(flow['timeFirstTxPacket'].replace('+', '').replace('ns', ''))  # First transmission time (ns)
        time_last_tx_ns = float(flow['timeLastTxPacket'].replace('+', '').replace('ns', ''))  # Last transmission time (ns)
        # Calculate total time in seconds
        total_time_sec = (time_last_tx_ns - time_first_tx_ns) / 1e9

        # Calculate throughput in Mbps
        throughput_bps = tx_bytes / total_time_sec
        throughput_mbps = (throughput_bps * 8) / 1e6

        throughput_data.append({'flowId': flow['flowId'], 'throughput_mbps': throughput_mbps})

    return throughput_data

if __name__ == "__main__":
    folder_path = '/home/jack/github/mtp/pythonWork/tcp-dumbbell-regular-tcplinuxreno/'
    # effective delay
    # propagation delay is rtt and queueing delay is queue at router 1 * link bandwith
    # need to store RTT of each flow in a file. for each time point multiply queue 
    
    # flow completion time
    # run it for 100 mb and check last enqueued cwnd time
    
    # throughput calculation
    tree = ET.parse('dumbbell-flowmonitor.xml')
    root = tree.getroot()
    flowstats = root[0]
    attri = []
    for flows in flowstats:
        attri.append(flows.attrib)
    ## throughput per flow calculated
    throughputPerFlow = calculate_throughput(attri)
            
    # synchrony calculation
    os.chdir(folder_path)
    data = []
    for f in os.listdir():
        if(f.endswith('.cwnd')):
            d = np.genfromtxt(f, delimiter=8).reshape(-1, 2)
            data.append(d[:], 1)
    ## convert array to numpy
    data = np.array(data)

    data_loss = np.zeros(data.shape)

    ## convert it into loss events
    for i in range(len(data)):
        for j in range(1, len(data[i])):
            if(data[i][j-1] > data[i][j]):
                data_loss[i][j] = 1

    ## global sync
    sync_rate = []
    for k in range(len(data_loss[0])):
        nij = 0
        low = max(0, k-window_size)
        high = min(k+window_size+1, len(data_loss[0]))
        for i in range(len(data_loss)):
            for j in range(low, high):
                if(data_loss[i][j] == 1):
                    nij+=1
                    break
        sync_rate.append(nij/len(data_loss))
    ### global sync rates is synchrony
            

    
