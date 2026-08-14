
The Network Simulator, Version 3
================================

ns-3.36.1 official source code + customizations
Prerequisites:
```bash
sudo apt install cmake g++ python3-dev pkg-config sqlite3
```
Install with:
```bash
cd ns3-mod
./ns3 configure --enable-examples --enable-tests
./ns3 build
```
Test for successfull compilation:
```bash
./ns3 run hello-simulator
```

Following network topology are available in scratch/ folder for simulation:  
- Dumbbell topology: With Homonogenous (clientServerRouter.cc) + Heterogenous
  traffic (clientServerRouter-trafficMix.cc) scenario
- Parking Lot topology: With Homonogenous (parkingLot.cc) + Heterogenous
  traffic (parkingLot-trafficMix.cc) scenario



## Publications


1. S. Bhunia, S. Gond, S. Manjunath and P. Rajan, "A mechanism for TCP-aware
adaptive management of queues in Internet routers," 2026 18th International
Conference on COMmunication Systems and NETworks (COMSNETS), Bengaluru, India,
2026, pp. 409-415, doi: 10.1109/COMSNETS67989.2026.11418299. keywords:
{Transport protocols;Analytical models;Fluids;Computational
modeling;Linux;Rendering (computer graphics);Performance metrics;Routing
protocols;Internet;Delays;Bufferbloat;Active Queue
Management;Latency;Congestion Control},
