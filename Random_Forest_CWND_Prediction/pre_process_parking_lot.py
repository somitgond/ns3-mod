import os
from collections import defaultdict
import pandas as pd
from scapy.all import PcapReader, IP, TCP

# --- CONFIGURATION ---
DATA_DIR = ""  # Set this to the folder containing your single .pcap and multiple .cwnd files
COMBINED_PCAP = "" # Update with your exact single PCAP filename
OUTPUT_FILE = "" # Update with your desired output CSV filename
MSS = 1400      # Max Segment Size to convert CWND to absolute bytes
NUM_NODES = 90  # Updated for 90 nodes

def process_combined_pcap():
    pcap_path = os.path.join(DATA_DIR, COMBINED_PCAP)
    if not os.path.exists(pcap_path):
        print(f"❌ Error: Combined PCAP file '{pcap_path}' not found.")
        return

    # Dictionary to hold extracted packet data grouped by NodeID
    # Format: node_packets[node_id] = [ {packet_dict_1}, {packet_dict_2}, ... ]
    node_packets = defaultdict(list)
    packet_count = 0
    valid_packet_count = 0

    print(f"--- 1. Scanning single combined PCAP: {COMBINED_PCAP} ---")
    print("Reading PCAP and distributing packets to respective nodes (this may take time)...")
    
    # Read PCAP exactly once
    with PcapReader(pcap_path) as pcap_reader:
        for pkt in pcap_reader:
            packet_count += 1
            if packet_count % 50000 == 0:
                print(f"Scanned {packet_count} packets... (Kept {valid_packet_count} valid TCP packets)", end='\r')

            if not pkt.haslayer(IP) or not pkt.haslayer(TCP):
                continue
                
            ip_layer = pkt[IP]
            tcp_layer = pkt[TCP]
            
            src = ip_layer.src
            dst = ip_layer.dst

            if not (src.startswith("10.1.") and dst.startswith("10.1.")):
                continue
                
            src_parts = src.split('.')
            dst_parts = dst.split('.')
            
            try:
                src_x = int(src_parts[2])
                dst_x = int(dst_parts[2])
            except ValueError:
                continue

            direction = -1
            node_id = -1
            
            #  90-Node Topology Logic (Extracting 'x')
            # Data (Send): Src is x (0 to 89), Dst is x + 90
            if src_x < NUM_NODES and dst_x == src_x + NUM_NODES:
                direction = 0
                node_id = src_x + 3
            # ACK (Recv): Src is x + 90, Dst is x
            elif src_x >= NUM_NODES and src_x == dst_x + NUM_NODES:
                direction = 1
                node_id = dst_x + 3
            else:
                continue # Skip unrelated cross-traffic
                
            payload_len = len(tcp_layer.payload)
            
            # 3. Skip empty sender ACKs unless SYN/FIN
            if direction == 0 and payload_len == 0 and not (tcp_layer.flags.S or tcp_layer.flags.F):
                continue

            valid_packet_count += 1
            node_packets[node_id].append({
                "Time": float(pkt.time),
                "NodeID": node_id,
                "Direction": direction,
                "Seq_Number": int(tcp_layer.seq),
                "Ack_Number": int(tcp_layer.ack),
                "Packet_Size": len(pkt),
                "Payload_Size": payload_len
            })

    print(f"\n\nFinished scanning. Processed {packet_count} total packets, kept {valid_packet_count} valid packets.")

    # --- Process Each Node with its CWND file ---
    all_processed_data = []
    sorted_nodes = sorted(node_packets.keys())
    
    print(f"\n--- 2. Processing data and merging CWND for {len(sorted_nodes)} unique nodes ---")
    
    for node_id in sorted_nodes:
        packets = node_packets[node_id]
        
        df_node = pd.DataFrame(packets).sort_values(by=['Time'])
        
        df_node['Seq_Diff'] = df_node.groupby('Direction')['Seq_Number'].transform(lambda val: val.diff().fillna(0)).astype(int)
        df_node['Ack_Diff'] = df_node.groupby('Direction')['Ack_Number'].transform(lambda val: val.diff().fillna(0)).astype(int)

        cwnd_file = os.path.join(DATA_DIR, f"node-{node_id}.cwnd")
        if not os.path.exists(cwnd_file):
            print(f"  [!] Warning: CWND file {cwnd_file} missing. Skipping CWND merge for Node {node_id}.")
            continue
            
        df_cwnd = pd.read_csv(cwnd_file, sep=r'\s+', names=['Time', 'CWND'])
        df_cwnd['CWND'] = df_cwnd['CWND'] * MSS 
        df_cwnd = df_cwnd.sort_values(by=['Time'])
        
        df_merged = pd.merge_asof(
            df_node,
            df_cwnd,
            on='Time',
            direction='backward'
        )
        
        # Post-Processing
        df_merged['CWND'] = df_merged['CWND'].fillna(0)
        df_merged['time_diff'] = df_merged['Time'].diff().fillna(0) 
        
        all_processed_data.append(df_merged)
        print(f"  Processed Node {node_id} -> {len(df_merged)} rows")

    # --- Combine and Save Final Dataset ---
    if all_processed_data:
        print("\n--- 3. Saving final dataset ---")
        final_df = pd.concat(all_processed_data, ignore_index=True)
        
        final_df = final_df.sort_values(by=['NodeID', 'Time']).reset_index(drop=True)
        
        final_df.to_csv(OUTPUT_FILE, index=False)
        print(f"✅ Full processing complete! Saved {len(final_df)} total rows to {OUTPUT_FILE}")
    else:
        print("❌ No data processed. Check file paths and PCAP contents.")

if __name__ == "__main__":
    process_combined_pcap()