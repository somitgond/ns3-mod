  /* Code created for Lab 4.1 of CS313 - Aug-Dec 2014
Copywrite: Sreelakshmi Manjunath, IIT Mandi
Last edit: Oct 4th 2024.
 */

//
// One TCP Client-Server pair, separated by one router node. The router has a FIFO queue disc with a fixed buffer size.  
//
// - pcap traces also generated in the following files
//   "three-node-prioqdisc-$n-$i.pcap" where n and i represent node and interface numbers respectively. 

#include <iostream> 
#include <fstream>
#include <string>
#include "ns3/core-module.h"
#include "ns3/applications-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/traffic-control-helper.h"
#include "ns3/traffic-control-module.h"
#include "ns3/traffic-control-layer.h"
#include "ns3/point-to-point-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/bulk-send-application.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/tcp-socket-factory.h"
#include "ns3/packet-filter.h"
#include "ns3/ipv4-header.h"
#include "ns3/udp-client.h"
#include "ns3/udp-header.h"
#include "ns3/tcp-header.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("PriorityQueueDisc");


using namespace ns3;



// Client parameters
uint16_t port1 = 80; 
uint16_t port2 = 9;


// The number of bytes to send in this simulation.
static const uint32_t totalTxBytes = 5000000000;
static uint32_t currentTxBytes = 0;
// Perform series of 1040 byte writes (this is a multiple of 26 since
// we want to detect data splicing in the output stream)
static const uint32_t writeSize = 1040;
uint8_t data[writeSize];

void StartFlow (Ptr<Socket>, Ipv4Address, uint16_t);
void WriteUntilBufferFull (Ptr<Socket>, uint32_t);



