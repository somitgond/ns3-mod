# TCP Congestion Window (CWND) Prediction from a Middle Router  
By: Vayun

## Overview

This code is used for predicting the **Congestion Window (CWND)** of TCP clients from observations made at an **intermediate router** along the path between a client and server.

The goal is to estimate the sender's CWND without direct access to the sender host, using network-level features extracted from packet captures (PCAPs). The approach uses a **Random Forest Regressor** trained on features derived from observed TCP traffic.

Two network topologies were studied:

1. **Dumbbell Topology**
2. **Parking Lot Topology**


## Project Workflow

```text
PCAP + CWND Logs
        │
        ▼
Preprocessing
        │
        ▼
Feature Extraction
        │
        ▼
Training Dataset (CSV)
        │
        ▼
Random Forest Training
        │
        ▼
Trained Model
        │
        ▼
CWND Prediction & Evaluation
```

## Files

### pre_process_dumbell.py

Preprocessing pipeline for the Dumbbell topology dataset.

**Purpose**
- Reads packet captures (.pcap) and corresponding CWND traces.
- Extracts TCP-level information observed at the router.
- Aligns packet observations with ground-truth CWND values.
- Generates a consolidated CSV dataset for model training.

### pre_process_parking_lot.py

Preprocessing pipeline for the Parking Lot topology dataset.

**Purpose**
- Processes a single combined PCAP containing traffic from many nodes.
- Reads multiple CWND traces.
- Associates network observations with the correct node.
- Produces a training dataset for model development.

### train_RF.py

Training script for the CWND prediction model.

**Purpose**
- Loads the processed dataset.
- Removes TCP slow-start phases (optional).
- Creates sliding-window features from packet observations.
- Trains a Random Forest Regressor.
- Evaluates performance using RMSE and MAPE.
- Saves the trained model.

Note: Since the dataset was heavily skewed towards normal CWND growth, with comparatively few packet-drop and window-reduction events, weighted samples were used during training. This ensured that rare congestion events had sufficient influence on the learned model and improved prediction performance across different TCP states.

**Features Used**
- Direction
- Seq_Diff
- Ack_Diff
- Packet_Size
- time_diff
- DupAck_Count

### test_RF_weighted.py

Model evaluation and inference script.

**Purpose**
- Loads a trained Random Forest model.
- Performs recursive CWND prediction.
- Compares predicted and actual CWND trajectories.
- Supports multiple prediction initialization modes.

Note: To simulate a real-world monitoring scenario where a router has no knowledge of the sender's current CWND, the first 10 CWND values were initialized to 1400 bytes (1 MSS) and all future values were generated recursively from previous predictions. The model eventually recovered from this naive initialization and tracked the actual CWND behavior.

## Feature Engineering

A sliding-window representation is used to capture temporal dependencies in TCP behavior. For each packet, information from the previous packets is used to construct features describing sequence progression, acknowledgements, timing, packet sizes, and duplicate ACK patterns.
