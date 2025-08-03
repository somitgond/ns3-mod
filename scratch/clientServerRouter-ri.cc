/*
Single bottleneck dumbbell network
Active Queue Management using variable maxSize
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

#define MAX_SOURCES 100
#define BETA_VALUE 0.5
#define GLOBAL_SYNC_THRESHOLD 0.2

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("TCPSCRIPT");

std::string dir = "result-clientServerRouter/";
uint32_t prev = 0;
Time prevTime = Seconds(0);
uint32_t segmentSize = 1400;
double segSize = segmentSize;
uint32_t threshold = 10;
uint32_t increment = 100;
uint32_t nNodes = 60;

// to store RTT of each flow
Ptr<OutputStreamWrapper> rtts;

// to store parameters
Ptr<OutputStreamWrapper> parameters;

std::vector<uint32_t> cwnd(nNodes + 1, 0);
std::vector<Ptr<OutputStreamWrapper>> cwnd_streams;

uint64_t queue_size;
Ptr<OutputStreamWrapper> qSize_stream;
Ptr<OutputStreamWrapper> tc_qSize_stream;

uint64_t bottleneckTransimitted;
Ptr<OutputStreamWrapper> bottleneckTransimittedStream;

uint64_t droppedPackets;
Ptr<OutputStreamWrapper> dropped_stream;

// queue disc in router 1
Ptr<QueueDisc> queueDisc_router = CreateObject<FifoQueueDisc>();

/////////////////////////// calculating global sync matrix

// single vector storing loss events
std::vector<uint32_t> loss_events(nNodes, 0);

double give_global_sync() {
    int rate = 0;
    for (int i = 0; i < (int)nNodes; i++)
        if (loss_events[i] == 1)
            rate++;

    return (double)rate / nNodes;
}

//////////////// get qth ///////////////

int giveQth(double w_av, double beta) {
    double capacity = 100; // in mbps
    double pi = 3.141593, c = (capacity * 1000000 / (segSize * 8 * nNodes)),
           tao = 0.5;
    double val = log(pi / 2);

    // std::cout<<val<<std::endl;

    int qth = 0;
    double diff = 100000;
    for (int i = 1; i < 2048; i++) {
        double estimate =
            log(i) + i * (log(w_av / (c * tao))) + log(w_av * beta);
        if (fabs(val - estimate) < diff && estimate <= val) {
            diff = fabs(val - estimate);
            qth = i;
        }
    }

    return qth;
}

//////////////////////////////////////////////

void AdjustQueueSize(Ptr<QueueDisc> queueDisc) {
    QueueSize currentSize = queueDisc->GetMaxSize();
    NS_LOG_UNCOND("Queue MaxsizeSize " << currentSize.GetValue());
    NS_LOG_UNCOND("Queue CurrentSize "
                  << queueDisc->GetCurrentSize().GetValue());
    // if (queueDisc->GetCurrentSize().GetValue() > threshold) {
    QueueSize newSize =
        QueueSize(currentSize.GetUnit(), currentSize.GetValue() + increment);
    queueDisc->SetMaxSize(newSize);
    threshold = (currentSize.GetValue() + increment) / 2;
    NS_LOG_UNCOND("Queue size adjusted to " << newSize);
    // }
}

// set new size
void SetQueueSize(uint32_t qth) {
    QueueSize currentSize = queueDisc_router->GetMaxSize();
    // NS_LOG_UNCOND("Queue MaxsizeSize " << currentSize.GetValue());
    if (currentSize.GetValue() == qth or qth == 0)
        return;
    std::string qth_str = std::to_string(qth) + "p";
    QueueSize newSize = QueueSize(qth_str);
    queueDisc_router->SetMaxSize(newSize);
    // NS_LOG_UNCOND("Queue size adjusted to " << newSize);
}

void PeriodicQueueAdjustment(Ptr<QueueDisc> queueDisc, Time interval) {
    AdjustQueueSize(queueDisc);
    // NS_LOG_UNCOND("Queue size adjusted");
    Simulator::Schedule(interval, &PeriodicQueueAdjustment, queueDisc,
                        interval);
}

void TraceQueueSizeTc(Ptr<QueueDisc> queueDisc) {
    // Trace Queue Size in Traffic Control Layer
    *tc_qSize_stream->GetStream()
        << Simulator::Now().GetSeconds() << " "
        << queueDisc->GetCurrentSize().GetValue() << std::endl;
}

static void plotQsizeChange(uint32_t oldQSize, uint32_t newQSize) {
    // NS_LOG_UNCOND(Simulator::Now().GetSeconds() << "\t" << newCwnd);
    queue_size = newQSize;
}

static void RxDrop(Ptr<OutputStreamWrapper> stream, Ptr<const Packet> p) {
    // std::cout << "Packet Dropped (finally!)" << std::endl;
    //*stream->GetStream () << Simulator::Now().GetSeconds() << "\tRxDrop" <<
    // std::endl;
    droppedPackets++;
}

static void TxPacket(Ptr<const Packet> p) { bottleneckTransimitted++; }

static void TraceDroppedPacket(std::string dropped_trace_filename) {
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
        << Simulator::Now().GetSeconds() << "\t" << queue_size << std::endl;
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

void BytesInQueueTrace(Ptr<OutputStreamWrapper> stream, uint32_t oldVal,
                       uint32_t newVal) {
    *stream->GetStream() << Simulator::Now().GetSeconds() << " "
                         << newVal / segmentSize << std::endl;
}

static void StartTracingQueueSize() {
    // trace Queue size in pointTopointNetDevice
    Config::ConnectWithoutContext(
        "/NodeList/0/DeviceList/0/$ns3::PointToPointNetDevice/TxQueue/"
        "PacketsInQueue",
        MakeCallback(&plotQsizeChange));

    // Trace Queue size in trafficcontrol Layer
    // Config::ConnectWithoutContext("/NodeList/0/DeviceList/0/$ns3::PointToPointNetDevice/TxQueue/PacketsInQueue",
    // MakeCallback(&plotQsizeChange));
}

static void StartTracingTransmitedPacket() {
    bottleneckTransimitted = 0;
    Config::ConnectWithoutContext(
        "/NodeList/0/DeviceList/0/$ns3::PointToPointNetDevice/PhyTxEnd",
        MakeCallback(&TxPacket));
}

double minB = 100.0, maxB = -100.0;

//////////// CALCULATNG BETA /////////////////
bool hasSynchrony = false;
std::vector<bool> gotDip;
bool gotAll = false;
int cntDips = 0;

std::vector<double> prevWindow;

std::vector<double> prevSumWindows;
double sumWindows = 0.0;

std::vector<double> wti2;
double sum_wti2 = 0.0;

std::vector<double> arrWti;
double sum_wti = 0.0;

std::vector<double> wiwti;
double sum_wiwti = 0.0;

std::vector<double> biwiwti;
double sum_biwiwti = 0.0;
/// ////////////
std::vector<double> biwi;
double sum_biwi = 0.0;
std::vector<double> prevOldVal;
double sumPrevOldVal = 0.0;
void initiateArray() {
    gotDip = std::vector<bool>(nNodes + 1, false);
    prevWindow = std::vector<double>(nNodes + 1, 0.0);
    prevSumWindows = std::vector<double>(nNodes + 1, 0.0);
    wti2 = std::vector<double>(nNodes + 1, 0.0);
    arrWti = std::vector<double>(nNodes + 1, 0.0);
    wiwti = std::vector<double>(nNodes + 1, 0.0);
    biwiwti = std::vector<double>(nNodes + 1, 0.0);
    biwi = std::vector<double>(nNodes + 1, 0.0);
    prevOldVal = std::vector<double>(nNodes + 1, 0.0);
}

// static void getDipOfHost(int node, double biwi, double wti, double wi){
//     sum_wti2 += (wti*wti - wti2[node]); wti2[node] = wti*wti;
//     sum_wti += (wti - arrWti[node]); arrWti[node] = wti;
//     sum_wiwti += (wi*wti - wiwti[node]); wiwti[node] = wi*wti;
//     sum_biwiwti += (biwi*wti - biwiwti[node]); biwiwti[node] = biwi*wti;

//     if(!gotDip[node]){
//         gotDip[node] = true;
//         cntDips++;
//     }
//     if(cntDips == nNodes) gotAll = true;
// }

///// getBeta returns the beta_optimal at anytime.
double getBeta() {
    if (!sum_wti)
        return -1;
    return (sum_wti2 - sum_wiwti + sum_biwiwti) / sum_wti;
}

///////////////////////////////////////////////////////////////////////////////

// Trace congestion window
static void CwndTracer(uint32_t node, uint32_t oldval, uint32_t newval) {

    double newVal = newval / segSize;
    double diff = newVal - prevWindow[node];
    // update loss events
    if (newval < oldval) {
        loss_events[node] = 1;
    } else {
        loss_events[node] = 0;
    }
    // get global sync rate if it is greater than a parameter
    if (give_global_sync() > GLOBAL_SYNC_THRESHOLD) {
        // NS_LOG_UNCOND("global sync rate: "<<give_global_sync());
        //  set appropriate qth
        hasSynchrony = true;
    }

    // NS_LOG_UNCOND("Old and New :"<< oldVal<<" "<<newVal);
    sumWindows += diff;
    if (newval < oldval) {
        sum_biwi -= (diff - biwi[node]);
        biwi[node] = diff;
        sumPrevOldVal += (prevWindow[node] - prevOldVal[node]);
        prevOldVal[node] = prevWindow[node];
        if (sumPrevOldVal) {
            double beta = sum_biwi / sumPrevOldVal;
            minB = std::min(minB, beta);
            maxB = std::max(maxB, beta);
            // NS_LOG_UNCOND("min and max beta value: "<< minB <<" "<<maxB<<"
            // beta:
            // "<<beta<<" w_av "<<prevSumWindows[node]/nNodes);
            // uint32_t qth_n = giveQth(prevSumWindows[node] / nNodes, BETA_VALUE);
            // NS_LOG_UNCOND("qth value: " << qth_n);
            // SetQueueSize(qth_n);
        }
    }

    if (hasSynchrony) {
        // NS_LOG_UNCOND("qth value In synchrony: "<<
        // giveQth(prevSumWindows[node]/nNodes, 0.5));
        hasSynchrony = false;
    }

    prevSumWindows[node] = sumWindows;
    prevWindow[node] = newVal;

    cwnd[node] = newval / segmentSize;
    //    *cwnd_streams[node]->GetStream() << Simulator::Now ().GetSeconds () <<
    //    " " << newval/segmentSize<< std::endl;
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
        *cwnd_streams[i]->GetStream()
            << Simulator::Now().GetSeconds() << " " << cwnd[i] << std::endl;
    }
}

// initialize tracing cwnd streams
static void start_tracing_timeCwnd(uint32_t nNodes) {
    for (uint32_t i = 0; i < nNodes; i++) {
        AsciiTraceHelper ascii;
        std::string fileName =
            dir + "dumbbell-" + std::to_string(i + 2) + ".cwnd";
        Ptr<OutputStreamWrapper> stream = ascii.CreateFileStream(fileName);
        cwnd_streams.push_back(stream);
    }
}

int main(int argc, char *argv[]) {
    initiateArray();
    uint32_t del_ack_count = 2;
    uint32_t cleanup_time = 2;
    uint32_t initial_cwnd = 10;
    uint32_t bytes_to_send = 0;                    // 40 MB
    std::string tcp_type_id = "ns3::TcpLinuxReno"; // TcpNewReno
    std::string queue_disc = "ns3::FifoQueueDisc";
    std::string queueSize = "1p";
    std::string tc_queueSize = "2083p";
    std::string RTT = "198ms"; // round-trip time of each TCP flow
    std::string bottleneck_bandwidth =
        "100Mbps"; // bandwidth of the bottleneck link
    std::string bottleneck_delay =
        "1ms"; // bottleneck link has negligible propagation delay
    std::string access_bandwidth = "2Mbps";
    std::string root_dir;
    std::string qsize_trace_filename = "qsizeTrace-dumbbell";
    std::string dropped_trace_filename = "droppedPacketTrace-dumbbell";
    std::string bottleneck_tx_filename = "bottleneckTx-dumbbell";
    std::string tc_qsize_trace_filename = "tc-qsizeTrace-dumbbell";
    std::string parametersFileName = "parameters";
    std::string rttFileName = "RTTs";
    float stop_time = 500;
    float start_time = 0;
    float start_tracing_time = 5;
    bool enable_bot_trace = 0;

    CommandLine cmd(__FILE__);
    cmd.AddValue("nNodes", "Number of nodes in right and left", nNodes);
    cmd.AddValue("RTT", "Round Trip Time for a packet", RTT);

    cmd.Parse(argc, argv);
    NS_LOG_UNCOND("Starting Simulation");
    NS_LOG_UNCOND("RTT value : " << RTT);

    Config::SetDefault("ns3::TcpL4Protocol::SocketType",
                       StringValue(tcp_type_id));
    // Config::SetDefault ("ns3::TcpSocket::SndBufSize", UintegerValue
    // (4194304)); Config::SetDefault ("ns3::TcpSocket::RcvBufSize",
    // UintegerValue (6291456));
    Config::SetDefault("ns3::TcpSocket::InitialCwnd",
                       UintegerValue(initial_cwnd));
    Config::SetDefault("ns3::TcpSocket::DelAckCount",
                       UintegerValue(del_ack_count));
    Config::SetDefault("ns3::TcpSocket::SegmentSize",
                       UintegerValue(segmentSize));
    // Config::SetDefault ("ns3::DropTailQueue<Packet>::MaxSize", QueueSizeValue
    // (QueueSize ("1p"))); Config::SetDefault (queue_disc + "::MaxSize",
    // QueueSizeValue (QueueSize (queue_size)));
    Config::SetDefault("ns3::TcpSocketBase::MaxWindowSize",
                       UintegerValue(20 * 1000));
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

    // two for router and nNodes on left and right of bottleneck
    NodeContainer nodes;
    nodes.Create(2 + nNodes * 2);
    // Source nodes
    NodeContainer leftNodes[nNodes];
    // Destination nodes
    NodeContainer rightNodes[nNodes];

    // router
    NodeContainer r1r2 = NodeContainer(nodes.Get(0), nodes.Get(1));

    for (uint32_t i = 0; i < nNodes; i++) {
        leftNodes[i] = NodeContainer(nodes.Get(i + 2), nodes.Get(0));
        rightNodes[i] = NodeContainer(nodes.Get(2 + nNodes + i), nodes.Get(1));
    }

    // creating channel
    // Defining the links to be used between nodes
    double min = double(std::stoi(RTT.substr(0, RTT.length() - 2))) - 10.0;
    double max = double(std::stoi(RTT.substr(0, RTT.length() - 2))) + 10.0;

    Ptr<UniformRandomVariable> x = CreateObject<UniformRandomVariable>();
    x->SetAttribute("Min", DoubleValue(min));
    x->SetAttribute("Max", DoubleValue(max));

    PointToPointHelper p2p_router;
    p2p_router.SetDeviceAttribute("DataRate",
                                  StringValue(bottleneck_bandwidth));
    p2p_router.SetChannelAttribute("Delay", StringValue(bottleneck_delay));
    p2p_router.SetQueue("ns3::DropTailQueue<Packet>", "MaxSize",
                        QueueSizeValue(QueueSize(queueSize)));
    // p2p_router.DisableFlowControl();

    // write RTT
    AsciiTraceHelper rtt_helper;
    rtts = rtt_helper.CreateFileStream(dir + rttFileName + ".txt");

    PointToPointHelper p2p_s[nNodes], p2p_d[nNodes];
    for (uint32_t i = 0; i < nNodes; i++) {
        double delay = (x->GetValue()) / 4;

        std::string delay_str = std::to_string(delay) + "ms";

        // write delay
        *rtts->GetStream() << i << " " << delay * 4 << std::endl;

        p2p_s[i].SetDeviceAttribute("DataRate", StringValue(access_bandwidth));
        p2p_s[i].SetChannelAttribute("Delay", StringValue(delay_str));
        p2p_s[i].SetQueue(
            "ns3::DropTailQueue<Packet>", "MaxSize",
            QueueSizeValue(QueueSize(std::to_string(0 / nNodes) +
                                     "p"))); // p in 1000p stands for packets
        p2p_s[i].DisableFlowControl();

        p2p_d[i].SetDeviceAttribute("DataRate", StringValue(access_bandwidth));
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

    /////////////////////////////////////////////////////
    /////////////// Traffic Controller //////////////////
    // Remove any existing queue disc that might be installed
    for (NetDeviceContainer::Iterator i = r1r2ND.Begin(); i != r1r2ND.End();
         ++i) {
        Ptr<NetDevice> device = *i;
        Ptr<TrafficControlLayer> tcLayer =
            device->GetNode()->GetObject<TrafficControlLayer>();

        if (tcLayer != nullptr) {
            Ptr<QueueDisc> rootDisc = tcLayer->GetRootQueueDiscOnDevice(device);
            if (rootDisc != nullptr) {
                tcLayer->DeleteRootQueueDiscOnDevice(
                    device); // Remove existing queue disc
            }
        }
    }
    TrafficControlHelper tch;
    tch.SetRootQueueDisc("ns3::FifoQueueDisc", "MaxSize",
                         QueueSizeValue(QueueSize(tc_queueSize)));
    // tch.SetRootQueueDisc("ns3::AdaptiveFifoQueueDisc", "MaxSize",
    // QueueSizeValue (QueueSize (queue_size)), "AdaptationInterval",
    // StringValue("1s"),
    //                     "AdaptationThreshold", UintegerValue(20));
    QueueDiscContainer queueDiscs = tch.Install(r1r2ND);
    // // two devices
    // // for(auto i = queueDiscs.Begin(); i != queueDiscs.End(); ++i)
    // NS_LOG_UNCOND("queueDiscs "<<*i);
    Ptr<QueueDisc> queueDisc = queueDiscs.Get(0);
    queueDisc_router = queueDiscs.Get(0);
    ///////-------------------->>>>>>>>>>>>>>>>>>>>>
    // SetQueueSize(tc_queueSize);
    //////--------------------->>>>>>>>>>>>>>>>>>>>>

    // // tracing queue Size change
    // AsciiTraceHelper ascii;
    // Ptr<Queue<Packet> > queue = StaticCast<PointToPointNetDevice> (r1r2ND.Get
    // (0))->GetQueue (); Ptr<OutputStreamWrapper> streamBytesInQueue =
    // ascii.CreateFileStream ( "result-cs-bytesInQueue.txt");
    // queue->TraceConnectWithoutContext ("BytesInQueue",MakeBoundCallback
    // (&BytesInQueueTrace, streamBytesInQueue));

    // Schedule periodic queue size adjustments
    // Time adjustmentInterval = Seconds(10.0);
    // Simulator::Schedule(adjustmentInterval, &PeriodicQueueAdjustment,
    // queueDisc, adjustmentInterval); Simulator::Schedule(
    // Seconds(start_time+1), &PeriodicQueueAdjustment, queueDisc,
    // adjustmentInterval);

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
        sinkApp[i].Start(Seconds(start_time));
        sinkApp[i].Stop(Seconds(stop_time));
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

    double stime = start_time;
    // Configuring the application at each source node.
    for (uint32_t i = 0; i < nNodes; i++) {
        BulkSendHelper tmp_source(
            "ns3::TcpSocketFactory",
            InetSocketAddress(rIp[i].GetAddress(0), port));

        // Set the amount of data to send in bytes.  Zero is unlimited.
        tmp_source.SetAttribute("MaxBytes", UintegerValue(bytes_to_send));
        sourceApps[i] = tmp_source.Install(nodes.Get(2 + i));

        sourceApps[i].Start(Seconds(stime));
        sourceApps[i].Stop(Seconds(stop_time));
        double gap = expRandomVariable->GetValue();

        stime += gap;
    }

    // write parameters
    AsciiTraceHelper parameters_helper;

    parameters =
        parameters_helper.CreateFileStream(dir + parametersFileName + ".txt");
    *parameters->GetStream() << "regular cwnd sampling." << std::endl;
    *parameters->GetStream() << "Nodes : " << "\t" << nNodes << std::endl;
    *parameters->GetStream()
        << "TCP type id: " << "\t" << tcp_type_id << std::endl;
    *parameters->GetStream() << "RTT : " << "\t" << RTT << std::endl;
    *parameters->GetStream()
        << "Bottleneck Delay: " << "\t" << bottleneck_delay << std::endl;
    *parameters->GetStream() << "Bottleneck Bandwidth: " << "\t"
                             << bottleneck_bandwidth << std::endl;
    *parameters->GetStream()
        << "Queue Disc: " << "\t" << queue_disc << std::endl;
    *parameters->GetStream()
        << "Queue Size: " << "\t" << queue_size << std::endl;
    *parameters->GetStream()
        << "Simulation Stop time: " << "\t" << stop_time << std::endl;

    // Configuring file stream to write the Qsize
    AsciiTraceHelper ascii_qsize;
    qSize_stream =
        ascii_qsize.CreateFileStream(dir + qsize_trace_filename + ".txt");

    // trace traffic control qsize
    AsciiTraceHelper ascii_tc_qsize;
    tc_qSize_stream =
        ascii_tc_qsize.CreateFileStream(dir + tc_qsize_trace_filename + ".txt");

    // Configuring file stream to write the no of packets transmitted by the
    // bottleneck
    AsciiTraceHelper ascii_qsize_tx;
    bottleneckTransimittedStream =
        ascii_qsize_tx.CreateFileStream(dir + bottleneck_tx_filename + ".txt");

    AsciiTraceHelper ascii_dropped;
    dropped_stream =
        ascii_dropped.CreateFileStream(dir + dropped_trace_filename + ".txt");
    // start tracing the congestion window size and qSize

    Simulator::Schedule(Seconds(stime), &start_tracing_timeCwnd, nNodes);
    Simulator::Schedule(Seconds(stime), &StartTracingQueueSize);
    Simulator::Schedule(Seconds(stime), &StartTracingTransmitedPacket);
    Simulator::Schedule(Seconds(stime), &updateCwndValues, nNodes);
    //    Simulator::Schedule( Seconds(stime+start_tracing_time),
    //    &writeCwndToFile, nNodes);

    // start tracing Queue Size and Dropped Files
    Simulator::Schedule(Seconds(stime), &TraceDroppedPacket,
                        dropped_trace_filename);
    // writing the congestion windows size, queue_size, packetTx to files
    // periodically ( 1 sec. )
    for (auto time = stime + start_tracing_time; time < stop_time;
         time += 0.1) {
        Simulator::Schedule(Seconds(time), &writeCwndToFile, nNodes);
        Simulator::Schedule(Seconds(time), &TraceQueueSizeTc, queueDisc);
        Simulator::Schedule(Seconds(time), &TraceQueueSize);
        Simulator::Schedule(Seconds(time), &TraceBottleneckTx);
        Simulator::Schedule(Seconds(time), &TraceDroppedPkts);
    }

    if (enable_bot_trace == 1) {
        AsciiTraceHelper bottleneck_ascii;
        p2p_router.EnableAscii(bottleneck_ascii.CreateFileStream(
                                   dir + "bottleneck-trace-router0.tr"),
                               leftND[0]);
    }

    // Check for dropped packets using Flow Monitor
    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll();

    Simulator::Stop(Seconds(stop_time + cleanup_time));
    Simulator::Run();
    monitor->SerializeToXmlFile(dir + "dumbbell-flowmonitor.xml", false, true);
    Simulator::Destroy();

    return 0;
}
