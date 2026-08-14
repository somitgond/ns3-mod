import pandas as pd
import numpy as np
import joblib
import matplotlib.pyplot as plt
from collections import deque
import os
import argparse
from sklearn.metrics import mean_squared_error

# --- Configuration ---
MODEL_FILE = '' # Update with your trained model path
CSV_FILE = '' # Updated to match your new output file
OUTPUT_FOLDER = '' # Updated to match your new output folder
WINDOW_SIZE = 10 
OMIT_SLOW_START = False 

# Slow Start Heuristics 
SKIP_INITIAL_PACKETS = 40
DROP_THRESHOLD = 20000.0

def get_sawtooth_phase(group):
    """
    Helper function to strip slow start phase based on heuristics.
    """
    group = group.sort_values('Time').reset_index(drop=True)
    
    if len(group) < SKIP_INITIAL_PACKETS:
        return pd.DataFrame()
    
    subset = group.iloc[SKIP_INITIAL_PACKETS:]
    
    # Find cut point (Strict Mode)
    below_threshold = subset[subset['CWND'] < DROP_THRESHOLD].index
    
    if len(below_threshold) > 0:
        cut_idx = below_threshold[0]
        print(f"  Sawtooth Detected: Starting at packet index {cut_idx} (CWND={group.loc[cut_idx, 'CWND']})")
        return group.iloc[cut_idx:]
    else:
        return pd.DataFrame()

