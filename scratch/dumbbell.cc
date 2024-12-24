/*
Single bottleneck dumbbell network
*/
#include <sys/stat.h>
#include <iostream>
#include <fstream>
#include <string>
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/error-model.h"
#include "ns3/tcp-header.h"
#include "ns3/udp-header.h"
#include "ns3/enum.h"
#include "ns3/event-id.h"
#include "ns3/flow-monitor-helper.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/traffic-control-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/config-store-module.h"
#include "ns3/node.h"
#include "ns3/netanim-module.h"

#define MAX_SOURCES 100;

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("TCPSCRIPT");

std::string dir = "tcp-dumbbell/";
uint32_t prev = 0;
Time prevTime = Seconds (0);
uint32_t segmentSize = 1400;



std::vector<uint32_t> cwnd;
std::vector<Ptr<OutputStreamWrapper>> cwnd_streams;

uint64_t queue_size;
Ptr<OutputStreamWrapper> qSize_stream;

uint64_t bottleneckTransimitted;
Ptr<OutputStreamWrapper> bottleneckTransimittedStream;

uint64_t droppedPackets;
Ptr<OutputStreamWrapper> dropped_stream;


static void
plotQsizeChange (uint32_t oldQSize, uint32_t newQSize){
    //NS_LOG_UNCOND(Simulator::Now().GetSeconds() << "\t" << newCwnd);
    queue_size = newQSize;
}

static void
RxDrop(Ptr<OutputStreamWrapper> stream,  Ptr<const Packet> p){
   // std::cout << "Packet Dropped (finally!)" << std::endl;
   //*stream->GetStream () << Simulator::Now().GetSeconds() << "\tRxDrop" << std::endl;
   droppedPackets++;
} 

static void
TxPacket(Ptr<const Packet> p){
    bottleneckTransimitted++;
}

static void
TraceDroppedPacket(std::string dropped_trace_filename){
    // tracing all the dropped packets in a seperate file
    Config::ConnectWithoutContext("/NodeList/*/DeviceList/*/$ns3::PointToPointNetDevice/TxQueue/Drop", MakeBoundCallback(&RxDrop, dropped_stream));
    Config::ConnectWithoutContext("/NodeList/*/DeviceList/*/$ns3::PointToPointNetDevice/MacTxDrop", MakeBoundCallback(&RxDrop, dropped_stream));
    Config::ConnectWithoutContext("/NodeList/*/DeviceList/*/$ns3::PointToPointNetDevice/PhyRxDrop", MakeBoundCallback(&RxDrop, dropped_stream));
    Config::ConnectWithoutContext("/NodeList/*/DeviceList/*/$ns3::PointToPointNetDevice/PhyTxDrop", MakeBoundCallback(&RxDrop, dropped_stream));
    //Config::ConnectWithoutContext("/NodeList/*/DeviceList/*/$ns3::PointToPointNetDevice/TcDrop", MakeBoundCallback(&RxDrop, dropped_stream));

}

static void
TraceQueueSize(){
    *qSize_stream->GetStream() << Simulator::Now().GetSeconds() << "\t" << queue_size << std::endl;
}

static void
TraceDroppedPkts(){
    *dropped_stream->GetStream() << Simulator::Now().GetSeconds() << "\t" << droppedPackets << std::endl;
}

static void
TraceBottleneckTx(){
    *bottleneckTransimittedStream->GetStream() << Simulator::Now().GetSeconds() << "\t" << bottleneckTransimitted << std::endl;
}

static void
StartTracingQueueSize(){
    Config::ConnectWithoutContext("/NodeList/0/DeviceList/0/$ns3::PointToPointNetDevice/TxQueue/PacketsInQueue", MakeCallback(&plotQsizeChange));
}

static void
StartTracingTransmitedPacket(){
    bottleneckTransimitted = 0;
    Config::ConnectWithoutContext("/NodeList/0/DeviceList/0/$ns3::PointToPointNetDevice/PhyTxEnd", MakeCallback(&TxPacket));
}

