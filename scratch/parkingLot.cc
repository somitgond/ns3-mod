/*
Parking Lot topology
Active Queue Management using variable maxSize


Toplology
    Source S1 [30]         Source S3 [30]        Destination D2 [30]
        |         BL 1          |          BL 2         |      
        R1 -------------------- R2 -------------------- R3 
        |                       |                       |      
    Source S2 [30]        Destination D1 [30]    Destination D3 [30]


Traffic flow: 
          S1 -> D1
          S2 -> D2
          S3 -> D3

Total Nodes = 
      Sources: 30 + 30 + 30 = 90
  Destination: 30 + 30 + 30 = 90
      Routers: 1  +  1 + 1  = 3
                            -----
                             183
                          
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
#include "ns3/mobility-helper.h"
#include "ns3/node.h"
#include "ns3/point-to-point-module.h"
#include "ns3/tcp-header.h"
#include "ns3/traffic-control-module.h"
#include "ns3/udp-header.h"
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <numeric>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("TCPSCRIPT");

std::string dir = "result-parkingLot/";
uint32_t prev = 0;
Time prevTime = Seconds(0);
uint32_t segmentSize = 1400; // segment size 
double segSize = segmentSize;
uint32_t threshold = 10;
uint32_t increment = 100;
uint32_t nNodes = 30; // 60 + 60 [source] -> 60 + 60 [destination
uint32_t totalSourceNodes = nNodes *3;
uint32_t bytes_to_send = 100000000;                    // 0 for unbounded
double cap, Tao, rtt_global;
std::string queue_disc = "ns3::FifoQueueDisc";

bool AQM_ENABLED = 0; // 0: if we want to run our aqm, 1: don't run our aqm

// store parameters in a file
Ptr<OutputStreamWrapper> parameters;


std::vector<uint32_t> cwnd;
std::vector<Ptr<OutputStreamWrapper>> cwnd_streams;
Ptr<OutputStreamWrapper> rtts;

uint64_t queue_size;
Ptr<OutputStreamWrapper> qSize_stream;
Ptr<OutputStreamWrapper> tc_qSize_stream;

uint64_t bottleneckTransimittedBytes;
Ptr<OutputStreamWrapper> bottleneckTransimittedStream;

uint64_t droppedPackets;
Ptr<OutputStreamWrapper> dropped_stream;

// queue disc in router 1
Ptr<QueueDisc> queueDisc_router = CreateObject<FifoQueueDisc>();

// find zero crossings in autocorrelation in queue data
uint32_t Q_WINDOW = 50;
// zero crossing threshold, determined after doing experimentation with different queue size
uint32_t ZC_THRE = 5; 
std::vector<double> qSizeData(Q_WINDOW);
uint32_t numOfObs = 0;
std::vector<double> zerocrossings_data;
Ptr<OutputStreamWrapper> zc_stream;

std::vector<uint64_t> clientBytes;

void TxTrace (uint32_t clientId, Ptr<const Packet> p)
{
    clientBytes[clientId] += p->GetSize ();
}

// Compute mean
double computeMean(const std::vector<double> &x) {
    return std::accumulate(x.begin(), x.end(), 0.0) / x.size();
}

// Subtract mean
std::vector<double> meanCentered(const std::vector<double> &x, double mean) {
    std::vector<double> centered(x.size());
    for (size_t i = 0; i < x.size(); ++i) {
        centered[i] = x[i] - mean;
    }
    return centered;
}

// Full cross-correlation of a signal with itself
std::vector<double> fullAutocorrelation(const std::vector<double> &x) {
    int n = x.size();
    double mean = computeMean(x);
    std::vector<double> x_centered = meanCentered(x, mean);

    int size_full = 2 * n - 1;
    std::vector<double> result(size_full, 0.0);

    // Cross-correlation at all lags: from -n+1 to n-1
    for (int lag = -n + 1; lag < n; ++lag) {
        double sum = 0.0;
        for (int i = 0; i < n; ++i) {
            int j = i + lag;
            if (j >= 0 && j < n) {
                sum += x_centered[i] * x_centered[j];
            }
        }
        result[lag + n - 1] = sum;
    }

    // Normalize by zero-lag value (center of full correlation)
    double zero_lag = result[n - 1];
    for (int i = n - 1; i < size_full; ++i) {
        result[i] /= zero_lag;
    }

    // Return only non-negative lags
    return std::vector<double>(result.begin() + n - 1, result.end());
}

// Count zero crossings
int countZeroCrossings(const std::vector<double> &x) {
    int count = 0;
    for (size_t i = 0; i < x.size() - 1; ++i) {
        if ((x[i] > 0 && x[i + 1] < 0) || (x[i] < 0 && x[i + 1] > 0)) {
            ++count;
        }
    }
    return count;
}

//////////////// get qth ///////////////
int giveQth(double w_av, double beta, int B) {
    double capacity = 100; // in mbitsps
    double pi = 3.141593, c = (capacity * 1000000 / (segSize * 8 * nNodes)),
    tao = rtt_global/1000;
    //    tao = 0.5;
    cap = c;
    Tao = tao;
    double val = pi/2;

    int qth = 10;
    double f;
    if(w_av > (c*tao)) f = qth*beta*(c*tao)*pow(0.9, qth+1);
    else f = qth*beta*w_av*pow((w_av/(c*tao)), qth);


    while(f > val && qth < B){
        qth += 1;
        if(w_av > (c*tao)) f = qth*beta*(c*tao)*pow(0.9, qth+1);
        else f = qth*beta*w_av*pow((w_av/(c*tao)), qth);
    }

    return qth;
}

//////////////////////////////////////////////

// set new Queue size
void SetQueueSize(uint32_t qth) {
    QueueSize currentSize = queueDisc_router->GetMaxSize();
    NS_LOG_UNCOND("Queue MaxsizeSize " << currentSize.GetValue());
    if (currentSize.GetValue() == qth)
        return;
    std::string qth_str = std::to_string(qth) + "p";
    QueueSize newSize = QueueSize(qth_str);
    queueDisc_router->SetMaxSize(newSize);
    NS_LOG_UNCOND("Queue size adjusted to " << newSize);
}


void TraceQueueSizeTc(Ptr<QueueDisc> queueDisc) {
    // Trace Queue Size in Traffic Control Layer
    *tc_qSize_stream->GetStream()
        << Simulator::Now().GetSeconds() << " "
        << queueDisc->GetCurrentSize().GetValue() << std::endl;

    // check zero crossings
    if ((numOfObs != 0) && (numOfObs % Q_WINDOW == 0)) {
        std::vector<double> autocorr = fullAutocorrelation(qSizeData);
        auto zc_val = countZeroCrossings(autocorr);
        zerocrossings_data.push_back(zc_val);
        *zc_stream->GetStream()
            << Simulator::Now().GetSeconds() << " " << zc_val << std::endl;
    }
    qSizeData[numOfObs % Q_WINDOW] = queueDisc->GetCurrentSize().GetValue() + 1;
    numOfObs++;
}

static void plotQsizeChange(uint32_t oldQSize, uint32_t newQSize) {
    // NS_LOG_UNCOND(Simulator::Now().GetSeconds() << "\t" << newCwnd);
    queue_size = newQSize;
}

static void RxDrop(Ptr<OutputStreamWrapper> stream, Ptr<const Packet> p) {
    droppedPackets++;
}

static void TxPacket(Ptr<const Packet> p) { 
  bottleneckTransimittedBytes += p->GetSize(); 
}

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
        << Simulator::Now().GetSeconds() << "\t" << bottleneckTransimittedBytes
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
}

static void StartTracingTransmitedPacket() {
    bottleneckTransimittedBytes = 0;
    Config::ConnectWithoutContext(
        "/NodeList/0/DeviceList/0/$ns3::PointToPointNetDevice/PhyTxEnd",
        MakeCallback(&TxPacket));
}


//////////// CALCULATNG BETA /////////////////
bool needToUpdate = true;
bool hasSynchrony = false;
std::vector<double> betas;
std::vector<double> countBeta;
std::vector<bool> dipStarted;
std::vector<double> highs;

std::vector<double> prevWin;
std::vector<double> dropCounts;
double sumWin = 0;

void initiateArray() {
    cwnd = std::vector<uint32_t>(nNodes+1, 0);
    clientBytes = std::vector<uint64_t>(nNodes, 0);
    betas = std::vector<double>(nNodes + 1, 0.5);
    countBeta = std::vector<double>(nNodes + 1, 0);
    dipStarted = std::vector<bool>(nNodes + 1, false);
    highs = std::vector<double>(nNodes + 1, 0);
    prevWin = std::vector<double>(nNodes + 1, 0);
    dropCounts = std::vector<double>(nNodes + 1, 0);
}

double getBeta() {
    double beta = 0, sm = 0;
    for (int i = 0; i < (int)nNodes; i++) {
        beta += betas[i] * dropCounts[i];
        sm += dropCounts[i];
    }

    return beta / sm;
}

///////////////////////////////////////////////////////////////////////////////

// Trace congestion window
static void CwndTracer(uint32_t node, uint32_t oldval, uint32_t newval) {
    double oldVal = (double)oldval / segSize;
    sumWin += (oldVal - prevWin[node]); prevWin[node] = oldVal;

    if (newval < oldval) {
        // loss_events[node] = 1;
        dropCounts[node] += 1;
    }

    // NS_LOG_UNCOND("old and new: " << oldVal << " "<<newVal);
    if (!dipStarted[node] && (oldval > newval)) {
        dipStarted[node] = true;
        // NS_LOG_UNCOND("---Gothigh " << oldVal);
        highs[node] = oldval;
    }
    if (dipStarted[node] && (oldval < newval) &&
        ((highs[node] - (double)oldval) / highs[node]) > 0.1 &&
        ((highs[node] - (double)oldval) / highs[node]) < 0.9) {

        dipStarted[node] = false;
        double newBeta = (highs[node] - (double)oldval) / highs[node];
        betas[node] = (betas[node] * countBeta[node]) + newBeta;
        countBeta[node] += 1;
        betas[node] = betas[node]/countBeta[node];

#if 0
        int qth = giveQth(sumWin / nNodes, getBeta(), 2084);
#else
        int qth = 0;
#endif

        // zero crossings data is greater than 3
        int temp_len = zerocrossings_data.size();
        auto t_gp = getBeta();
        // NS_LOG_UNCOND("w*: "<<(sumWin / nNodes)<<" limit: "<<(cap * Tao));
        // NS_LOG_UNCOND("t_gp: "<<t_gp<<" qTh: "<<qth<<" tempLen: "<<temp_len);
        if ((t_gp > 0.1) && (t_gp < 0.9) && (qth > 0) && (temp_len > 3)) {
            // NS_LOG_UNCOND("w*: "<<(sumWin / nNodes)<<" limit: "<<(cap * Tao));
            // NS_LOG_UNCOND("t_gp: "<<t_gp<<" qTh: "<<qth<<" tempLen: "<<temp_len);
            auto ta = zerocrossings_data[temp_len - 1];
            auto tb = zerocrossings_data[temp_len - 2];
            auto tc = zerocrossings_data[temp_len - 3];
            if(qth >= 2084)std::cout<<"!!! qth anomaly with qth:"<<qth<<std::endl;
            // AQM will be triggered only once
            if ((Simulator::Now().GetSeconds() > 100) && (ta < ZC_THRE) &&
                (tb < ZC_THRE) && (tc < ZC_THRE) && (AQM_ENABLED == 0) && (queue_disc == "ns3::FifoQueueDisc")) {
                SetQueueSize(qth);
                *parameters->GetStream() << "AQM triggered with qth: " <<qth 
                    << " w* : " << sumWin/nNodes << " beta: "<< getBeta()<< std::endl;
                *zc_stream->GetStream()
                    << Simulator::Now().GetSeconds() << " " << -1 << std::endl;
                AQM_ENABLED = 1; // reset the flag
                NS_LOG_UNCOND("----------------------DONE!!");
                NS_LOG_UNCOND("--------BETA: " << getBeta() << "-------");
                NS_LOG_UNCOND("--------AQM_ENABLED: " << AQM_ENABLED << "-------");
                zerocrossings_data.clear(); // clear zero crossing data after aqm is enabled
            }
        }
    }

    cwnd[node] = newval / segmentSize;
    //    *cwnd_streams[node]->GetStream() << Simulator::Now ().GetSeconds () <<
    //    " " << newval/segmentSize<< std::endl;
}

// Update values as cwndChanges
static void updateCwndValues(uint32_t nNodes) {
    for (uint32_t i = 0; i < nNodes; i++) {
        std::string path = "/NodeList/" + std::to_string(i + 3) +
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

// check if all flows has finished sending 'bytes_to_send' data
void CheckCompletion (std::vector<Ptr<BulkSendApplication>> apps)
{
  int totCount = 0;
  for (int i = 0; i < nNodes; i++)
  {
    if (clientBytes[i] >= bytes_to_send) // still sending
      totCount++;
  }

  if (totCount == nNodes)
  {
    std::cout << "All flows finished at " << Simulator::Now ().GetSeconds () << "s\n";
    Simulator::Stop ();
  }
  else
  {
    Simulator::Schedule (Seconds (1.0), &CheckCompletion, apps);
  }
}


int main(int argc, char *argv[]) {
    
    uint32_t del_ack_count = 1;
    uint32_t cleanup_time = 2;
    uint32_t initial_cwnd = 10;
    std::string tcp_type_id = "ns3::TcpLinuxReno"; // TcpNewReno
    std::string queueSize = "1p";
    std::string tc_queueSize = "2083p";
    std::string RTT = "198ms"; // round-trip time of each TCP flow
    std::string bottleneck1_bandwidth = "100Mbps"; // bandwidth of the bottleneck link
    std::string bottleneck1_delay = "1ms"; // bottleneck link has negligible propagation delay
    std::string bottleneck2_bandwidth = "100Mbps"; // bandwidth of the bottleneck link
    std::string bottleneck2_delay = "1ms"; // bottleneck link has negligible propagation delay
    std::string access_bandwidth = "2Mbps";
    std::string root_dir;
    std::string qsize_trace_filename = "qsizeTrace-dumbbell";
    std::string zc_trace_filename = "zeroCrossingTrace-dumbbell";
    std::string dropped_trace_filename = "droppedPacketTrace-dumbbell";
    std::string bottleneck_tx_filename = "bottleneckTx-dumbbell";
    std::string tc_qsize_trace_filename = "tc-qsizeTrace-dumbbell";
    std::string rttFileName = "RTTs";

    std::string parametersFileName = "parameters";
    float stop_time = 500;
    float start_time = 0;
    float start_tracing_time = 5;
    bool enable_bot_trace = 0;
    bool enable_bot_pcap = 0;

    CommandLine cmd(__FILE__);
    cmd.AddValue("nNodes", "Number of nodes in right and left", nNodes);
    cmd.AddValue("RTT", "Round Trip Time for a packet", RTT);
    cmd.AddValue("queue_disc", "Queue disc to use", queue_disc);
    cmd.AddValue("bytes_to_send", "Total bytes to send", bytes_to_send);
    cmd.AddValue("AQM_ENABLED", "To enable aqm or not", AQM_ENABLED);
    
    cmd.Parse(argc, argv);
    NS_LOG_UNCOND("Starting Simulation");
    NS_LOG_UNCOND("nNodes : " << nNodes);
    NS_LOG_UNCOND("RTT value : " << RTT);
    NS_LOG_UNCOND("Queue disc : " << queue_disc);
    NS_LOG_UNCOND("total bytes : " << bytes_to_send);
    NS_LOG_UNCOND("AQM_ENABLED: " << AQM_ENABLED);

    Config::SetDefault("ns3::TcpL4Protocol::SocketType", StringValue(tcp_type_id));
    // Config::SetDefault ("ns3::TcpSocket::SndBufSize", UintegerValue (4194304)); 
    // Config::SetDefault ("ns3::TcpSocket::RcvBufSize", UintegerValue (6291456));
    Config::SetDefault("ns3::TcpSocket::InitialCwnd", UintegerValue(initial_cwnd));
    Config::SetDefault("ns3::TcpSocket::DelAckCount", UintegerValue(del_ack_count));
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(segmentSize));
    // Config::SetDefault ("ns3::DropTailQueue<Packet>::MaxSize", QueueSizeValue (QueueSize ("1p"))); 

    // set traffic control queue size according to queue disc
    if (queue_disc == "ns3::RedQueueDisc")
    {
      Config::SetDefault ("ns3::RedQueueDisc::MeanPktSize", UintegerValue (segmentSize));
      Config::SetDefault ("ns3::RedQueueDisc::Gentle", BooleanValue (false));

      // set min and max qth
      int minTh = 50;
      int maxTh = 100;

      NS_LOG_UNCOND("minTh: "<<minTh);
      NS_LOG_UNCOND("maxTh: "<<maxTh);

      Config::SetDefault ("ns3::RedQueueDisc::MinTh", DoubleValue (minTh));
      Config::SetDefault ("ns3::RedQueueDisc::MaxTh", DoubleValue (maxTh));

      Config::SetDefault ("ns3::RedQueueDisc::LinkBandwidth", StringValue (bottleneck1_bandwidth));
      Config::SetDefault ("ns3::RedQueueDisc::LinkDelay", StringValue (bottleneck1_delay));

      // enable ARED
      Config::SetDefault ("ns3::RedQueueDisc::ARED", BooleanValue (true));
    }
    Config::SetDefault (queue_disc + "::MaxSize", QueueSizeValue (QueueSize (tc_queueSize)));
    // Config::SetDefault("ns3::TcpSocketBase::MaxWindowSize", UintegerValue(20 * 1000));

    initiateArray();
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
    
    AsciiTraceHelper ascii_zc;
    zc_stream = ascii_zc.CreateFileStream(dir + zc_trace_filename + ".txt");
    
    // two for router and nNodes on left and right of bottleneck
    NodeContainer nodes;
    nodes.Create(3 + (totalSourceNodes * 2));

    // Source nodes
    NodeContainer S1[nNodes], S2[nNodes], S3[nNodes];

    // Destination nodes
    NodeContainer D1[nNodes], D2[nNodes], D3[nNodes];

    // router
    NodeContainer r1r2 = NodeContainer(nodes.Get(0), nodes.Get(1));
    NodeContainer r2r3 = NodeContainer(nodes.Get(1), nodes.Get(2));
                                                      
    for (uint32_t i = 0; i < nNodes; i++) {
        // S1 to R1
        S1[i] = NodeContainer(nodes.Get(i+3), nodes.Get(0));

        S2[i] = NodeContainer(nodes.Get(nNodes+i+3), nodes.Get(0));

        // S3 to R2
        S3[i] = NodeContainer(nodes.Get((2*nNodes)+i+3), nodes.Get(1));

        // D1 to R2
        D1[i] = NodeContainer(nodes.Get((3*nNodes)+i+3), nodes.Get(1));

        // D2 to R3
        D2[i] = NodeContainer(nodes.Get((4*nNodes)+i+3), nodes.Get(2));

        D3[i] = NodeContainer(nodes.Get((5*nNodes)+i+3), nodes.Get(2));
    }
    // write topology connection 
    Ptr<OutputStreamWrapper> topology_connection;
    AsciiTraceHelper topology_connection_helper;

    topology_connection = topology_connection_helper.CreateFileStream(dir + "topology.txt");
    *topology_connection->GetStream() << "regular cwnd sampling." << std::endl;
    for (uint32_t i = 0; i < nNodes; i++)
    {
     *topology_connection->GetStream() << "S1["<<i<<"] -> NodeID " << 
       S1[i].Get(0)->GetId() << " connected to R1 NodeID " << S1[i].Get(1)->GetId() << std::endl;
    }
    for (uint32_t i = 0; i < nNodes; i++)
    {
     *topology_connection->GetStream()  << "S2["<<i<<"] -> NodeID " << 
       S2[i].Get(0)->GetId() << " connected to R1 NodeID " << S2[i].Get(1)->GetId() << std::endl;
    }
    for (uint32_t i = 0; i < nNodes; i++)
    {
      *topology_connection->GetStream() << "S3["<<i<<"] -> NodeID " << 
        S3[i].Get(0)->GetId() << " connected to R2 NodeID " << S3[i].Get(1)->GetId() << std::endl;
    }

    for (uint32_t i = 0; i < nNodes; i++)
    {
      *topology_connection->GetStream()  << "D1["<<i<<"] -> NodeID " << 
        D1[i].Get(0)->GetId() << " connected to R2 NodeID " << D1[i].Get(1)->GetId() << std::endl;
    }
    for (uint32_t i = 0; i < nNodes; i++)
    {
      *topology_connection->GetStream()  << "D2["<<i<<"] -> NodeID " << 
        D2[i].Get(0)->GetId() << " connected to R3 NodeID " << D2[i].Get(1)->GetId() << std::endl;
    }
    for (uint32_t i = 0; i < nNodes; i++)
    {
      *topology_connection->GetStream()  << "D3["<<i<<"] -> NodeID " << 
        D3[i].Get(0)->GetId() << " connected to R3 NodeID " << D3[i].Get(1)->GetId() << std::endl;
    }

    // write RTT
    AsciiTraceHelper rtt_helper;
    rtts = rtt_helper.CreateFileStream(dir + rttFileName + ".txt");

    // creating channel
    // Defining the links to be used between nodes
    double min = double(std::stoi(RTT.substr(0, RTT.length() - 2))) - 10;
    double max = double(std::stoi(RTT.substr(0, RTT.length() - 2))) + 10;
    
    // assigning tao and capacity
    rtt_global = std::stod(RTT.substr(0, RTT.length() - 2)) + 2; // 2 for bottleneck link
    giveQth(1, 1, 1);
    NS_LOG_UNCOND("limit: "<<cap <<" "<< Tao);
    
    Ptr<UniformRandomVariable> x = CreateObject<UniformRandomVariable>();
    x->SetAttribute("Min", DoubleValue(min));
    x->SetAttribute("Max", DoubleValue(max));

    // FIXME: each bottleneck 
    PointToPointHelper p2p_router1, p2p_router2;
    p2p_router1.SetDeviceAttribute("DataRate", StringValue(bottleneck1_bandwidth));
    p2p_router1.SetChannelAttribute("Delay", StringValue(bottleneck1_delay));
    p2p_router1.SetQueue("ns3::DropTailQueue<Packet>", "MaxSize", QueueSizeValue(QueueSize(queueSize)));
    // p2p1_router.DisableFlowControl();

    p2p_router2.SetDeviceAttribute("DataRate", StringValue(bottleneck2_bandwidth));
    p2p_router2.SetChannelAttribute("Delay", StringValue(bottleneck2_delay));
    p2p_router2.SetQueue("ns3::DropTailQueue<Packet>", "MaxSize", QueueSizeValue(QueueSize(queueSize)));

    // source and destination link attributes
    PointToPointHelper p2p_s1[nNodes], p2p_s2[nNodes], p2p_s3[nNodes];
    PointToPointHelper p2p_d1[nNodes], p2p_d2[nNodes], p2p_d3[nNodes];

    for (uint32_t i = 0; i < nNodes; i++) {
        double delay = (x->GetValue()) / 4;
        std::string delay_str = std::to_string(delay) + "ms";

        // FIXME: write properdelay
        *rtts->GetStream() << i << " " << delay * 4 << std::endl;

        p2p_s1[i].SetDeviceAttribute("DataRate", StringValue(access_bandwidth));
        p2p_s1[i].SetChannelAttribute("Delay", StringValue(delay_str));
        // p in 1000p stands for packets
        // FIXME: queue size should be 0 ?
        p2p_s1[i].SetQueue( "ns3::DropTailQueue<Packet>", "MaxSize", QueueSizeValue(QueueSize(std::to_string(1000 / nNodes) +"p"))); 
        p2p_s1[i].DisableFlowControl();

        p2p_s2[i].SetDeviceAttribute("DataRate", StringValue(access_bandwidth));
        p2p_s2[i].SetChannelAttribute("Delay", StringValue(delay_str));
        // p in 1000p stands for packets
        p2p_s2[i].SetQueue( "ns3::DropTailQueue<Packet>", "MaxSize", QueueSizeValue(QueueSize(std::to_string(1000 / nNodes) +"p"))); 
        p2p_s2[i].DisableFlowControl();

        p2p_s3[i].SetDeviceAttribute("DataRate", StringValue(access_bandwidth));
        p2p_s3[i].SetChannelAttribute("Delay", StringValue(delay_str));
        // p in 1000p stands for packets
        p2p_s3[i].SetQueue( "ns3::DropTailQueue<Packet>", "MaxSize", QueueSizeValue(QueueSize(std::to_string(1000 / nNodes) +"p"))); 
        p2p_s3[i].DisableFlowControl();

        p2p_d1[i].SetDeviceAttribute("DataRate", StringValue(access_bandwidth));
        p2p_d1[i].SetChannelAttribute("Delay", StringValue(delay_str));
        // p in 1000p stands for packets
        p2p_d1[i].SetQueue( "ns3::DropTailQueue<Packet>", "MaxSize", QueueSizeValue(QueueSize(std::to_string(1000 / nNodes) +"p"))); 
        p2p_d1[i].DisableFlowControl();

        p2p_d2[i].SetDeviceAttribute("DataRate", StringValue(access_bandwidth));
        p2p_d2[i].SetChannelAttribute("Delay", StringValue(delay_str));
        // p in 1000p stands for packets
        p2p_d2[i].SetQueue( "ns3::DropTailQueue<Packet>", "MaxSize", QueueSizeValue(QueueSize(std::to_string(1000 / nNodes) +"p"))); 
        p2p_d2[i].DisableFlowControl();

        p2p_d3[i].SetDeviceAttribute("DataRate", StringValue(access_bandwidth));
        p2p_d3[i].SetChannelAttribute("Delay", StringValue(delay_str));
        // p in 1000p stands for packets
        p2p_d3[i].SetQueue( "ns3::DropTailQueue<Packet>", "MaxSize", QueueSizeValue(QueueSize(std::to_string(1000 / nNodes) +"p"))); 
        p2p_d3[i].DisableFlowControl();
    }

    // network device on each node
    NetDeviceContainer r1r2ND = p2p_router1.Install(r1r2);
    NetDeviceContainer r2r3ND = p2p_router2.Install(r2r3);

    std::vector<NetDeviceContainer> S1ND_v, S2ND_v, S3ND_v, D1ND_v, D2ND_v, D3ND_v;
    for (uint32_t i = 0; i < nNodes; i++) {
        S1ND_v.push_back(p2p_s1[i].Install(S1[i]));
        S2ND_v.push_back(p2p_s2[i].Install(S2[i]));
        S3ND_v.push_back(p2p_s3[i].Install(S3[i]));
        D1ND_v.push_back(p2p_d1[i].Install(D1[i]));
        D2ND_v.push_back(p2p_d2[i].Install(D2[i]));
        D3ND_v.push_back(p2p_d3[i].Install(D3[i]));
    }


    // Installing internet stack
    InternetStackHelper stack;
    stack.InstallAll(); // install internet stack on all nodes

    /////////////////////////////////////////////////////
    /////////////// Traffic Controller //////////////////
    // Remove any existing queue disc that might be installed
    for (NetDeviceContainer::Iterator i = r1r2ND.Begin(); i != r1r2ND.End(); ++i) {
        Ptr<NetDevice> device = *i;
        Ptr<TrafficControlLayer> tcLayer = device->GetNode()->GetObject<TrafficControlLayer>();

        if (tcLayer != nullptr) {
            Ptr<QueueDisc> rootDisc = tcLayer->GetRootQueueDiscOnDevice(device);
            if (rootDisc != nullptr) {
                tcLayer->DeleteRootQueueDiscOnDevice(device); // Remove existing queue disc
            }
        }
    }

    for (NetDeviceContainer::Iterator i = r2r3ND.Begin(); i != r2r3ND.End(); ++i) {
        Ptr<NetDevice> device = *i;
        Ptr<TrafficControlLayer> tcLayer = device->GetNode()->GetObject<TrafficControlLayer>();

        if (tcLayer != nullptr) {
            Ptr<QueueDisc> rootDisc = tcLayer->GetRootQueueDiscOnDevice(device);
            if (rootDisc != nullptr) {
                tcLayer->DeleteRootQueueDiscOnDevice(device); // Remove existing queue disc
            }
        }
    }

    //////////////////// Install traffic controller //////////////////////////
    
    TrafficControlHelper tch;
    tch.SetRootQueueDisc(queue_disc);
    QueueDiscContainer queueDiscs1 = tch.Install(r1r2ND.Get(0)); // at R1
    QueueDiscContainer queueDiscs2 = tch.Install(r2r3ND.Get(0)); // at R2 
                                                                 
    Ptr<QueueDisc> queueDisc1 = queueDiscs1.Get(0);
    Ptr<QueueDisc> queueDisc2 = queueDiscs2.Get(0);

    queueDisc_router = queueDiscs1.Get(0);
    
    // setting Queue size to 1
    //SetQueueSize(2048);

    // Giving IP Address to each node
    Ipv4AddressHelper ipv4;
    ipv4.SetBase("172.16.1.0", "255.255.255.0");

    Ipv4InterfaceContainer r1r2Ip = ipv4.Assign(r1r2ND);
    Ipv4InterfaceContainer r2r3Ip = ipv4.Assign(r2r3ND);

    std::vector<Ipv4InterfaceContainer> S1Ip_v, S2Ip_v, S3Ip_v, D1Ip_v, D2Ip_v, D3Ip_v;
    for (uint32_t i = 0; i < nNodes; i++) {
        std::string ip = "10.1." + std::to_string(i) + ".0";
        ipv4.SetBase(ip.c_str(), "255.255.255.0");
        S1Ip_v.push_back(ipv4.Assign(S1ND_v[i]));

        ip = "10.1." + std::to_string(nNodes + i) + ".0";
        ipv4.SetBase(ip.c_str(), "255.255.255.0");
        S2Ip_v.push_back(ipv4.Assign(S2ND_v[i]));

        ip = "10.1." + std::to_string((2*nNodes) + i) + ".0";
        ipv4.SetBase(ip.c_str(), "255.255.255.0");
        S3Ip_v.push_back(ipv4.Assign(S3ND_v[i]));

        std::string ip2 = "10.1." + std::to_string((3*nNodes) + i) + ".0";
        ipv4.SetBase(ip2.c_str(), "255.255.255.0");
        D1Ip_v.push_back(ipv4.Assign(D1ND_v[i]));

        ip2 = "10.1." + std::to_string((4*nNodes) + i) + ".0";
        ipv4.SetBase(ip2.c_str(), "255.255.255.0");
        D2Ip_v.push_back(ipv4.Assign(D2ND_v[i]));

        ip2 = "10.1." + std::to_string((5*nNodes) + i) + ".0";
        ipv4.SetBase(ip2.c_str(), "255.255.255.0");
        D3Ip_v.push_back(ipv4.Assign(D3ND_v[i]));
    }

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    ////////////////////// PACKET SINK ON ALL DESTINATION NODE //////////////////////////
    // Attack sink to all nodes
    uint16_t port = 50000;
    PacketSinkHelper packetSinkHelper( "ns3::TcpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), port));
    Address sinkAddressD1[nNodes], sinkAddressD2[nNodes], sinkAddressD3[nNodes];
    ApplicationContainer sinkAppD1[nNodes], sinkAppD2[nNodes], sinkAppD3[nNodes];

    for (uint32_t i = 0; i < nNodes; i++) {
        sinkAddressD1[i] = *(new Address(InetSocketAddress(D1Ip_v[i].GetAddress(0), port)));
        sinkAppD1[i] = packetSinkHelper.Install(D1[i].Get(0));
        sinkAppD1[i].Start(Seconds(start_time));
        sinkAppD1[i].Stop(Seconds(stop_time));

        sinkAddressD2[i] = *(new Address(InetSocketAddress(D2Ip_v[i].GetAddress(0), port)));
        sinkAppD2[i] = packetSinkHelper.Install(D2[i].Get(0));
        sinkAppD2[i].Start(Seconds(start_time));
        sinkAppD2[i].Stop(Seconds(stop_time));

        sinkAddressD3[i] = *(new Address(InetSocketAddress(D3Ip_v[i].GetAddress(0), port)));
        sinkAppD3[i] = packetSinkHelper.Install(D3[i].Get(0));
        sinkAppD3[i].Start(Seconds(start_time));
        sinkAppD3[i].Stop(Seconds(stop_time));
    }

    double mean = 0.1; // more like a ~ 0.06
    double bound = 1;
    Ptr<ExponentialRandomVariable> expRandomVariable = CreateObject<ExponentialRandomVariable>();
    expRandomVariable->SetAttribute("Mean", DoubleValue(mean));
    expRandomVariable->SetAttribute("Bound", DoubleValue(bound));

    double stime = start_time;

    /////////////////// BULK SEND APPLICATION AT EACH SOURCE NODE /////////////////////////////
    // Installing BulkSend on each node on left
    Ptr<Socket> ns3TcpSocket[nNodes];
    ApplicationContainer sourceAppsS1[nNodes], sourceAppsS2[nNodes], sourceAppsS3[nNodes];
    // Configuring the application at each source node.
    for (uint32_t i = 0; i < nNodes; i++) {
        BulkSendHelper tmp_source1( "ns3::TcpSocketFactory", InetSocketAddress(D1Ip_v[i].GetAddress(0), port));
        // Set the amount of data to send in bytes.  Zero is unlimited.
        tmp_source1.SetAttribute("MaxBytes", UintegerValue(bytes_to_send));
        sourceAppsS1[i] = tmp_source1.Install(S1[i].Get(0));
        sourceAppsS1[i].Start(Seconds(stime));
        sourceAppsS1[i].Stop(Seconds(stop_time));

        BulkSendHelper tmp_source2( "ns3::TcpSocketFactory", InetSocketAddress(D2Ip_v[i].GetAddress(0), port));
        // Set the amount of data to send in bytes.  Zero is unlimited.
        tmp_source2.SetAttribute("MaxBytes", UintegerValue(bytes_to_send));
        sourceAppsS2[i] = tmp_source2.Install(S2[i].Get(0));
        sourceAppsS2[i].Start(Seconds(stime));
        sourceAppsS2[i].Stop(Seconds(stop_time));

        BulkSendHelper tmp_source3( "ns3::TcpSocketFactory", InetSocketAddress(D3Ip_v[i].GetAddress(0), port));
        // Set the amount of data to send in bytes.  Zero is unlimited.
        tmp_source3.SetAttribute("MaxBytes", UintegerValue(bytes_to_send));
        sourceAppsS3[i] = tmp_source3.Install(S3[i].Get(0));
        sourceAppsS3[i].Start(Seconds(stime));
        sourceAppsS3[i].Stop(Seconds(stop_time));

        double gap = expRandomVariable->GetValue();
        stime += gap;
    }
#if 1
    // tracing total data sent in bulksend
    if(bytes_to_send > 0)
    {
      std::vector<Ptr<BulkSendApplication>> bulkApps;
      for (uint32_t i = 0; i < nNodes; ++i)
      {
        for(uint32_t j = 0; j < sourceAppsS1[i].GetN(); ++j)
        {
          Ptr<BulkSendApplication> bulkApp = DynamicCast<BulkSendApplication>(sourceAppsS1[i].Get(j));
          bulkApps.push_back (bulkApp);
          // Bind client ID into the callback
          bulkApp->TraceConnectWithoutContext( "Tx", MakeBoundCallback(&TxTrace, i)); // trace total number of bytes sent so far
        }
      }
      Simulator::Schedule (Seconds (stime), &CheckCompletion, bulkApps);
    }

#endif

    // write parameters
    AsciiTraceHelper parameters_helper;

    parameters = parameters_helper.CreateFileStream(dir + parametersFileName + ".txt");
    *parameters->GetStream() << "regular cwnd sampling." << std::endl;
    *parameters->GetStream() << "Nodes : \t" << nNodes << std::endl;
    *parameters->GetStream() << "TotalSourceNodes: \t" << totalSourceNodes<< std::endl;
    *parameters->GetStream() << "TCP type id: \t" << tcp_type_id << std::endl;
    *parameters->GetStream() << "RTT : \t" << RTT << std::endl;
    *parameters->GetStream() << "Bottleneck1 Delay: \t" << bottleneck1_delay << std::endl;
    *parameters->GetStream() << "Bottleneck1 Bandwidth: \t" << bottleneck1_bandwidth << std::endl;
    *parameters->GetStream() << "Bottleneck2 Delay: \t" << bottleneck2_delay << std::endl;
    *parameters->GetStream() << "Bottleneck2 Bandwidth: \t" << bottleneck2_bandwidth << std::endl;
    *parameters->GetStream() << "Queue Disc: \t" << queue_disc << std::endl;
    *parameters->GetStream() << "Queue Size: \t" << queue_size << std::endl;
    *parameters->GetStream() << "Simulation Stop time: \t" << stop_time << std::endl;

    // Configuring file stream to write the Qsize
    AsciiTraceHelper ascii_qsize;
    qSize_stream = ascii_qsize.CreateFileStream(dir + qsize_trace_filename + ".txt");

    // trace traffic control qsize
    AsciiTraceHelper ascii_tc_qsize;
    tc_qSize_stream = ascii_tc_qsize.CreateFileStream(dir + tc_qsize_trace_filename + ".txt");

    // Configuring file stream to write the no of packets transmitted by the
    // bottleneck
    AsciiTraceHelper ascii_qsize_tx;
    bottleneckTransimittedStream = ascii_qsize_tx.CreateFileStream(dir + bottleneck_tx_filename + ".txt");

    AsciiTraceHelper ascii_dropped;
    dropped_stream = ascii_dropped.CreateFileStream(dir + dropped_trace_filename + ".txt");
    // start tracing the congestion window size and qSize

    Simulator::Schedule(Seconds(stime), &start_tracing_timeCwnd, nNodes);
    Simulator::Schedule(Seconds(stime), &StartTracingQueueSize);
    Simulator::Schedule(Seconds(stime), &StartTracingTransmitedPacket);
    Simulator::Schedule(Seconds(stime), &updateCwndValues, nNodes);
    //    Simulator::Schedule( Seconds(stime+start_tracing_time),
    //    &writeCwndToFile, nNodes);

    // start tracing Queue Size and Dropped Files
    Simulator::Schedule(Seconds(stime), &TraceDroppedPacket, dropped_trace_filename);
    // writing the congestion windows size, queue_size, packetTx to files
    // periodically ( 1 sec. )
    for (auto time = stime + start_tracing_time; time < stop_time; time += 0.1) {
        Simulator::Schedule(Seconds(time), &writeCwndToFile, nNodes);
        Simulator::Schedule(Seconds(time), &TraceQueueSizeTc, queueDisc1);
        Simulator::Schedule(Seconds(time), &TraceQueueSize);
        Simulator::Schedule(Seconds(time), &TraceBottleneckTx);
        Simulator::Schedule(Seconds(time), &TraceDroppedPkts);
    }

    if (enable_bot_trace == 1) {
        AsciiTraceHelper bottleneck_ascii;
        p2p_router1.EnableAscii(bottleneck_ascii.CreateFileStream(dir + "bottleneck-trace-router0.tr"), r1r2ND.Get(0));
    }

    if(enable_bot_pcap == 1) {
      // enable trace between two router links
      // enable promiscous mode
      p2p_router1.EnablePcap(dir + "/router-0" , r1r2ND.Get(0), true );
    }

    // Check for dropped packets using Flow Monitor
    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll();

    // Install mobility 
    MobilityHelper mobility;
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(nodes);
    
    // netanim 
    AnimationInterface anim(dir + "/parkinglot.xml");
    anim.SetConstantPosition(nodes.Get(0), 50, 150);   // R1
    anim.UpdateNodeDescription(nodes.Get(0), "R1");
    anim.UpdateNodeColor(nodes.Get(0), 255,0,0);   // routers red
                                                   
    anim.SetConstantPosition(nodes.Get(1), 150, 150);  // R2
    anim.UpdateNodeDescription(nodes.Get(1), "R2");
    anim.UpdateNodeColor(nodes.Get(1), 255,0,0);

    anim.SetConstantPosition(nodes.Get(2), 250, 150);  // R3
    anim.UpdateNodeDescription(nodes.Get(2), "R3");
    anim.UpdateNodeColor(nodes.Get(2), 255,0,0);
    for (uint32_t i = 0; i < nNodes; i++) {
        // S1 to R1
        anim.SetConstantPosition(nodes.Get(i+3), 20, 100 + i*5);

        anim.SetConstantPosition(nodes.Get(nNodes+i+3), 20, 300 + i*5);

        // S3 to R2
        anim.SetConstantPosition(nodes.Get(2*nNodes+i+3), 150, 50 + i*5);

        // D1 to R2
        anim.SetConstantPosition(nodes.Get(3*nNodes+i+3), 150, 350 + i*5);

        // D2 to R3
        anim.SetConstantPosition(nodes.Get(4*nNodes+i+3), 270, 100 + i*5);

        anim.SetConstantPosition(nodes.Get(5*nNodes+i+3), 270, 300 + i*5);
    }
    // enable packet visualtion 
    anim.EnablePacketMetadata(true);
    anim.SetMaxPktsPerTraceFile(500000); // const number of packet  
                                      
    Simulator::Stop(Seconds(stop_time + cleanup_time));
    Simulator::Run();
    monitor->SerializeToXmlFile(dir + "flowmonitor.xml", false, true);
    Simulator::Destroy();

    return 0;
}