int main (int argc, char *argv[])
{

  CommandLine cmd (__FILE__);
  cmd.Parse (argc, argv);

//LogComponentEnable("TrafficControlLayer", LOG_LEVEL_ALL);
//LogComponentEnable("QueueDisc", LOG_LEVEL_ALL);
LogComponentEnable("PrioQueueDisc", LOG_LEVEL_ALL);
//LogComponentEnable("FifoQueueDisc", LOG_LEVEL_ALL);

// Create nodes

NodeContainer Nodes;
Nodes.Create (3);

NodeContainer ClientRouter1;
ClientRouter1.Add(Nodes.Get(0));
ClientRouter1.Add(Nodes.Get(1));


NodeContainer RouterServer;
RouterServer.Add(Nodes.Get(1));
RouterServer.Add(Nodes.Get(2));

// Creating all the required p2p channels

  PointToPointHelper p2p_ClientRouter1;
  p2p_ClientRouter1.SetDeviceAttribute ("DataRate", DataRateValue (DataRate (2000000)));
  p2p_ClientRouter1.SetChannelAttribute ("Delay", TimeValue (MilliSeconds (2)));
    p2p_ClientRouter1.DisableFlowControl();
    


  PointToPointHelper p2p_RouterServer;
  p2p_RouterServer.SetDeviceAttribute ("DataRate", DataRateValue (DataRate (2000000)));
  p2p_RouterServer.SetChannelAttribute ("Delay", TimeValue (MilliSeconds (2)));
  p2p_RouterServer.DisableFlowControl();

  // And then install devices and channels connecting the nodes are required for the given topology. One device container for each p2p link
  NetDeviceContainer dev0 = p2p_ClientRouter1.Install (ClientRouter1);
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
  
  
    
    // Remove any existing queue disc that might be installed
    for (NetDeviceContainer::Iterator i = dev1.Begin(); i != dev1.End(); ++i) {
        Ptr<NetDevice> device = *i;
        Ptr<TrafficControlLayer> tcLayer = device->GetNode()->GetObject<TrafficControlLayer>();

        if (tcLayer != nullptr) {
            Ptr<QueueDisc> rootDisc = tcLayer->GetRootQueueDiscOnDevice(device);
            if (rootDisc != nullptr) {
                tcLayer->DeleteRootQueueDiscOnDevice(device);  // Remove existing queue disc
            }
        }
    }


  TrafficControlHelper tchPri;
 uint16_t handle = tchPri.SetRootQueueDisc("ns3::PrioQueueDisc", "Priomap", StringValue("0 1 0 1 0 1 0 1 0 1 0 1 0 1 0 1"));
  TrafficControlHelper::ClassIdList cid = tchPri.AddQueueDiscClasses(handle, 2, "ns3::QueueDiscClass");
   tchPri.AddChildQueueDisc(handle, cid[0], "ns3::FifoQueueDisc", "MaxSize", QueueSizeValue(QueueSize("100p")));
   tchPri.AddChildQueueDisc(handle, cid[1], "ns3::FifoQueueDisc", "MaxSize", QueueSizeValue(QueueSize("100p")));


    tchPri.Install(dev1.Get(0));    
tchPri.AddPacketFilter(0,"ns3::TcpSocketFactory");  // TCP - High Priority (Band 0)
  tchPri.AddPacketFilter(1,"ns3::UdpSocketFactory");  // UDP - Low Priority (Band 1)




  // Setup ip routing tables to get total ip-level connectivity.
  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

 // Create a packet sink to receive these packets on n2...
  PacketSinkHelper sink ("ns3::TcpSocketFactory",
                         InetSocketAddress (Ipv4Address::GetAny (), port1));

  ApplicationContainer apps = sink.Install (RouterServer.Get(1));
  apps.Start (Seconds (0.0));
  apps.Stop (Seconds (150.0));

  Ptr<Socket> localSocket =
    Socket::CreateSocket (ClientRouter1.Get (0), TcpSocketFactory::GetTypeId ());
  localSocket->Bind ();
  
    Simulator::ScheduleNow (&StartFlow, localSocket,
                          ipInterfs1.GetAddress (1), port1);


  // Setup UDP Client-Server on the same node as the TCP


    UdpServerHelper server(port2);
    ApplicationContainer UDPapps = server.Install(RouterServer.Get(1));
    UDPapps.Start(Seconds(0.0));
    UDPapps.Stop(Seconds(150.0));


    uint32_t maxPacketCount = 100000000;
    Time interPacketInterval = Seconds(0.008);
    UdpClientHelper client(ipInterfs1.GetAddress(1), port2);
    client.SetAttribute("MaxPackets", UintegerValue(maxPacketCount));
    client.SetAttribute("Interval", TimeValue(interPacketInterval));
    client.SetAttribute("PacketSize", UintegerValue(1040));
    UDPapps = client.Install(ClientRouter1.Get(0));
    UDPapps.Start(Seconds(0.0));
    UDPapps.Stop(Seconds(150.0));


 
  // Enable pcap tracing

  p2p_RouterServer.EnablePcapAll ("three-node-prioqdisc");

  // Set up the simulator to run. 
  Simulator::Stop (Seconds (150.0));
  Simulator::Run ();
  Simulator::Destroy ();
}


//begin implementation of sending "Application"
void StartFlow (Ptr<Socket> localSocket,
                Ipv4Address servAddress,
                uint16_t servPort)
{
  NS_LOG_LOGIC ("Starting flow at time " <<  Simulator::Now ().GetSeconds ());
  localSocket->Connect (InetSocketAddress (servAddress, servPort)); //connect

  // tell the tcp implementation to call WriteUntilBufferFull again
  // if we blocked and new tx buffer space becomes available
  localSocket->SetSendCallback (MakeCallback (&WriteUntilBufferFull));
  WriteUntilBufferFull (localSocket, localSocket->GetTxAvailable ());
}

void WriteUntilBufferFull (Ptr<Socket> localSocket, uint32_t txSpace)
{
  while (currentTxBytes < totalTxBytes && localSocket->GetTxAvailable () > 0) 
    {
      uint32_t left = totalTxBytes - currentTxBytes;
      uint32_t dataOffset = currentTxBytes % writeSize;
      uint32_t toWrite = writeSize - dataOffset;
      toWrite = std::min (toWrite, left);
      toWrite = std::min (toWrite, localSocket->GetTxAvailable ());
      int amountSent = localSocket->Send (&data[dataOffset], toWrite, 0);
      if(amountSent < 0)
        {
          // we will be called again when new tx space becomes available.
          return;
        }
      currentTxBytes += amountSent;
    }
  if (currentTxBytes >= totalTxBytes)
    {
      localSocket->Close ();
    }
}

