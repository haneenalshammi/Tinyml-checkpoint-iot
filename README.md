# TinyML with Checkpoint on Intermittently Powered IoT Devices

## Project Overview

This project implements TinyML with checkpointing for intermittently powered IoT devices using Contiki-NG on the FIT IoT-LAB testbed.

The system is designed for a Smart Building Energy Management System (BEMS), where IoT devices operate under intermittent power conditions.

The project uses TinyML to predict potential power failures and checkpointing to preserve the device state before power is lost.

## Objectives

- Predict potential power failures using TinyML.
- Deploy a lightweight machine learning model on an IoT device.
- Implement checkpointing to preserve the device state.
- Evaluate network performance under intermittent power conditions.
- Analyze metrics such as Packet Delivery Ratio (PDR) and latency.

## Technologies Used

- Python
- Pandas
- NumPy
- Scikit-learn
- TinyML
- Contiki-NG
- FIT IoT-LAB
- C
- STM32 Cortex-M3
- Jupyter Notebook

## Machine Learning

The project uses a Random Forest Classifier to predict potential power failures.

The model uses the following features:

- `energy_level`
- `energy_slope`
- `sends_in_session`

The trained model is converted for deployment on the IoT device as lightweight C code.

## System Workflow

```text
Energy Data
     ↓
Data Preprocessing
     ↓
Machine Learning Model
     ↓
Power Failure Prediction
     ↓
TinyML Deployment
     ↓
Checkpointing
     ↓
State Recovery
```

## Experimental Environment
The system was implemented using:

Contiki-NG
FIT IoT-LAB
ARM Cortex-M3
RPL-based wireless sensor networks

The solution was evaluated under intermittent power conditions to analyze the behavior and performance of IoT nodes.

## Results
Prediction Accuracy: 93.3%
The solution was validated on the FIT IoT-LAB platform.
Network performance was evaluated using metrics including Packet Delivery Ratio (PDR) and latency.

## Project Files
IntermittentClient.c — Intermittent IoT client implementation.
IntermittentWithTinyML.c — Intermittent client with TinyML and checkpoint recovery.
udp-client.c — UDP client implementation.
udp-server.c — UDP server implementation.
TinyML_Energy_Predictor.ipynb — Machine learning model training notebook.
energy_predictor.h — TinyML model implementation.
models.h — Model prediction interface.
project.conf.h — Contiki-NG project configuration.
Makefile — Project build configuration.
aggregator_script.txt — Serial data aggregation script.


## How to Run

The project requires a Contiki-NG environment and access to the FIT IoT-LAB testbed.

The source files can be compiled using the provided Makefile.

## Project Information

University: Qassim University
Program: Bachelor of Science in Information Technology
Period: 2025 – 2026
Project Type: Graduation Project

## Author
Haneen Alshami