// Trace congestion window
static void 
CwndTracer (Ptr<OutputStreamWrapper> stream, uint32_t oldval, uint32_t newval){
    // NS_LOG_UNCOND(Simulator::Now ().GetSeconds () << "\t"<<oldval<<" " << newval);

    *stream->GetStream () << Simulator::Now ().GetSeconds () << " " << newval/segmentSize<< std::endl;
}

// Write to congestion window streams
static void 
writeCwndToFile(uint32_t n_nodes){
    for(uint32_t i = 0; i < n_nodes; i++){
        Config::ConnectWithoutContext("/NodeList/" + std::to_string(i+2) + "/$ns3::TcpL4Protocol/SocketList/0/CongestionWindow", MakeBoundCallback(&CwndTracer, cwnd_streams[i]));
    }
}

// initialize tracing cwnd streams
static void 
start_tracing_timeCwnd (uint32_t n_nodes){
    for(uint32_t i = 0 ; i < n_nodes; i++){
        AsciiTraceHelper ascii;
        std::string fileName = dir+"dumbbell-" + std::to_string(i+2) + ".cwnd";
        Ptr<OutputStreamWrapper> stream = ascii.CreateFileStream (fileName);
        cwnd_streams.push_back(stream);
        // cwnd.push_back(i);
    }
}

