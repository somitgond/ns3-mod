/*
Single bottleneck dumbbell network
*/
#include "ns3/applications-module.h"
#include "ns3/config-store-module.h"
#include "ns3/core-module.h"
#include "ns3/enum.h"
#include "ns3/error-model.h"
#include "ns3/event-id.h"
#include "ns3/flow-monitor-helper.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/netanim-module.h"
#include "ns3/network-module.h"
#include "ns3/node.h"
#include "ns3/point-to-point-module.h"
#include "ns3/tcp-header.h"
#include "ns3/traffic-control-module.h"
#include "ns3/udp-header.h"
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>

#define MAX_SOURCES 100;

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("TCPSCRIPT");

std::string dir = "tcp-dumbbell-regular/";
uint32_t prev = 0;
Time prevTime = Seconds(0);
uint32_t segmentSize = 1400;
uint32_t nNodes = 60; // number of nodes on client and server

std::vector<uint32_t>
    cwndChanges(nNodes + 1, 0); // keep track of latest cwnd value of client i

// to store parameters
Ptr<OutputStreamWrapper> parameters;

// std::vector<uint32_t> cwnd;
std::vector<Ptr<OutputStreamWrapper>> cwnd_streams; // file stream of cwnd

uint64_t queueSize;
Ptr<OutputStreamWrapper> qSize_stream;

uint64_t bottleneckTransimitted;
Ptr<OutputStreamWrapper> bottleneckTransimittedStream;

uint64_t droppedPackets;
Ptr<OutputStreamWrapper> dropped_stream;

static void plotQsizeChange(uint32_t oldQSize, uint32_t newQSize) {
    // NS_LOG_UNCOND(Simulator::Now().GetSeconds() << "\t" << newCwnd);
    queueSize = newQSize;
}

static void RxDrop(Ptr<OutputStreamWrapper> stream, Ptr<const Packet> p) {
    // std::cout << "Packet Dropped (finally!)" << std::endl;
    //*stream->GetStream () << Simulator::Now().GetSeconds() << "\tRxDrop" <<
    // std::endl;
    droppedPackets++;
}

static void TxPacket(Ptr<const Packet> p) { bottleneckTransimitted++; }

static void TraceDroppedPacket(std::string droppedTrFileName) {
    // tracing all the dropped packets in a seperate file
    Config::ConnectWithoutContext(
        "/NodeList/*/DeviceList/*/$ns3::PointToPointNetDevice/TxQueue/Drop",
        MakeBoundCallback(&RxDrop, dropped_stream));
    Config::ConnectWithoutContext(
        "/NodeList/*/DeviceList/*/$ns3::PointToPointNetDevice/MacTxDrop",
        MakeBoundCallback(&RxDrop, dropped_stream));
    Config::ConnectWithoutContext(
        "/NodeList/*/DeviceList/*/$ns3::PointToPointNetDevice/PhyRxDrop",
        MakeBoundCallback(&RxDrop, dropped_stream));
    Config::ConnectWithoutContext(
        "/NodeList/*/DeviceList/*/$ns3::PointToPointNetDevice/PhyTxDrop",
        MakeBoundCallback(&RxDrop, dropped_stream));
    // Config::ConnectWithoutContext("/NodeList/*/DeviceList/*/$ns3::PointToPointNetDevice/TcDrop",
    // MakeBoundCallback(&RxDrop, dropped_stream));
}

static void TraceQueueSize() {
    *qSize_stream->GetStream()
        << Simulator::Now().GetSeconds() << "\t" << queueSize << std::endl;
}

static void TraceDroppedPkts() {
    *dropped_stream->GetStream()
        << Simulator::Now().GetSeconds() << "\t" << droppedPackets << std::endl;
}

static void TraceBottleneckTx() {
    *bottleneckTransimittedStream->GetStream()
        << Simulator::Now().GetSeconds() << "\t" << bottleneckTransimitted
        << std::endl;
}

static void StartTracingQueueSize() {
    Config::ConnectWithoutContext(
        "/NodeList/0/DeviceList/0/$ns3::PointToPointNetDevice/TxQueue/"
        "PacketsInQueue",
        MakeCallback(&plotQsizeChange));
}

static void StartTracingTransmitedPacket() {
    bottleneckTransimitted = 0;
    Config::ConnectWithoutContext(
        "/NodeList/0/DeviceList/0/$ns3::PointToPointNetDevice/PhyTxEnd",
        MakeCallback(&TxPacket));
}

// Trace congestion window
static void CwndTracer(uint32_t nodeNumber, uint32_t oldval, uint32_t newval) {
    // NS_LOG_UNCOND(Simulator::Now ().GetSeconds () << "\t"<<oldval<<" " <<
    // newval);
    cwndChanges[nodeNumber] = newval / segmentSize;
    // *stream->GetStream () << Simulator::Now().GetSeconds () << " " <<
    // newval/segmentSize<< std::endl;
}

