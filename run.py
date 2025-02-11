# python script to run ns3 simulations

import os
import subprocess
import sys

if __name__ == "__main__":
    print("Hello")
    # Command to run in the shell
    cmd_to_run = ["./ns3", "run", "scratch/clientServerRouter.cc", "--"]

    print(sys.argv)
    if(len(sys.argv) < 2):
        print("""
        Select different profiles to run the simulation:
        1 : run scratch/clientServerRouter.cc with no arguments
        2 : run scratch/dumbbell.cc with no arguments
        3 : run scratch/dumbbell.cc with tcpNewReno
        4 : run scratch/dumbbellRegularInterval.cc
        5 : run scratch/dumbbellRegularInterval.cc with tcpNewReno
        """)
    else:
        profile = sys.argv[1]
        if(profile == '1'):
            subprocess.run(cmd_to_run)
        elif(profile == '2'):
            cmd_to_run = ["./ns3", "run", "scratch/dumbbell.cc", "--"]
            subprocess.run(cmd_to_run)
        elif(profile == '3'):
            cmd_to_run = ["./ns3", "run", "scratch/dumbbell.cc", "--", "--tcp_type_id=ns3::TcpNewReno"]
            subprocess.run(cmd_to_run)
        elif(profile == '4'):
            cmd_to_run = ["./ns3", "run", "scratch/dumbbellRegularInterval.cc", "--"]
            subprocess.run(cmd_to_run)
        elif(profile == '5'):
            cmd_to_run = ["./ns3", "run", "scratch/dumbbellRegularInterval.cc", "--", "--tcp_type_id=ns3::TcpNewReno"]
            subprocess.run(cmd_to_run)

