import pandas as pd
import numpy as np
from sklearn.ensemble import RandomForestRegressor
from sklearn.model_selection import train_test_split
from sklearn.metrics import mean_squared_error, mean_absolute_percentage_error
import matplotlib.pyplot as plt
import joblib

# --- Configuration ---
SKIP_INITIAL_PACKETS = 40
DROP_THRESHOLD = 20000.0
omit_slow_start = True

RF_CONFIG = {
    'n_estimators': 100,
    'max_depth': 235,
    'max_features': 'sqrt',
    'min_samples_split': 5,
    'random_state': 42,
    'n_jobs': -1
}

WINDOW_SIZE = 10
CSV_PATH = '' # Update with your training CSV path
MODEL_PATH = '' # Update with your desired output model path

# --- Load Data ---
print(f"Loading data from {CSV_PATH}...")
df = pd.read_csv(CSV_PATH)

# --- Remove Slow Start ---
print("Filtering out Slow Start phases...")

def remove_slow_start(group):
    group = group.sort_values('Time').reset_index(drop=True)
    if len(group) < SKIP_INITIAL_PACKETS: return pd.DataFrame()
    
    subset = group.iloc[SKIP_INITIAL_PACKETS:]
    below_thresh = subset[subset['CWND'] < DROP_THRESHOLD].index
    
    if len(below_thresh) > 0:
        return group.iloc[below_thresh[0]:]
    return pd.DataFrame()

if omit_slow_start:
    df_sawtooth = df.groupby('NodeID', group_keys=False).apply(remove_slow_start).reset_index(drop=True)
else:
    df_sawtooth = df.copy()

print(f"Available columns: {df_sawtooth.columns.tolist()}")

print(f"Training Rows: {len(df_sawtooth)}")

# --- Feature Engineering ---
print("Generating Sliding Window Features with DupAck & Weights...")

# Added 'DupAck_Count' to feature list
feature_cols = ['Direction', 'Seq_Diff', 'Ack_Diff', 'Packet_Size', 'time_diff', 'DupAck_Count']
target_col = 'CWND'

def create_weighted_dataset(dataframe, window_size):
    X_all = []
    y_all = []
    w_all = [] # Weights
    
    for node_id, group in dataframe.groupby('NodeID'):
        group = group.sort_values('Time').reset_index(drop=True)
        
        group['Prev_Ack'] = group['Ack_Number'].shift(1).fillna(-1)
        group['Is_Dup'] = (group['Ack_Number'] == group['Prev_Ack']) & (group['Direction'] == 1)
        group['Dup_Group'] = (~group['Is_Dup']).cumsum()
        group['DupAck_Count'] = group.groupby('Dup_Group').cumcount()
        
        if len(group) < window_size + 1:
            continue
            
        feat_values = group[feature_cols].values
        cwnd_values = group[target_col].values
        
        for i in range(window_size, len(group)):
            # Features
            packet_window = feat_values[i-window_size+1 : i+1].flatten()
            cwnd_window = cwnd_values[i-window_size+1 : i]
            combined = np.concatenate([packet_window, cwnd_window])
            
            # Target (Absolute CWND)
            current_target = cwnd_values[i]
            
            # Weight Calculation
            # Weight = | Current_CWND - Prev_CWND |
            # If it's a huge drop, weight is huge. If steady, weight is small.
            # We add 1.0 to ensure steady state doesn't get 0 weight.
            prev_target = cwnd_values[i-1]
            diff = abs(current_target - prev_target)
            weight = 1.0 + diff
            
            X_all.append(combined)
            y_all.append(current_target)
            w_all.append(weight)
            
    return np.array(X_all), np.array(y_all), np.array(w_all)

X, y, weights = create_weighted_dataset(df_sawtooth, WINDOW_SIZE)
print(f"Dataset Shape: {X.shape}")

# --- Train/Test Split ---
# Pass weights to split so they correspond to the right samples
X_train, X_test, y_train, y_test, w_train, w_test = train_test_split(
    X, y, weights, test_size=0.1, shuffle=True, random_state=42
)

print(f"Train Size: {len(X_train)}, Test Size: {len(X_test)}")

# --- Train Random Forest ---
print("Training Random Forest with Sample Weights...")
rf_model = RandomForestRegressor(**RF_CONFIG)

# Pass sample_weight here
rf_model.fit(X_train, y_train, sample_weight=w_train)

# --- Evaluation ---
print("Evaluating...")
y_pred = rf_model.predict(X_test)

rmse = np.sqrt(mean_squared_error(y_test, y_pred))
mape = mean_absolute_percentage_error(y_test, y_pred) * 100

print(f"Results:")
print(f"RMSE: {rmse:.4f}")
print(f"MAPE: {mape:.4f}%")

# --- Plotting Verification ---
print("\nRunning Reconstruction Test on Node 11...")
node_11 = df[df['NodeID'] == 11].sort_values('Time').reset_index(drop=True)

if len(node_11) > WINDOW_SIZE + 100:
    # Generate features for Node 11 (Need to regen DupAcks first)
    # Note: We must re-run the dup-ack logic just like in training
    node_11['Prev_Ack'] = node_11['Ack_Number'].shift(1).fillna(-1)
    node_11['Is_Dup'] = (node_11['Ack_Number'] == node_11['Prev_Ack']) & (node_11['Direction'] == 1)
    node_11['Dup_Group'] = (~node_11['Is_Dup']).cumsum()
    node_11['DupAck_Count'] = node_11.groupby('Dup_Group').cumcount()

    # Create dataset (we ignore weights/y for inference)
    feat_values = node_11[feature_cols].values
    cwnd_values = node_11[target_col].values
    X_val = []
    
    # Start after window
    for i in range(WINDOW_SIZE, len(node_11)):
        p_win = feat_values[i-WINDOW_SIZE+1 : i+1].flatten()
        c_win = cwnd_values[i-WINDOW_SIZE+1 : i]
        X_val.append(np.concatenate([p_win, c_win]))
    
    X_val = np.array(X_val)
    
    # Predict Absolute Values
    pred_absolute = rf_model.predict(X_val)
    
    # Ground Truth (aligned)
    ground_truth = cwnd_values[WINDOW_SIZE:]
    
    # Plot
    plt.figure(figsize=(14, 6))
    limit = 2000
    plt.plot(ground_truth[:limit], label='Ground Truth', color='blue', alpha=0.6)
    plt.plot(pred_absolute[:limit], label='Weighted RF Prediction', color='red', linestyle='--')
    plt.title(f'Node 11 Test (Absolute Prediction with Weights + DupAck)')
    plt.ylabel('CWND')
    plt.xlabel('Step')
    plt.legend()
    plt.show()

# --- 8. Save Model ---
joblib.dump(rf_model, MODEL_PATH)
print(f"Model saved to {MODEL_PATH}")