// Update values as cwndChanges
static void updateCwndValues(uint32_t nNodes) {
    for (uint32_t i = 0; i < nNodes; i++) {
        std::string path = "/NodeList/" + std::to_string(i + 2) +
                           "/$ns3::TcpL4Protocol/SocketList/0/CongestionWindow";
        Config::ConnectWithoutContext(path, MakeBoundCallback(&CwndTracer, i));
    }
}

// Write to congestion window streams
static void writeCwndToFile(uint32_t nNodes) {
    for (uint32_t i = 0; i < nNodes; i++) {
        auto val = cwndChanges[i];
        *cwnd_streams[i]->GetStream()
            << Simulator::Now().GetSeconds() << " " << val << std::endl;
    }
}

// initialize tracing cwnd streams
static void startTracingCwnd(uint32_t nNodes) {
    for (uint32_t i = 0; i < nNodes; i++) {
        AsciiTraceHelper ascii;
        std::string fileName =
            dir + "dumbbell-" + std::to_string(i + 2) + ".cwnd";
        Ptr<OutputStreamWrapper> stream = ascii.CreateFileStream(fileName);
        cwnd_streams.push_back(stream);
        // cwnd.push_back(i);
    }
}

int main(int argc, char *argv[]) {
    uint32_t delAckCount = 2;
    uint32_t cleanup_time = 2;
    uint32_t initialCwnd = 10;
    // uint32_t bytesToSend = 100 * 1e6; // 100 MB
    std::string tcpTypeId = "ns3::TcpLinuxReno"; // TcpNewReno
    std::string queueDisc = "ns3::FifoQueueDisc";
    std::string queueSize = "500p";
    std::string RTT = "198ms"; // round-trip time of each TCP flow
    std::string bottleneckBandwidth =
        "100Mbps"; // bandwidth of the bottleneck link
    std::string bottleneckDelay =
        "1ms"; // bottleneck link has negligible propagation delay
    std::string accessBandwidth = "2Mbps";
    std::string root_dir;
    std::string qsizeTrFileName = "qsizeTrace-dumbbell";
    ;
    std::string droppedTrFileName = "droppedPacketTrace-dumbbell";
    std::string bottleneckTxFileName = "bottleneckTx-dumbbell";
    std::string parametersFileName = "parameters";
    float stopTime = 500;
    float startTime = 0;
    float startTracing = 10;
    bool enbBotTrace = 0;

    CommandLine cmd(__FILE__);
    cmd.AddValue("nNodes", "Number of nodes in right and left", nNodes);
    cmd.AddValue("tcp_type_id", "Flavor of TCP to use", tcpTypeId);
    cmd.AddValue("stopTime", "Simulation stop time", stopTime);
    cmd.Parse(argc, argv);

    Config::SetDefault("ns3::TcpL4Protocol::SocketType",
                       StringValue(tcpTypeId));
    // Config::SetDefault ("ns3::TcpSocket::SndBufSize", UintegerValue
    // (4194304)); Config::SetDefault ("ns3::TcpSocket::RcvBufSize",
    // UintegerValue (6291456));
    Config::SetDefault("ns3::TcpSocket::InitialCwnd",
                       UintegerValue(initialCwnd));
    Config::SetDefault("ns3::TcpSocket::DelAckCount",
                       UintegerValue(delAckCount));
    Config::SetDefault("ns3::TcpSocket::SegmentSize",
                       UintegerValue(segmentSize));
    // Config::SetDefault ("ns3::DropTailQueue<Packet>::MaxSize", QueueSizeValue
    // (QueueSize ("1p")));
    Config::SetDefault(queueDisc + "::MaxSize",
                       QueueSizeValue(QueueSize(queueSize)));
    Config::SetDefault("ns3::TcpSocketBase::MaxWindowSize",
                       UintegerValue(20 * 1000));

    NS_LOG_UNCOND("TCP variant: " << tcpTypeId);

    // two for router and nNodes on left and right of bottleneck
    NodeContainer nodes;
    nodes.Create(2 + nNodes * 2);
    // Source nodes
    NodeContainer leftNodes[nNodes];
    // Destination nodes
    NodeContainer rightNodes[nNodes];
    NodeContainer r1r2 = NodeContainer(nodes.Get(0), nodes.Get(1));
    for (uint32_t i = 0; i < nNodes; i++) {
        leftNodes[i] = NodeContainer(nodes.Get(i + 2), nodes.Get(0));
        rightNodes[i] = NodeContainer(nodes.Get(2 + nNodes + i), nodes.Get(1));
    }

    // creating channel
    // Defining the links to be used between nodes
    double min = 0.0;
    double max = double(2 * std::stoi(RTT.substr(0, RTT.length() - 2)));

    Ptr<UniformRandomVariable> x = CreateObject<UniformRandomVariable>();
    x->SetAttribute("Min", DoubleValue(min));
    x->SetAttribute("Max", DoubleValue(max));

    PointToPointHelper p2p_router;
    p2p_router.SetDeviceAttribute("DataRate", StringValue(bottleneckBandwidth));
    p2p_router.SetChannelAttribute("Delay", StringValue(bottleneckDelay));
    p2p_router.SetQueue(
        "ns3::DropTailQueue<Packet>", "MaxSize",
        QueueSizeValue(QueueSize(queueSize))); // p in 1000p stands for packets
    p2p_router.DisableFlowControl();

    PointToPointHelper p2p_s[nNodes], p2p_d[nNodes];
    for (uint32_t i = 0; i < nNodes; i++) {
        double delay = (x->GetValue()) / 2;
        // std::cout << delay*2 << std::endl;
        std::string delay_str = std::to_string(delay) + "ms";
        p2p_s[i].SetDeviceAttribute("DataRate", StringValue(accessBandwidth));
        p2p_s[i].SetChannelAttribute("Delay", StringValue(delay_str));
        p2p_s[i].SetQueue(
            "ns3::DropTailQueue<Packet>", "MaxSize",
            QueueSizeValue(QueueSize(std::to_string(0 / nNodes) +
                                     "p"))); // p in 1000p stands for packets
        p2p_s[i].DisableFlowControl();

        p2p_d[i].SetDeviceAttribute("DataRate", StringValue(accessBandwidth));
        p2p_d[i].SetChannelAttribute("Delay", StringValue(delay_str));
        p2p_d[i].SetQueue(
            "ns3::DropTailQueue<Packet>", "MaxSize",
            QueueSizeValue(QueueSize(std::to_string(0 / nNodes) +
                                     "p"))); // p in 1000p stands for packets
        p2p_d[i].DisableFlowControl();
    }

    NetDeviceContainer r1r2ND = p2p_router.Install(r1r2);

    std::vector<NetDeviceContainer> leftND, rightND;
    for (uint32_t i = 0; i < nNodes; i++) {
        leftND.push_back(p2p_s[i].Install(leftNodes[i]));
        rightND.push_back(p2p_d[i].Install(rightNodes[i]));
    }

    // Installing internet stack
    InternetStackHelper stack;
    stack.InstallAll(); // install internet stack on all nodes

    // Giving IP Address to each node
    Ipv4AddressHelper ipv4;
    ipv4.SetBase("172.16.1.0", "255.255.255.0");

    Ipv4InterfaceContainer r1r2Ip = ipv4.Assign(r1r2ND);

    std::vector<Ipv4InterfaceContainer> lIp, rIp;
    for (uint32_t i = 0; i < nNodes; i++) {
        std::string ip = "10.1." + std::to_string(i) + ".0";
        ipv4.SetBase(ip.c_str(), "255.255.255.0");
        lIp.push_back(ipv4.Assign(leftND[i]));

        std::string ip2 = "10.1." + std::to_string(i + nNodes) + ".0";
        ipv4.SetBase(ip2.c_str(), "255.255.255.0");
        rIp.push_back(ipv4.Assign(rightND[i]));
    }

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // Attack sink to all nodes
    uint16_t port = 50000;
    PacketSinkHelper packetSinkHelper(
        "ns3::TcpSocketFactory",
        InetSocketAddress(Ipv4Address::GetAny(), port));
    Address sinkAddress[nNodes];
    ApplicationContainer sinkApp[nNodes];

    for (uint32_t i = 0; i < nNodes; i++) {
        sinkAddress[i] =
            *(new Address(InetSocketAddress(rIp[i].GetAddress(0), port)));
        sinkApp[i] = packetSinkHelper.Install(nodes.Get(2 + nNodes + i));
        sinkApp[i].Start(Seconds(startTime));
        sinkApp[i].Stop(Seconds(stopTime));
    }

    // Installing BulkSend on each node on left
    Ptr<Socket> ns3TcpSocket[nNodes];
    ApplicationContainer sourceApps[nNodes];

    double mean = 0.1; // more like a ~ 0.06
    double bound = 1;
    Ptr<ExponentialRandomVariable> expRandomVariable =
        CreateObject<ExponentialRandomVariable>();
    expRandomVariable->SetAttribute("Mean", DoubleValue(mean));
    expRandomVariable->SetAttribute("Bound", DoubleValue(bound));

    double stime = startTime;
    // Configuring the application at each source node.
    for (uint32_t i = 0; i < nNodes; i++) {
        BulkSendHelper tmp_source(
            "ns3::TcpSocketFactory",
            InetSocketAddress(rIp[i].GetAddress(0), port));

        // Set the amount of data to send in bytes.  Zero is unlimited.
        tmp_source.SetAttribute("MaxBytes", UintegerValue(0));
        sourceApps[i] = tmp_source.Install(nodes.Get(2 + i));

        sourceApps[i].Start(Seconds(stime));
        sourceApps[i].Stop(Seconds(stopTime));
        double gap = expRandomVariable->GetValue();

        stime += gap;
    }

    // creating a directory to save results
    struct stat buffer;
    [[maybe_unused]] int retVal;

    if ((stat(dir.c_str(), &buffer)) == 0) {
        std::string dirToRemove = "rm -rf " + dir;
        retVal = system(dirToRemove.c_str());
        NS_ASSERT_MSG(retVal == 0, "Error in return value");
    }
    std::string dirToSave = "mkdir -p " + dir;
    retVal = system(dirToSave.c_str());
    NS_ASSERT_MSG(retVal == 0, "Error in return value");

    // write parameters
    AsciiTraceHelper parameters_helper;

    parameters =
        parameters_helper.CreateFileStream(dir + parametersFileName + ".txt");
    *parameters->GetStream() << "regular sampling of cwnd." << std::endl;
    *parameters->GetStream() << "Nodes : " << "\t" << nNodes << std::endl;
    *parameters->GetStream()
        << "TCP type id: " << "\t" << tcpTypeId << std::endl;
    *parameters->GetStream() << "RTT : " << "\t" << RTT << std::endl;
    *parameters->GetStream()
        << "Bottleneck Delay: " << "\t" << bottleneckDelay << std::endl;
    *parameters->GetStream()
        << "Bottleneck Bandwidth: " << "\t" << bottleneckBandwidth << std::endl;
    *parameters->GetStream()
        << "Queue Disc: " << "\t" << queueDisc << std::endl;
    *parameters->GetStream()
        << "Queue Size: " << "\t" << queueSize << std::endl;
    *parameters->GetStream()
        << "Simulation Stop time: " << "\t" << stopTime << std::endl;

    // Configuring file stream to write the Qsize
    AsciiTraceHelper ascii_qsize;
    qSize_stream = ascii_qsize.CreateFileStream(dir + qsizeTrFileName + ".txt");

    // Configuring file stream to write the no of packets transmitted by the
    // bottleneck
    AsciiTraceHelper ascii_qsize_tx;
    bottleneckTransimittedStream =
        ascii_qsize_tx.CreateFileStream(dir + bottleneckTxFileName + ".txt");
    AsciiTraceHelper ascii_dropped;
    dropped_stream =
        ascii_dropped.CreateFileStream(dir + droppedTrFileName + ".txt");
    // start tracing the congestion window size and qSize

    Simulator::Schedule(Seconds(stime), &startTracingCwnd, nNodes);
    Simulator::Schedule(Seconds(stime), &StartTracingQueueSize);
    Simulator::Schedule(Seconds(stime), &StartTracingTransmitedPacket);
    Simulator::Schedule(Seconds(stime), &updateCwndValues, nNodes);

    // start tracing Queue Size and Dropped Files
    Simulator::Schedule(Seconds(stime), &TraceDroppedPacket, droppedTrFileName);
    // writing the congestion windows size, queueSize, packetTx to files
    // periodically ( 1 sec. )
    //    vector<string> qsizeRandom = {"30p", "60p", "110p"};
    for (auto time = stime + startTracing; time < stopTime; time += 0.1) {
        //        Config::SetDefault (queueDisc + "::MaxSize", QueueSizeValue
        //        (QueueSize (queueSize)));
        Simulator::Schedule(Seconds(time), &writeCwndToFile, nNodes);
        Simulator::Schedule(Seconds(time), &TraceQueueSize);
        Simulator::Schedule(Seconds(time), &TraceBottleneckTx);
        Simulator::Schedule(Seconds(time), &TraceDroppedPkts);
    }

    if (enbBotTrace == 1) {
        AsciiTraceHelper bottleneck_ascii;
        p2p_router.EnableAscii(bottleneck_ascii.CreateFileStream(
                                   dir + "bottleneck-trace-router0.tr"),
                               leftND[0]);
    }

    // Check for dropped packets using Flow Monitor
    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll();

    Simulator::Stop(Seconds(stopTime + cleanup_time));
    Simulator::Run();
    monitor->SerializeToXmlFile(dir + "dumbbell-flowmonitor.xml", false, true);
    Simulator::Destroy();

    return 0;
}
