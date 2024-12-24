/* Code created for Lab 4.1 of CS313 - Aug-Dec 2014
Copywrite: Sreelakshmi Manjunath, IIT Mandi
Last edit: Oct 4th 2024.
 */

//
// One TCP Client-Server pair, separated by one router node. The router has a FIFO queue disc with a fixed buffer size.  
//
// - pcap traces also generated in the following files
//   "three-node-buffermanagement-$n-$i.pcap" where n and i represent node and interface numbers respectively. 

#include <iostream> 
#include <fstream>
#include <string>
#include "ns3/core-module.h"
#include "ns3/applications-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/bulk-send-application.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("BufferManagement");


// TCP client parameters
uint16_t port1 = 80; 


// Defining the variable and data stream required for queue size tracing
uint64_t queueSize;
Ptr<OutputStreamWrapper> qSize_stream;

// Defining the variables and data stream required for tracing dropped packets
uint64_t droppedPackets;
uint64_t prev_droppedPackets;
Ptr<OutputStreamWrapper> dropped_stream;

// Function for tracing queue size
static void
TraceQueueSize(Ptr<OutputStreamWrapper> qSize_stream, uint32_t oldQSize, uint32_t newQSize){
        queueSize = newQSize;
        *qSize_stream->GetStream() << Simulator::Now().GetSeconds() << "\t" << queueSize << std::endl;
}

// Functions for tracing dropped packets

static void
PacketDrop (Ptr<OutputStreamWrapper> dropped_stream, Ptr<const Packet> p){
   std::cout << "Packet Dropped (finally!)" << std::endl;
   droppedPackets++;
} 

static void PacketDropSetup(){
	Config::ConnectWithoutContext("/NodeList/1/DeviceList/1/TxQueue/Drop",MakeBoundCallback(&PacketDrop, dropped_stream));
}


static void TraceDroppedPackets(){
	float packetLoss = droppedPackets - prev_droppedPackets;
   *dropped_stream-> GetStream() << Simulator::Now().GetSeconds() << "\t" << packetLoss << std::endl;
   prev_droppedPackets = droppedPackets;
}

int main (int argc, char *argv[])
{

  CommandLine cmd (__FILE__);
  cmd.Parse (argc, argv);

//LogComponentEnable("BufferManagement", LOG_LEVEL_INFO);
//LogComponentEnable("DropTailQueue", LOG_LEVEL_ALL);

// Create nodes

NodeContainer Nodes;
Nodes.Create (3);

NodeContainer ClientRouter;
ClientRouter.Add(Nodes.Get(0));
ClientRouter.Add(Nodes.Get(1));

NodeContainer RouterServer;
RouterServer.Add(Nodes.Get(1));
RouterServer.Add(Nodes.Get(2));

// Creating all the required p2p channels

  PointToPointHelper p2p_ClientRouter;
  p2p_ClientRouter.SetDeviceAttribute ("DataRate", DataRateValue (DataRate (3000000)));
  p2p_ClientRouter.SetChannelAttribute ("Delay", TimeValue (MilliSeconds (2)));

  PointToPointHelper p2p_RouterServer;
  p2p_RouterServer.SetDeviceAttribute ("DataRate", DataRateValue (DataRate (2000000)));
  p2p_RouterServer.SetChannelAttribute ("Delay", TimeValue (MilliSeconds (2)));
 p2p_RouterServer.SetQueue ("ns3::DropTailQueue<Packet>", "MaxSize", QueueSizeValue (QueueSize ("50p")));
  p2p_RouterServer.DisableFlowControl();

  // And then install devices and channels connecting the nodes are required for the given topology. One device container for each p2p link
  NetDeviceContainer dev0 = p2p_ClientRouter.Install (ClientRouter);
  NetDeviceContainer dev1 = p2p_RouterServer.Install (RouterServer);


  // Now add ip/tcp stack to all nodes.
  InternetStackHelper internet;
  internet.InstallAll ();

  // Adding IP addresses to all the devices created above
  Ipv4AddressHelper ipv4;
  ipv4.SetBase ("10.1.1.0", "10.1.255.0");
  Ipv4InterfaceContainer ipInterfs0 = ipv4.Assign (dev0);
  ipv4.SetBase ("10.2.1.0", "10.2.255.0");
  Ipv4InterfaceContainer ipInterfs1 = ipv4.Assign (dev1);
  
  // and setup ip routing tables to get total ip-level connectivity.
  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

  BulkSendHelper TCPsource ("ns3::TcpSocketFactory",
                         InetSocketAddress (ipInterfs1.GetAddress (1), port1));
  // Set the amount of data to send in bytes.  Zero is unlimited.
  TCPsource.SetAttribute ("MaxBytes", UintegerValue (0));
  TCPsource.SetAttribute("SendSize", UintegerValue(1040));
  ApplicationContainer TCPsourceApps = TCPsource.Install (ClientRouter.Get (0));
  TCPsourceApps.Start (Seconds (0.0));
  TCPsourceApps.Stop (Seconds (149.0));
// 
  PacketSinkHelper TCPsink ("ns3::TcpSocketFactory",
                         InetSocketAddress (Ipv4Address::GetAny(), port1));
  ApplicationContainer TCPsinkApps = TCPsink.Install(RouterServer.Get(1));
  TCPsinkApps.Start (Seconds (0.0));
  TCPsinkApps.Stop (Seconds (150.0)); 
   
  // Trace changes to the queue size at the router
  AsciiTraceHelper ascii_qsize;
    qSize_stream = ascii_qsize.CreateFileStream("queue.tr");

  //  Trace packet drops at the left router 
  AsciiTraceHelper ascii_dropped;
    dropped_stream = ascii_dropped.CreateFileStream ("pktdrops.tr");


  // Enable pcap tracing

  p2p_RouterServer.EnablePcapAll ("three-node-buffermanagement");

// Call function for tracing queue size at router node output link interface
 Config::ConnectWithoutContext("/NodeList/1/DeviceList/1/$ns3::PointToPointNetDevice/TxQueue/PacketsInQueue", MakeBoundCallback(&TraceQueueSize, qSize_stream));

//Call the function PacketDropSetup at the beginning of the simulation 
 Simulator::Schedule(Seconds(0.01), &PacketDropSetup);

// Periodically write the number of dropped packets using function TraceDroppedPackets
 for (float t = 0; t < 151; t = t+ 0.1){
 	Simulator::Schedule(Seconds(t), &TraceDroppedPackets);
 }
 
  // Set up the simulator to run. 
  Simulator::Stop (Seconds (150));
  Simulator::Run ();
  Simulator::Destroy ();
}


