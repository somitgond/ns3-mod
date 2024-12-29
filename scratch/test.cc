#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/traffic-control-module.h"
#include "ns3/applications-module.h"
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
using namespace ns3;

// Threshold and increment values
const uint32_t threshold = 50;    // Packets
const uint32_t increment = 10;    // Packets

void AdjustQueueSize(Ptr<QueueDisc> queueDisc) {
    QueueSize currentSize = queueDisc->GetMaxSize();
    if (queueDisc->GetCurrentSize().GetValue() > threshold) {
        QueueSize newSize = QueueSize(currentSize.GetUnit(), currentSize.GetValue() + increment);
        queueDisc->SetMaxSize(newSize);
        NS_LOG_UNCOND("Queue size adjusted to " << newSize);
    }
}

void PeriodicQueueAdjustment(Ptr<QueueDisc> queueDisc, Time interval) {
    AdjustQueueSize(queueDisc);
    Simulator::Schedule(interval, &PeriodicQueueAdjustment, queueDisc, interval);
}

int main(int argc, char *argv[]) {
    // Set up logging
    // LogComponentEnable("AdjustQueueSize", LOG_LEVEL_INFO);

    // Create two nodes
    NodeContainer nodes;
    nodes.Create(2);

    // Set up a PointToPoint link
    PointToPointHelper pointToPoint;
    pointToPoint.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
    pointToPoint.SetChannelAttribute("Delay", StringValue("5ms"));
    NetDeviceContainer devices = pointToPoint.Install(nodes);

    // Install an internet stack
    InternetStackHelper stack;
    stack.Install(nodes);

    // Assign IP addresses
    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces = address.Assign(devices);
    /////////////////////////////////////////////////////
    /////////////// Traffic Controller //////////////////
    // Remove any existing queue disc that might be installed
    for (NetDeviceContainer::Iterator i = devices.Begin(); i != devices.End(); ++i) {
        Ptr<NetDevice> device = *i;
        Ptr<TrafficControlLayer> tcLayer = device->GetNode()->GetObject<TrafficControlLayer>();

        if (tcLayer != nullptr) {
            Ptr<QueueDisc> rootDisc = tcLayer->GetRootQueueDiscOnDevice(device);
            if (rootDisc != nullptr) {
                tcLayer->DeleteRootQueueDiscOnDevice(device);  // Remove existing queue disc
            }
        }
    }
    // Install a Traffic Control Helper with a FIFO queue discipline
    TrafficControlHelper tch;
    tch.SetRootQueueDisc("ns3::FifoQueueDisc", "MaxSize", StringValue("100p"));
    QueueDiscContainer queueDiscs = tch.Install(devices.Get(0));

    Ptr<QueueDisc> queueDisc = queueDiscs.Get(0);

    // Schedule periodic queue size adjustments
    Time adjustmentInterval = Seconds(1.0);
    Simulator::Schedule(adjustmentInterval, &PeriodicQueueAdjustment, queueDisc, adjustmentInterval);

    // Install a traffic-generating application
    uint16_t port = 9; // Port for UDP traffic
    OnOffHelper onOffHelper("ns3::UdpSocketFactory", InetSocketAddress(interfaces.GetAddress(1), port));
    onOffHelper.SetAttribute("DataRate", StringValue("5Mbps"));
    onOffHelper.SetAttribute("PacketSize", UintegerValue(1024));
    ApplicationContainer app = onOffHelper.Install(nodes.Get(0));
    app.Start(Seconds(1.0));
    app.Stop(Seconds(10.0));

    // Install a packet sink on the receiving node
    PacketSinkHelper sinkHelper("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), port));
    ApplicationContainer sinkApp = sinkHelper.Install(nodes.Get(1));
    sinkApp.Start(Seconds(1.0));
    sinkApp.Stop(Seconds(10.0));

    // Run the simulation
    Simulator::Run();
    Simulator::Destroy();

    return 0;
}
