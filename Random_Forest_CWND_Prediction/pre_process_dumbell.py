import os
import glob
import pandas as pd
from scapy.all import PcapReader, IP, TCP

# --- CONFIGURATION ---
DATA_DIR = ""  # Set this to the folder containing your .pcap and .cwnd files
OUTPUT_FILE = "" # Update with your desired output CSV filename
MSS = 1400      # Max Segment Size to convert CWND to absolute bytes

def process_all_nodes(data_dir, output_csv):
    all_processed_data = []
    
    # 1. Find all new format PCAP files: sourceNode-x-0-x+1.pcap
    search_pattern = os.path.join(data_dir, "sourceNode-*-0-*.pcap")
    pcap_files = glob.glob(search_pattern)
    
    if not pcap_files:
        print(f"No PCAP files found matching the pattern '{search_pattern}'.")
        return

    for pcap_file in pcap_files:
        # 2. Extract 'x' from the filename
        basename = os.path.basename(pcap_file)
        parts = basename.split('-')
        
        try:
            x = int(parts[1])
        except (IndexError, ValueError):
            print(f"Skipping {basename}: Could not parse 'x' from filename.")
            continue
            
        # 3. Setup Node Logic based on 'x'
        node_id = x + 2
        sender_ip = f"10.1.{x}.1"
        receiver_ip = f"10.1.{x+60}.1"
        cwnd_file = os.path.join(data_dir, f"dumbbell-{node_id}.cwnd")
        
        if not os.path.exists(cwnd_file):
            print(f"Warning: CWND file {cwnd_file} not found for {basename}. Skipping node.")
            continue

        print(f"\n--- Processing Node ID {node_id} (x={x}) ---")
        print(f"Flow: {sender_ip} <--> {receiver_ip}")

        extracted_packets = []
        packet_count = 0
        
        # 4. Read and Filter PCAP (In-Memory)
        with PcapReader(pcap_file) as pcap_reader:
            for pkt in pcap_reader:
                packet_count += 1
                if packet_count % 10000 == 0:
                    print(f"Scanned {packet_count} packets...", end='\r')

                if not pkt.haslayer(IP) or not pkt.haslayer(TCP):
                    continue
                    
                ip_layer = pkt[IP]
                tcp_layer = pkt[TCP]
                
                # Identify Direction directly during extraction
                if ip_layer.src == sender_ip and ip_layer.dst == receiver_ip:
                    direction = 0  # DATA (Send)
                elif ip_layer.src == receiver_ip and ip_layer.dst == sender_ip:
                    direction = 1  # ACK (Recv)
                else:
                    continue  # Skip unrelated packets
                    
                payload_len = len(tcp_layer.payload)
                
                # Skip empty sender ACKs unless SYN/FIN
                if direction == 0 and payload_len == 0 and not (tcp_layer.flags.S or tcp_layer.flags.F):
                    continue
                    
                extracted_packets.append({
                    "Time": float(pkt.time),
                    "NodeID": node_id,
                    "Direction": direction,
                    "Seq_Number": int(tcp_layer.seq),
                    "Ack_Number": int(tcp_layer.ack),
                    "Packet_Size": len(pkt),
                    "Payload_Size": payload_len
                })
        
        if not extracted_packets:
            print(f"\nNo relevant TCP packets found for Node {node_id}.")
            continue
            
        # 5. Calculate Sequence & Ack Diffs
        df_packets = pd.DataFrame(extracted_packets).sort_values(by=['Time'])
        
        df_packets['Seq_Diff'] = df_packets.groupby('Direction')['Seq_Number'].transform(lambda val: val.diff().fillna(0)).astype(int)
        df_packets['Ack_Diff'] = df_packets.groupby('Direction')['Ack_Number'].transform(lambda val: val.diff().fillna(0)).astype(int)
        
        # 6. Read and Format CWND File
        df_cwnd = pd.read_csv(cwnd_file, sep=r'\s+', names=['Time', 'CWND'])
        df_cwnd['CWND'] = df_cwnd['CWND'] * MSS 
        df_cwnd = df_cwnd.sort_values(by=['Time'])
        
        # 7. Merge Packets with CWND (Time Alignment)
        df_merged = pd.merge_asof(
            df_packets,
            df_cwnd,
            on='Time',
            direction='backward'
        )
        
        # 8. Post-Processing for this specific Node
        df_merged['CWND'] = df_merged['CWND'].fillna(0)
        df_merged['time_diff'] = df_merged['Time'].diff().fillna(0) 
        
        all_processed_data.append(df_merged)
        print(f"\nSuccessfully processed {len(df_merged)} aligned rows for Node {node_id}.")

    # 9. Combine and Save Final Dataset
    if all_processed_data:
        print("\nCombining all node data into final dataset...")
        final_df = pd.concat(all_processed_data, ignore_index=True)
        
        final_df = final_df.sort_values(by=['NodeID', 'Time']).reset_index(drop=True)
        
        final_df.to_csv(output_csv, index=False)
        print(f"✅ Full processing complete! Saved {len(final_df)} total rows to {output_csv}")
    else:
        print("❌ No data processed. Check file paths and contents.")

if __name__ == "__main__":
    process_all_nodes(DATA_DIR, OUTPUT_FILE)