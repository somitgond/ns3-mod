
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