def analyze_node(node_id, df_full, model, output_folder, init_mode='ground_truth'):
    """
    Runs the recursive prediction simulation for a specific Node ID.
    init_mode options: 'ground_truth', 'replicate_first', 'from_scratch'
    """
    print(f"\n--- Processing Node {node_id} (Init Mode: {init_mode}) ---")
    
    df_node = df_full[df_full['NodeID'] == node_id].copy()

    if df_node.empty:
        print(f"Warning: No data found for Node {node_id}. Skipping.")
        return
    
    # Generate DupAck Features BEFORE filtering anything
    df_node = df_node.sort_values('Time')
    df_node['Prev_Ack'] = df_node['Ack_Number'].shift(1).fillna(-1)
    df_node['Is_Dup'] = (df_node['Ack_Number'] == df_node['Prev_Ack']) & (df_node['Direction'] == 1)
    df_node['Dup_Group'] = (~df_node['Is_Dup']).cumsum()
    df_node['DupAck_Count'] = df_node.groupby('Dup_Group').cumcount()

    valid_cwnd_indices = df_node[df_node['CWND'] > 0].index
    
    if len(valid_cwnd_indices) == 0:
        print(f"Warning: No valid CWND data (CWND > 0) found for Node {node_id}. Skipping.")
        return
        
    first_valid_idx = valid_cwnd_indices[0]
    print(f"  Filtering initial 0s: Starting data at original index {first_valid_idx}")
    
    # Slice the dataframe from the first valid CWND onwards
    df_node = df_node.loc[first_valid_idx:].reset_index(drop=True)

    # Filter / Remove Slow Start (Optional)
    if OMIT_SLOW_START:
        df_test = get_sawtooth_phase(df_node)
        if df_test.empty:
            print(f"Warning: No Sawtooth phase detected for Node {node_id}. Skipping.")
            return
        df_test = df_test.reset_index(drop=True)
    else:
        df_test = df_node

    print(f"  Data Ready for Simulation. Size: {len(df_test)}")

    feature_cols = ['Direction', 'Seq_Diff', 'Ack_Diff', 'Packet_Size', 'time_diff', 'DupAck_Count']
    target_col = 'CWND'

    if not all(col in df_test.columns for col in feature_cols):
        print(f"Error: Missing columns in dataframe. Expected {feature_cols}")
        return

    packet_features_all = df_test[feature_cols].values
    ground_truth_cwnd = df_test[target_col].values

    # Initialization
    packet_window = deque(maxlen=WINDOW_SIZE)
    for i in range(WINDOW_SIZE):
        packet_window.append(packet_features_all[i])

    prediction_window = deque(maxlen=WINDOW_SIZE - 1)
    predictions = []
    
    # Actuals always use the real ground truth so the blue line is accurate
    time_values = list(df_test['Time'].values[:WINDOW_SIZE-1])
    actuals = list(ground_truth_cwnd[:WINDOW_SIZE-1]) 

    if init_mode == 'ground_truth':
        # Mode 0: Original behavior (uses first 9 ground truth values)
        for i in range(WINDOW_SIZE - 1):
            prediction_window.append(ground_truth_cwnd[i])
            predictions.append(ground_truth_cwnd[i])
            
    elif init_mode == 'replicate_first':
        # Mode 1: Take the very first CWND value and replicate it
        first_val = ground_truth_cwnd[0]
        for i in range(WINDOW_SIZE - 1):
            prediction_window.append(first_val)
            predictions.append(first_val)
            
    elif init_mode == 'from_scratch':
        # Mode 2: Assume 10 MSS (14000) for the initial sequence
        scratch_val = 14000.0
        for i in range(WINDOW_SIZE - 1):
            prediction_window.append(scratch_val)
            predictions.append(scratch_val)
            
    else:
        print(f"Error: Unknown init_mode '{init_mode}'. Skipping node.")
        return

    # The Live Loop
    print(f"  Starting Recursive Loop...")
    
    for i in range(WINDOW_SIZE, len(df_test)):
        current_packet_features = packet_features_all[i]
        packet_window.append(current_packet_features)
        
        feat_vector = np.array(packet_window).flatten()
        cwnd_vector = np.array(prediction_window) 
        
        input_vector = np.concatenate([feat_vector, cwnd_vector]).reshape(1, -1)
        
        pred_cwnd = model.predict(input_vector)[0]
        
        prediction_window.append(pred_cwnd)
        predictions.append(pred_cwnd)
        actuals.append(ground_truth_cwnd[i])

        time_values.append(df_test['Time'].values[i])

        if i % 5000 == 0:
            print(f"Step {i}: Pred={pred_cwnd:.0f} | Actual={ground_truth_cwnd[i]:.0f}")

    # Evaluation
    rmse = np.sqrt(mean_squared_error(actuals, predictions))
    print(f"  -> Recursive RMSE: {rmse:.4f}")

    results_df = pd.DataFrame({
        'Time': time_values,
        'Ground_Truth_CWND': actuals,
        'Predicted_CWND': predictions
    })
    csv_filename = f"rf_prediction_data_node_{node_id}.csv"
    csv_save_path = os.path.join(output_folder, csv_filename)
    results_df.to_csv(csv_save_path, index=False)
    print(f"  -> CSV Data saved to '{csv_save_path}'")

    # Plotting and Display
    plt.figure(figsize=(20, 8)) 

    plt.plot(actuals, label='Ground Truth', color='blue', alpha=0.6, linewidth=1.5)
    plt.plot(predictions, label='Recursive Prediction (RF)', color='red', linestyle='--', alpha=0.9, linewidth=1.5)

    title_text = f'Node {node_id} | Init: {init_mode} | RMSE: {rmse:.2f}'
    plt.title(title_text, fontsize=16)
    plt.xlabel('Packet Step', fontsize=14)
    plt.ylabel('CWND (Bytes)', fontsize=14)
    plt.legend(fontsize=14, loc='upper right')
    plt.grid(True, alpha=0.3)
    plt.tight_layout()

    # Save the plot
    filename = f"rf_prediction_node_{node_id}.png"
    save_path = os.path.join(output_folder, filename)
    plt.savefig(save_path, dpi=300)
    
    print(f"  -> Plot saved to '{save_path}'")

# --- Main Execution ---
if __name__ == "__main__":

    parser = argparse.ArgumentParser(description="Run RF CWND prediction per node.")
    parser.add_argument('--node', type=int, default=-1, help='Specific Node ID to process')
    args = parser.parse_args()

    CHOSEN_MODE = 'from_scratch' 
    OUTPUT_FOLDER = f'test_node_predictions_weighted_rf_{CHOSEN_MODE}'
    
    if not os.path.exists(OUTPUT_FOLDER):
        os.makedirs(OUTPUT_FOLDER)
        print(f"Created output folder: {OUTPUT_FOLDER}")

    print(f"Loading model from {MODEL_FILE}...")
    rf_model = joblib.load(MODEL_FILE)

    print(f"Loading test data from {CSV_FILE}...")
    df_full = pd.read_csv(CSV_FILE)
    
    # If a node was passed via bash, use only that node. Otherwise use defaults.
    if args.node != -1:
        nodes_to_test = [args.node]
    else:
        nodes_to_test = []
    
    for node in nodes_to_test:
        analyze_node(node, df_full, rf_model, OUTPUT_FOLDER, init_mode=CHOSEN_MODE)
        
    print("\nAll nodes processed successfully.")