int
main(int argc, char *argv[])
{
    uint32_t n_nodes = 60; // number of nodes on client and server
    uint32_t del_ack_count = 2;
    uint32_t cleanup_time = 2;
    uint32_t initial_cwnd = 10;
    uint32_t bytes_to_send = 100 * 1e6; // 40 MB
    std::string tcp_type_id = "ns3::TcpLinuxReno";// TcpNewReno
    std::string queue_disc = "ns3::FifoQueueDisc";
    std::string queue_size = "500p";
    std::string RTT = "198ms";   		//round-trip time of each TCP flow
    std::string bottleneck_bandwidth = "100Mbps";  //bandwidth of the bottleneck link
    std::string bottleneck_delay = "1ms";          //bottleneck link has negligible propagation delay
    std::string access_bandwidth = "2Mbps";
    std::string root_dir;
    std::string qsize_trace_filename = "qsizeTrace-dumbbell";;
    std::string dropped_trace_filename = "droppedPacketTrace-dumbbell";
    std::string bottleneck_tx_filename = "bottleneckTx-dumbbell";
    float stop_time = 300;
    float start_time = 0;
    float start_tracing_time = 10;
    bool enable_bot_trace = true;

    CommandLine cmd (__FILE__);
    // cmd.AddValue ("n_nodes", "Number of nodes in right and left", n_nodes);
    // cmd.AddValue ("del_ack_count", "del Ack Count", del_ack_count);
    // cmd.AddValue ("cleanup_time", "Clean up time before simulation ends", cleanup_time);
    // cmd.AddValue ("initial_cwnd", "Initial cwnd Size", initial_cwnd);
    // cmd.AddValue ("bytes_to_send", "Bytes to send using BulkSend", bytes_to_send);
    // //cmd.AddValue ("tcp_type_id", "Flavour of TCP to use", tcp_type_id);
    // cmd.AddValue ("queue_disc", "queue Discipline to use", queue_disc);
    // cmd.AddValue ("queue_size", "Queue size at router", queue_size);
    // cmd.AddValue ("RTT", "Round Trip Time for a packet", RTT);
    // cmd.AddValue ("bottleneck_bandwidth", "Bandwidth of the bottleneck link", bottleneck_bandwidth);
    // cmd.AddValue ("bottleneck_delay", "Delay of Bandwidth Link", bottleneck_delay);
    // cmd.AddValue ("access_bandwidth", "Bandwidth of the branches", access_bandwidth);
    // cmd.AddValue ("root_dir", "Root Directory of Project", root_dir);
    // cmd.AddValue ("qsize_trace_filename", "FileName to store qsize trace", qsize_trace_filename);
    // cmd.AddValue ("dropped_trace_filename", "FileName to store dropped packets", dropped_trace_filename);
    // cmd.AddValue ("bottleneck_tx_filename", "FileName to store bottlneck tra", bottleneck_tx_filename);
    // cmd.AddValue ("stop_time", "Simulation stop time", stop_time);
    // cmd.AddValue ("start_time", "Simulation Start Time", start_time);
    // cmd.AddValue ("start_tracing_time", "Time to wait before tracing", start_tracing_time);
    // cmd.AddValue ("enable_bot_trace", "Enable Tracing for whole simulation", enable_bot_trace);
    cmd.Parse (argc, argv);
    
    Config::SetDefault ("ns3::TcpL4Protocol::SocketType", StringValue (tcp_type_id));
    // Config::SetDefault ("ns3::TcpSocket::SndBufSize", UintegerValue (4194304));
    // Config::SetDefault ("ns3::TcpSocket::RcvBufSize", UintegerValue (6291456));
    Config::SetDefault ("ns3::TcpSocket::InitialCwnd", UintegerValue (initial_cwnd));
    Config::SetDefault ("ns3::TcpSocket::DelAckCount", UintegerValue (del_ack_count));
    Config::SetDefault ("ns3::TcpSocket::SegmentSize", UintegerValue (segmentSize));
    // Config::SetDefault ("ns3::DropTailQueue<Packet>::MaxSize", QueueSizeValue (QueueSize ("1p")));
   // Config::SetDefault (queue_disc + "::MaxSize", QueueSizeValue (QueueSize (queue_size)));
    Config::SetDefault("ns3::TcpSocketBase::MaxWindowSize", UintegerValue (20*1000));

    NS_LOG_UNCOND("Pass");
    // Print all values to std::cout
    // std::cout << "Configuration Values:" << std::endl;
    // std::cout << "n_nodes: " << n_nodes << std::endl;
    // std::cout << "del_ack_count: " << del_ack_count << std::endl;
    // std::cout << "cleanup_time: " << cleanup_time << " seconds" << std::endl;
    // std::cout << "initial_cwnd: " << initial_cwnd << std::endl;
    // std::cout << "bytes_to_send: " << bytes_to_send << " bytes" << std::endl;
    // std::cout << "tcp_type_id: " << tcp_type_id << std::endl;
    // std::cout << "queue_disc: " << queue_disc << std::endl;
    // std::cout << "queue_size: " << queue_size << std::endl;
    // std::cout << "RTT: " << RTT << std::endl;
    // std::cout << "bottleneck_bandwidth: " << bottleneck_bandwidth << std::endl;
    // std::cout << "bottleneck_delay: " << bottleneck_delay << std::endl;
    // std::cout << "access_bandwidth: " << access_bandwidth << std::endl;
    // std::cout << "root_dir: " << root_dir << std::endl;
    // std::cout << "qsize_trace_filename: " << qsize_trace_filename << std::endl;
    // std::cout << "dropped_trace_filename: " << dropped_trace_filename << std::endl;
    // std::cout << "bottleneck_tx_filename: " << bottleneck_tx_filename << std::endl;
    // std::cout << "stop_time: " << stop_time << " seconds" << std::endl;
    // std::cout << "start_time: " << start_time << " seconds" << std::endl;
    // std::cout << "start_tracing_time: " << start_tracing_time << " seconds" << std::endl;
    // std::cout << "enable_bot_trace: " << (enable_bot_trace ? "true" : "false") << std::endl;
    // return 0;

    // two for router and n_nodes on left and right of bottleneck
    NodeContainer nodes;
    nodes.Create (2+n_nodes*2);
    // Source nodes
    NodeContainer leftNodes [n_nodes];
    // Destination nodes
    NodeContainer rightNodes [n_nodes];
    NodeContainer r1r2 = NodeContainer(nodes.Get(0), nodes.Get(1));
    for( uint32_t i = 0; i< n_nodes ; i++){
        leftNodes[i] = NodeContainer(nodes.Get(i+2), nodes.Get(0));
        rightNodes[i] = NodeContainer(nodes.Get(2+n_nodes+i), nodes.Get(1));
    }

    // creating channel
    // Defining the links to be used between nodes
    double min = 0.0;
    double max = double(2*std::stoi(RTT.substr(0, RTT.length()-2)));
    
    Ptr<UniformRandomVariable> x = CreateObject<UniformRandomVariable> ();
    x->SetAttribute ("Min", DoubleValue (min));
    x->SetAttribute ("Max", DoubleValue (max));

    PointToPointHelper p2p_router;
    p2p_router.SetDeviceAttribute ("DataRate", StringValue ("100Mbps"));
    p2p_router.SetChannelAttribute ("Delay", StringValue ("1ms"));
    p2p_router.SetQueue ("ns3::DropTailQueue<Packet>", "MaxSize", QueueSizeValue (QueueSize (queue_size)));
    p2p_router.DisableFlowControl();

    
    PointToPointHelper p2p_s[n_nodes], p2p_d[n_nodes];
    for (uint32_t i = 0; i < n_nodes; i++)
    {
        double delay = (x->GetValue())/2;
        //std::cout << delay*2 << std::endl;
        std::string delay_str = std::to_string(delay) + "ms";
        p2p_s[i].SetDeviceAttribute ("DataRate", StringValue(access_bandwidth));
        p2p_s[i].SetChannelAttribute ("Delay", StringValue(delay_str));
        p2p_s[i].SetQueue ("ns3::DropTailQueue<Packet>", "MaxSize", QueueSizeValue (QueueSize (std::to_string(0/n_nodes)+"p"))); // p in 1000p stands for packets
        p2p_s[i].DisableFlowControl();
        
        p2p_d[i].SetDeviceAttribute ("DataRate", StringValue(access_bandwidth));
        p2p_d[i].SetChannelAttribute ("Delay", StringValue(delay_str));
        p2p_d[i].SetQueue ("ns3::DropTailQueue<Packet>", "MaxSize", QueueSizeValue (QueueSize (std::to_string(0/n_nodes)+"p"))); // p in 1000p stands for packets
        p2p_d[i].DisableFlowControl();
    }

    NetDeviceContainer r1r2ND = p2p_router.Install(r1r2);

    std::vector<NetDeviceContainer> leftND, rightND;
    for(uint32_t i = 0 ; i < n_nodes; i++){
        leftND.push_back(p2p_s[i].Install(leftNodes[i]));
        rightND.push_back(p2p_d[i].Install(rightNodes[i]));
    }

    // Installing internet stack
    InternetStackHelper stack;
    stack.InstallAll(); // install internet stack on all nodes

    // Giving IP Address to each node
    Ipv4AddressHelper ipv4;
    ipv4.SetBase("172.16.1.0", "255.255.255.0");

    Ipv4InterfaceContainer r1r2Ip =  ipv4.Assign(r1r2ND);

    std::vector<Ipv4InterfaceContainer> lIp, rIp; 
    for(uint32_t i = 0 ; i < n_nodes; i ++){
        std::string ip = "10.1."+std::to_string(i)+".0";
        ipv4.SetBase(ip.c_str(), "255.255.255.0");
        lIp.push_back(ipv4.Assign(leftND[i]));

        std::string ip2 = "10.1."+std::to_string(i+n_nodes)+".0";
        ipv4.SetBase(ip2.c_str(), "255.255.255.0");
        rIp.push_back(ipv4.Assign(rightND[i]));

    }

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // Attack sink to all nodes
    uint16_t port = 50000;
    PacketSinkHelper packetSinkHelper ("ns3::TcpSocketFactory", InetSocketAddress (Ipv4Address::GetAny(), port));
    Address sinkAddress[n_nodes];
    ApplicationContainer sinkApp[n_nodes];
    
    for(uint32_t i = 0 ; i < n_nodes; i++){
        sinkAddress[i] = *(new Address(InetSocketAddress(rIp[i].GetAddress(0), port)));
        sinkApp[i] = packetSinkHelper.Install(nodes.Get(2 + n_nodes + i));
        sinkApp[i].Start(Seconds(start_time));
        sinkApp[i].Stop(Seconds(stop_time));
    }

    // Installing BulkSend on each node on left
    Ptr<Socket> ns3TcpSocket[n_nodes];
    ApplicationContainer sourceApps[n_nodes];

    double mean = 0.1;   // more like a ~ 0.06
    double bound = 1;
    Ptr<ExponentialRandomVariable> expRandomVariable = CreateObject<ExponentialRandomVariable> ();
    expRandomVariable->SetAttribute ("Mean", DoubleValue (mean));
    expRandomVariable->SetAttribute ("Bound", DoubleValue (bound));

    double stime = start_time;
    // Configuring the application at each source node.
    for (uint32_t i = 0; i < n_nodes; i++)
    {
        BulkSendHelper tmp_source("ns3::TcpSocketFactory",InetSocketAddress (rIp[i].GetAddress (0), port));
           
        // Set the amount of data to send in bytes.  Zero is unlimited.
        tmp_source.SetAttribute ("MaxBytes", UintegerValue (bytes_to_send));
        sourceApps[i] = tmp_source.Install (nodes.Get (2 + i));
        
        sourceApps[i].Start (Seconds (stime));
        sourceApps[i].Stop (Seconds (stop_time));
        double gap = expRandomVariable->GetValue();

        stime += gap;        
    }

    // creating a directory to save results
    struct stat buffer;    
    [[maybe_unused]] int retVal;

    if ((stat (dir.c_str (), &buffer)) == 0)
    {
      std::string dirToRemove = "rm -rf " + dir;
      retVal = system (dirToRemove.c_str ());
      NS_ASSERT_MSG (retVal == 0, "Error in return value");
    }
    std::string dirToSave = "mkdir -p " + dir;
    retVal = system(dirToSave.c_str ());
    NS_ASSERT_MSG (retVal == 0, "Error in return value");

 // Configuring file stream to write the Qsize
    AsciiTraceHelper ascii_qsize;
    qSize_stream = ascii_qsize.CreateFileStream(dir+qsize_trace_filename+".txt");

    // Configuring file stream to write the no of packets transmitted by the bottleneck
    AsciiTraceHelper ascii_qsize_tx;
    bottleneckTransimittedStream = ascii_qsize_tx.CreateFileStream(dir+bottleneck_tx_filename+".txt");
    AsciiTraceHelper ascii_dropped;
    dropped_stream = ascii_dropped.CreateFileStream (dir+dropped_trace_filename + ".txt");
    // start tracing the congestion window size and qSize

    Simulator::Schedule( Seconds(stime), &start_tracing_timeCwnd, n_nodes);
    Simulator::Schedule( Seconds(stime), &StartTracingQueueSize);
    Simulator::Schedule( Seconds(stime), &StartTracingTransmitedPacket);
    Simulator::Schedule( Seconds(stime+start_tracing_time), &writeCwndToFile, n_nodes);

    // start tracing Queue Size and Dropped Files
    Simulator::Schedule( Seconds(stime), &TraceDroppedPacket, dropped_trace_filename);
    // writing the congestion windows size, queue_size, packetTx to files periodically ( 1 sec. )
    for (auto time = stime+start_tracing_time; time < stop_time; time+=0.1)
    {   
        // Simulator::Schedule( Seconds(time), &writeCwndToFile, n_nodes);
        Simulator::Schedule( Seconds(time), &TraceQueueSize);
        Simulator::Schedule( Seconds(time), &TraceBottleneckTx);
        Simulator::Schedule( Seconds(time), &TraceDroppedPkts);
    }
    
    if ( enable_bot_trace == 1 ){
        AsciiTraceHelper bottleneck_ascii;
        p2p_router.EnableAscii(bottleneck_ascii.CreateFileStream (dir+"bottleneck-trace-router0.tr"), leftND[0]);
    }

    // Check for dropped packets using Flow Monitor
    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll ();

    Simulator::Stop (Seconds (stop_time+cleanup_time));
    Simulator::Run ();
    monitor->SerializeToXmlFile(dir+"dumbbell-flowmonitor.xml", false, true);
    Simulator::Destroy ();

    return 0;
}


