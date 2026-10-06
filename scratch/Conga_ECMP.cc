/**
 * @file
 * @brief Trace-driven leaf-spine experiment for CONGA and its ECMP variants.
 *
 * The program builds the topology, reads one CSV workload, installs one TCP
 * BulkSend/PacketSink pair per input row, and writes FlowMonitor statistics.
 * Select the routing behavior with --runMode. The established defaults and
 * command-line names are deliberately retained so historical experiments can
 * be replayed without changing their configuration.
 */

#include "trace-delivery.h"

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/ipv4-conga-routing-helper.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/ipv4-queue-disc-item.h"
#include "ns3/ipv4-static-routing-helper.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/tcp-header.h"
#include "ns3/traffic-control-module.h"

#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace ns3;

// Packet-drop counters reported in the output CSV.  The map key is the ns-3
// node ID; nodes with no drops are emitted as zeroes in the summary.

std::map<uint32_t, uint64_t> nodeTxPackets;
std::map<uint32_t, uint64_t> nodeRxPackets;
std::map<uint32_t, uint64_t> nodeLostPackets;

std::map<uint32_t, uint64_t> macTxDrops;
std::map<uint32_t, uint64_t> phyTxDrops;
std::map<uint32_t, uint64_t> phyRxDrops;

std::ofstream queueTrace;
uint16_t queueTracePort;

/**
 * Record a TCP packet entering or leaving a root queue disc.
 *
 * The trace is filtered to the destination port assigned to the selected
 * input row, so a focused investigation does not emit every queue event.
 *
 * @param queueDisc Root queue disc that emitted the event.
 * @param nodeId Node containing the queue disc.
 * @param deviceId Device containing the queue disc.
 * @param event Queue-disc event name.
 * @param item Packet involved in the event.
 */
void
QueueDiscTrace(Ptr<QueueDisc> queueDisc,
               uint32_t nodeId,
               uint32_t deviceId,
               const std::string& event,
               Ptr<const QueueDiscItem> item)
{
    Ptr<const Ipv4QueueDiscItem> ipv4Item = DynamicCast<const Ipv4QueueDiscItem>(item);
    if (!ipv4Item || ipv4Item->GetHeader().GetProtocol() != 6)
    {
        return;
    }

    TcpHeader tcp;
    Ptr<Packet> packet = ipv4Item->GetPacket()->Copy();
    if (packet->PeekHeader(tcp) == 0)
    {
        return;
    }
    if (tcp.GetSourcePort() != queueTracePort && tcp.GetDestinationPort() != queueTracePort)
    {
        return;
    }

    const Ipv4Header& ipv4 = ipv4Item->GetHeader();
    queueTrace << Simulator::Now().GetNanoSeconds() << ',' << event << ',' << nodeId << ','
               << deviceId << ',' << queueDisc->GetNPackets() << ',' << queueDisc->GetNBytes()
               << ',' << packet->GetUid() << ',' << ipv4Item->GetSize() << ','
               << ipv4.GetIdentification() << ',' << ipv4.GetSource() << ','
               << ipv4.GetDestination() << ',' << tcp.GetSourcePort() << ','
               << tcp.GetDestinationPort() << ',' << static_cast<uint32_t>(tcp.GetFlags()) << ','
               << tcp.GetSequenceNumber().GetValue() << ',' << tcp.GetAckNumber().GetValue()
               << '\n';
}

/** Extract the numeric node ID embedded in an ns-3 trace context path. */
uint32_t
ExtractNodeId(const std::string& context)
{
    // Trace paths encode the node immediately after "/NodeList/".
    size_t n1 = context.find("/NodeList/");
    if (n1 == std::string::npos)
    {
        return UINT32_MAX;
    }

    n1 += 10; // length of "/NodeList/"
    size_t n2 = context.find("/", n1);
    if (n2 == std::string::npos)
    {
        return UINT32_MAX;
    }

    std::string idStr = context.substr(n1, n2 - n1);

    if (idStr.empty() || idStr.find_first_not_of("0123456789") != std::string::npos)
    {
        return UINT32_MAX;
    }

    return static_cast<uint32_t>(std::stoul(idStr));
}

// Drop callbacks update the counters written to the experiment CSV.

/** Record a MAC transmit drop for the node named by a trace context. */
void
MacTxDropSink(std::string context, Ptr<const Packet> p)
{
    uint32_t node = ExtractNodeId(context);
    macTxDrops[node]++;

    NS_LOG_UNCOND("[DROP] MacTxDrop @ node " << node << ", UID=" << p->GetUid());
}

/** Record a PHY transmit drop for the node named by a trace context. */
void
PhyTxDropSink(std::string context, Ptr<const Packet> p)
{
    uint32_t node = ExtractNodeId(context);
    phyTxDrops[node]++;

    NS_LOG_UNCOND("[DROP] PhyTxDrop @ node " << node << ", UID=" << p->GetUid());
}

/** Record a PHY receive drop for the node named by a trace context. */
void
PhyRxDropSink(std::string context, Ptr<const Packet> p)
{
    uint32_t node = ExtractNodeId(context);
    phyRxDrops[node]++;

    NS_LOG_UNCOND("[DROP] PhyRxDrop @ node " << node << ", UID=" << p->GetUid());
}

std::map<uint32_t, uint64_t> nodeQueueDrops;

/** Summary of a root queue disc associated with one local network device. */
struct RootQueueDiscSummary
{
    std::string type; ///< Concrete queue-disc type.
    uint64_t droppedPackets{0}; ///< Total packets dropped by the queue disc.
    uint64_t droppedBeforeEnqueue{0}; ///< Packets rejected before enqueue.
    uint64_t droppedAfterDequeue{0}; ///< Packets dropped by the active queue algorithm.
    uint64_t droppedBytes{0}; ///< Total dropped bytes.
    std::map<std::string, uint32_t, std::less<>> droppedBeforeEnqueueByReason; ///< Rejections by reason.
    std::map<std::string, uint32_t, std::less<>> droppedAfterDequeueByReason; ///< Active drops by reason.
};

// Root queue-disc drops are separate from a point-to-point device TxQueue drop.
std::map<uint32_t, uint64_t> nodeRootQueueDiscDrops;
std::map<std::tuple<uint32_t, uint32_t, Ipv4Address>, RootQueueDiscSummary> rootQueueDiscDrops;

// Queue drops grouped by local node, device, and interface address.
std::map<std::tuple<uint32_t, uint32_t, Ipv4Address>, uint64_t> queuedroplink;

/** Record a device transmit-queue drop by node, device, and local address. */
void
TxQueueDropCallback(std::string context, Ptr<const Packet> p)
{
    size_t n1 = context.find("/NodeList/");
    size_t d1 = context.find("/DeviceList/");

    if (n1 == std::string::npos || d1 == std::string::npos)
    {
        return;
    }

    uint32_t nodeId = std::stoul(context.substr(n1 + 10, context.find("/", n1 + 10) - (n1 + 10)));
    uint32_t devId = std::stoul(context.substr(d1 + 12, context.find("/", d1 + 12) - (d1 + 12)));

    Ptr<Node> node = NodeList::GetNode(nodeId);
    Ptr<NetDevice> device = node->GetDevice(devId);

    Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
    int32_t interfaceIndex = ipv4->GetInterfaceForDevice(device);

    Ipv4Address addr;

    if (interfaceIndex != -1)
    {
        addr = ipv4->GetAddress(interfaceIndex, 0).GetLocal();
    }

    // Keep both summaries in sync: one is per node and one is per link.
    nodeQueueDrops[nodeId]++;
    queuedroplink[std::make_tuple(nodeId, devId, addr)]++;

    NS_LOG_UNCOND("[DROP] QueueDrop @ node " << nodeId << ", UID=" << p->GetUid());
}

/**
 * Return the IPv4 address assigned to one local device.
 *
 * @param node Node that owns the device.
 * @param deviceId Device index on @p node.
 * @return The first IPv4 address on the device, or the unspecified address.
 */
Ipv4Address
GetDeviceAddress(Ptr<Node> node, uint32_t deviceId)
{
    Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
    int32_t interfaceIndex = ipv4->GetInterfaceForDevice(node->GetDevice(deviceId));
    return interfaceIndex == -1 ? Ipv4Address() : ipv4->GetAddress(interfaceIndex, 0).GetLocal();
}

/**
 * Collect root queue-disc drop counters after a simulation has finished.
 *
 * ns-3 automatically installs FqCoDel root queue discs on supported devices.
 * Their drops occur before the point-to-point device TxQueue and therefore do
 * not appear in the device queue's Drop trace.
 */
void
CollectRootQueueDiscDrops()
{
    rootQueueDiscDrops.clear();
    nodeRootQueueDiscDrops.clear();

    for (uint32_t nodeId = 0; nodeId < NodeList::GetNNodes(); ++nodeId)
    {
        Ptr<Node> node = NodeList::GetNode(nodeId);
        Ptr<TrafficControlLayer> trafficControl = node->GetObject<TrafficControlLayer>();
        if (!trafficControl)
        {
            continue;
        }

        for (uint32_t deviceId = 0; deviceId < node->GetNDevices(); ++deviceId)
        {
            Ptr<QueueDisc> rootQueueDisc =
                trafficControl->GetRootQueueDiscOnDevice(node->GetDevice(deviceId));
            if (!rootQueueDisc)
            {
                continue;
            }

            const QueueDisc::Stats& stats = rootQueueDisc->GetStats();
            RootQueueDiscSummary summary;
            summary.type = rootQueueDisc->GetInstanceTypeId().GetName();
            summary.droppedPackets = stats.nTotalDroppedPackets;
            summary.droppedBeforeEnqueue = stats.nTotalDroppedPacketsBeforeEnqueue;
            summary.droppedAfterDequeue = stats.nTotalDroppedPacketsAfterDequeue;
            summary.droppedBytes = stats.nTotalDroppedBytes;
            summary.droppedBeforeEnqueueByReason = stats.nDroppedPacketsBeforeEnqueue;
            summary.droppedAfterDequeueByReason = stats.nDroppedPacketsAfterDequeue;

            rootQueueDiscDrops[std::make_tuple(nodeId, deviceId, GetDeviceAddress(node, deviceId))] =
                summary;
            nodeRootQueueDiscDrops[nodeId] += summary.droppedPackets;
        }
    }
}

/**
 * Map each configured IPv4 address to its owning ns-3 node ID.
 *
 * @return Mapping keyed by the raw IPv4 address value.
 */
std::map<uint32_t, uint32_t>
BuildAddressToNodeMap()
{
    std::map<uint32_t, uint32_t> result;
    for (uint32_t nodeId = 0; nodeId < NodeList::GetNNodes(); ++nodeId)
    {
        Ptr<Ipv4> ipv4 = NodeList::GetNode(nodeId)->GetObject<Ipv4>();
        if (!ipv4)
        {
            continue;
        }

        for (uint32_t interfaceId = 0; interfaceId < ipv4->GetNInterfaces(); ++interfaceId)
        {
            for (uint32_t addressId = 0; addressId < ipv4->GetNAddresses(interfaceId); ++addressId)
            {
                result[ipv4->GetAddress(interfaceId, addressId).GetLocal().Get()] = nodeId;
            }
        }
    }
    return result;
}

/**
 * Find the node that owns an address reported by FlowMonitor.
 *
 * @param addressToNode Mapping built from configured IPv4 interfaces.
 * @param address FlowMonitor endpoint address.
 * @return The owning ns-3 node ID.
 */
uint32_t
GetNodeIdForAddress(const std::map<uint32_t, uint32_t>& addressToNode, Ipv4Address address)
{
    auto it = addressToNode.find(address.Get());
    NS_ABORT_MSG_IF(it == addressToNode.end(), "No node owns FlowMonitor address " << address);
    return it->second;
}

/**
 * Print point-to-point queue occupancy and schedule the next sample.
 *
 * @param nodes Servers whose device queues are printed.
 * @param interval Time between progress samples.
 */
void
PrintSimTime(NodeContainer nodes, Time interval)
{
    std::cout << "Time: " << Simulator::Now().GetSeconds() << "s" << std::endl;

    for (uint32_t i = 0; i < nodes.GetN(); i++)
    {
        Ptr<Node> node = nodes.Get(i);

        for (uint32_t d = 0; d < node->GetNDevices(); d++)
        {
            Ptr<PointToPointNetDevice> dev = DynamicCast<PointToPointNetDevice>(node->GetDevice(d));

            if (dev)
            {
                auto queue = dev->GetQueue();

                std::cout << "Node " << i << " Device " << d
                          << " QueuePackets: " << queue->GetNPackets() << std::endl;
            }
        }
    }

    Simulator::Schedule(interval, &PrintSimTime, nodes, interval);
}

// Link-utilization accounting is restricted to [utilStart, utilEnd].

std::map<std::tuple<uint32_t, uint32_t, Ipv4Address>, uint64_t> bytesPerLink;

double utilStart = 0.0;
double utilEnd = 10.0; // Existing workloads retain the 0-10 s window by default.

/** Count bytes transmitted by each interface during the utilization window. */
void
LinkTxTrace(std::string context, Ptr<const Packet> p)
{
    // Attribute bytes to the local address of the transmitting device.
    double now = Simulator::Now().GetSeconds();
    if (now < utilStart || now > utilEnd)
    {
        return;
    }

    size_t n1 = context.find("/NodeList/");
    size_t d1 = context.find("/DeviceList/");

    if (n1 == std::string::npos || d1 == std::string::npos)
    {
        return;
    }

    uint32_t nodeId = std::stoul(context.substr(n1 + 10, context.find("/", n1 + 10) - (n1 + 10)));
    uint32_t devId = std::stoul(context.substr(d1 + 12, context.find("/", d1 + 12) - (d1 + 12)));

    Ptr<Node> node = NodeList::GetNode(nodeId);
    Ptr<NetDevice> device = node->GetDevice(devId);

    Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
    int32_t interfaceIndex = ipv4->GetInterfaceForDevice(device);

    Ipv4Address addr;

    if (interfaceIndex != -1)
    {
        addr = ipv4->GetAddress(interfaceIndex, 0).GetLocal();
    }

    bytesPerLink[std::make_tuple(nodeId, devId, addr)] += p->GetSize();
}

#define LINK_CAPACITY_BASE 1000000000 // 1Gbps
#define BUFFER_SIZE 250               // 250 packets

// Each workload row maps to a deterministic TCP destination port in this range.
#define PORT_START 10000
#define PORT_END 50000

#define PACKET_SIZE 1400

NS_LOG_COMPONENT_DEFINE("CongaSimulationLarge");

/** Routing modes retained from the original CONGA experiment driver. */
enum RunMode
{
    TLB,
    CONGA,
    CONGA_FLOW,
    CONGA_ECMP,
    ECMP
};

/** One input-workload row used to install a TCP application pair. */
struct Flow
{
    int index;
    std::string flowid;
    int src;
    int dst;
    uint64_t size;
    double start;
};

/**
 * Install all workload rows whose source server belongs to one leaf.
 *
 * Each row uses a deterministic port derived from its original trace index.
 * When enabled, trace_delivery observes the source Tx and sink Rx events
 * separately from FlowMonitor so delivered application bytes can be audited.
 */
void
install_applications(int fromLeafId,
                     NodeContainer servers,
                     const std::vector<Flow>& flows,
                     long& flowCount,
                     long& totalFlowSize,
                     int SERVER_COUNT,
                     int LEAF_COUNT,
                     double START_TIME,
                     double END_TIME,
                     bool traceDelivery)
{
    NS_LOG_INFO("Install applications:");
    std::cout << "Installing applications\n";

    for (int i = 0; i < SERVER_COUNT; i++)
    {
        int fromServerIndex = fromLeafId * SERVER_COUNT + i;

        for (const auto& flow : flows)
        {
            if (flow.src != fromServerIndex)
            {
                continue;
            }

            uint16_t port = PORT_START + flow.index;

            if (port > PORT_END)
            {
                port = port - (PORT_END - PORT_START);
            }

            Ptr<Node> destServer = servers.Get(flow.dst);
            Ptr<Ipv4> ipv4 = destServer->GetObject<Ipv4>();
            Ipv4Address destAddress = ipv4->GetAddress(1, 0).GetLocal();

            BulkSendHelper source("ns3::TcpSocketFactory", InetSocketAddress(destAddress, port));

            source.SetAttribute("SendSize", UintegerValue(PACKET_SIZE));
            source.SetAttribute("MaxBytes", UintegerValue(flow.size));

            ApplicationContainer sourceApp = source.Install(servers.Get(fromServerIndex));
            if (traceDelivery)
            {
                sourceApp.Get(0)->TraceConnectWithoutContext(
                    "Tx",
                    MakeBoundCallback(&trace_delivery::Sent, flow.index));
                sourceApp.Get(0)->TraceConnectWithoutContext(
                    "TcpRetransmission",
                    MakeBoundCallback(&trace_delivery::Retransmitted, flow.index));
            }
            sourceApp.Start(Seconds(flow.start));
            sourceApp.Stop(Seconds(END_TIME - 0.5));

            PacketSinkHelper sink("ns3::TcpSocketFactory",
                                  InetSocketAddress(Ipv4Address::GetAny(), port));

            ApplicationContainer sinkApp = sink.Install(destServer);
            sinkApp.Start(Seconds(START_TIME));
            // Keep the receiver alive while the sender drains its TCP buffer.
            sinkApp.Stop(Seconds(END_TIME));
            if (traceDelivery)
            {
                trace_delivery::Track(flow, port, sinkApp.Get(0));
            }

            flowCount++;
            totalFlowSize += flow.size;
        }
    }

    std::cout << "Install Complete\n";
}

/** Read the first six columns of a workload CSV into application descriptors. */
std::vector<Flow>
readFlowsFromCSV(const std::string& filePath)
{
    std::ifstream file(filePath);
    std::vector<Flow> flows;
    if (!file.is_open())
    {
        throw std::runtime_error("Could not open CSV file.");
    }
    std::cout << "Opened File\n";
    std::string line;
    std::getline(file, line); // Skip header
    while (std::getline(file, line))
    {
        std::stringstream ss(line);
        std::string token;
        Flow flow;
        // Column 1: index
        std::getline(ss, token, ',');
        flow.index = std::stoi(token);
        // Column 2: flowid
        std::getline(ss, token, ',');
        flow.flowid = token;
        // Column 3: src
        std::getline(ss, token, ',');
        flow.src = std::stoi(token);
        // Column 4: dst
        std::getline(ss, token, ',');
        flow.dst = std::stoi(token);
        // Column 5: size
        std::getline(ss, token, ',');
        flow.size = std::stoull(token);
        // Column 6: start time
        std::getline(ss, token, ',');
        flow.start = std::stod(token);
        std::getline(ss, token, ','); // Establishment column.
        std::getline(ss, token, ','); // Second-index column.
        flows.push_back(flow);
    }
    file.close();
    std::cout << "Closed File\n";
    return flows;
}

/** Print non-loopback addresses assigned to every node in a container. */
void
PrintNodeContainerIPs(std::string name, NodeContainer container)
{
    std::cout << "\n===== " << name << " =====\n";

    for (uint32_t i = 0; i < container.GetN(); i++)
    {
        Ptr<Node> node = container.Get(i);
        Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();

        std::cout << name << " Node Index: " << i << std::endl;

        for (uint32_t iface = 0; iface < ipv4->GetNInterfaces(); iface++)
        {
            for (uint32_t addrIndex = 0; addrIndex < ipv4->GetNAddresses(iface); addrIndex++)
            {
                Ipv4Address addr = ipv4->GetAddress(iface, addrIndex).GetLocal();

                // Skip loopback
                if (addr != Ipv4Address("127.0.0.1"))
                {
                    std::cout << "   Interface " << iface << " IP: " << addr << std::endl;
                }
            }
        }
    }
}

/**
 * Build and run one CONGA/ECMP experiment.
 *
 * @param argc Number of command-line arguments.
 * @param argv Command-line arguments.
 * @return Zero after the simulator and output files are finalized.
 */
int
main(int argc, char* argv[])
{
#if 1
    LogComponentEnable("CongaSimulationLarge", LOG_LEVEL_INFO);
#endif

    std::string inputfile;
    std::string outputfile;
    bool powerof2 = false;

    std::string id = "0";
    std::string runModeStr = "Conga";
    unsigned randomSeed = 0;
    std::string transportProt = "Tcp";

    double START_TIME = 0.0;
    double END_TIME = 50;
    // These defaults are the validated comparison baseline: allow queued
    // transfers to drain and keep ConnTimeout above the modeled queue delay.
    double drainTime = 30;
    double fixedEndTime = -1;
    std::string packetTrace;
    std::string applicationTrace;
    std::string queueTracePath;
    double progressInterval = 0.1;
    bool traceDelivery = true;

    uint32_t linkLatency = 10;

    bool asymCapacity = false;

    uint32_t asymCapacityPoss = 40; // 40 %

    int SERVER_COUNT = 2;
    int SPINE_COUNT = 2;
    int LEAF_COUNT = 2;
    int LINK_COUNT = 1;

    double spineLeafCapacity = 0.1;
    double leafServerCapacity = 0.1;

    bool enableLargeDupAck = false;

    uint32_t congaFlowletTimeout = 500;

    // A reduced physical link must use its reduced capacity in CONGA's DRE
    // quantization.  Otherwise CONGA underreports its congestion and can
    // repeatedly select the bottleneck path.
    bool congaAwareAsym = true;
    bool asymCapacity2 = false;
    int asym = 0;

    bool enableLargeSynRetries = false;

    bool enableLargeDataRetries = false;

    bool enableFastReConnection = false;
    double tcpConnTimeoutMs = 3000;
    bool tcpSack = true;

    CommandLine cmd;
    cmd.AddValue("applicationTrace",
                 "Optional application send/receive CSV event path",
                 applicationTrace);
    cmd.AddValue("traceDelivery",
                 "Track per-flow application delivery and write .flows.csv and its audit",
                 traceDelivery);
    cmd.AddValue("traceIndex",
                 "Original trace row to log; -1 logs all rows",
                 trace_delivery::selectedIndex);
    cmd.AddValue("fixedEndTime",
                 "Absolute simulation deadline for workload reduction",
                 fixedEndTime);
    cmd.AddValue("packetTrace", "Optional point-to-point ASCII packet trace path", packetTrace);
    cmd.AddValue("queueTrace", "Optional root queue-disc CSV path for traceIndex", queueTracePath);
    cmd.AddValue("progressInterval",
                 "Queue-progress print interval in seconds; zero disables progress output",
                 progressInterval);
    cmd.AddValue("utilEnd", "End of the link-utilization window in seconds", utilEnd);
    cmd.AddValue("drainTime",
                 "Seconds after the last flow start; -1 keeps the legacy deadline",
                 drainTime);
    cmd.AddValue("inputFile", "Input file path", inputfile);
    cmd.AddValue("outputFile", "Output file path", outputfile);
    cmd.AddValue("powerof2", "Enable Power of 2 for Conga", powerof2);

    cmd.AddValue("ID", "Running ID", id);
    cmd.AddValue("StartTime", "Start time of the simulation", START_TIME);
    cmd.AddValue("runMode", "Running mode of this simulation: Conga, Conga-flow, ECMP", runModeStr);
    cmd.AddValue("randomSeed",
                 "C library seed for CONGA tie-breaking; zero retains the default sequence",
                 randomSeed);
    cmd.AddValue("transportProt", "Transport protocol to use: Tcp", transportProt);
    cmd.AddValue("linkLatency", "Link latency, should be in MicroSeconds", linkLatency);

    cmd.AddValue("asymCapacity",
                 "Whether the capacity is asym, which means some link will have only 1/2 the "
                 "capacity of others",
                 asymCapacity);
    cmd.AddValue("asymCapacityPoss",
                 "The possibility that a path will have only 1/10 capacity",
                 asymCapacityPoss);

    cmd.AddValue("serverCount", "The Server count", SERVER_COUNT);
    cmd.AddValue("spineCount", "The Spine count", SPINE_COUNT);
    cmd.AddValue("leafCount", "The Leaf count", LEAF_COUNT);
    cmd.AddValue("linkCount", "The Link count", LINK_COUNT);

    cmd.AddValue("spineLeafCapacity", "Spine <-> Leaf capacity in Gbps", spineLeafCapacity);
    cmd.AddValue("leafServerCapacity", "Leaf <-> Server capacity in Gbps", leafServerCapacity);

    cmd.AddValue("enableLargeDupAck",
                 "Whether to set the ReTxThreshold to a very large value to mask reordering",
                 enableLargeDupAck);

    cmd.AddValue("congaFlowletTimeout", "Flowlet timeout in Conga", congaFlowletTimeout);

    cmd.AddValue("congaAwareAsym",
                 "Use each asymmetric link's physical capacity in CONGA's congestion metric",
                 congaAwareAsym);

    cmd.AddValue("asymCapacity2",
                 "Whether the Spine0-Leaf0's capacity is asymmetric",
                 asymCapacity2);
    cmd.AddValue("asym",
                 "Add the number of the leaf-spine link to fail (Range: 1 - LEAF_COUNT*SPINE_COUNT "
                 "  (Leaf Major))",
                 asym);

    cmd.AddValue("enableLargeSynRetries",
                 "Whether the SYN packet would retry thousands of times",
                 enableLargeSynRetries);
    cmd.AddValue("enableFastReConnection",
                 "Whether the SYN gap will be very small when reconnecting",
                 enableFastReConnection);
    cmd.AddValue("enableLargeDataRetries",
                 "Whether the data retransmission will be more than 6 times",
                 enableLargeDataRetries);
    cmd.AddValue("tcpConnTimeoutMs",
                 "TCP connection timeout in milliseconds; negative uses the legacy 5 ms setting",
                 tcpConnTimeoutMs);
    cmd.AddValue("tcpSack", "Enable TCP selective acknowledgments", tcpSack);

    cmd.Parse(argc, argv);
    NS_ABORT_MSG_IF(!(utilEnd > utilStart), "utilEnd must be after utilStart");
    NS_ABORT_MSG_IF(!traceDelivery && !applicationTrace.empty(),
                    "applicationTrace requires traceDelivery=true");

    // CONGA selects among equally good ports with std::rand().  Preserve the
    // historical default sequence when zero is supplied, while allowing CDF
    // experiment scripts to create reproducible routing-seed replicates.
    if (randomSeed != 0)
    {
        std::srand(randomSeed);
    }

    uint64_t SPINE_LEAF_CAPACITY = spineLeafCapacity * LINK_CAPACITY_BASE;
    uint64_t LEAF_SERVER_CAPACITY = leafServerCapacity * LINK_CAPACITY_BASE;
    Time LINK_LATENCY = MicroSeconds(linkLatency);

    RunMode runMode;
    if (runModeStr.compare("Conga") == 0)
    {
        runMode = CONGA;
    }
    else if (runModeStr.compare("Conga-flow") == 0)
    {
        runMode = CONGA_FLOW;
    }
    else if (runModeStr.compare("Conga-ECMP") == 0)
    {
        runMode = CONGA_ECMP;
    }
    else if (runModeStr.compare("ECMP") == 0)
    {
        runMode = ECMP;
    }
    else
    {
        NS_LOG_ERROR("The running mode should be TLB, Conga, Conga-flow, Conga-ECMP, Presto, "
                     "FlowBender, DRB and ECMP");
        return 0;
    }

    NS_LOG_INFO("Config parameters");
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(PACKET_SIZE));
    Config::SetDefault("ns3::TcpSocket::DelAckCount", UintegerValue(0));
    Config::SetDefault("ns3::TcpSocketBase::Sack", BooleanValue(tcpSack));
    if (tcpConnTimeoutMs >= 0)
    {
        Config::SetDefault("ns3::TcpSocket::ConnTimeout",
                           TimeValue(MilliSeconds(tcpConnTimeoutMs)));
    }
    else if (enableFastReConnection)
    {
        Config::SetDefault("ns3::TcpSocket::ConnTimeout", TimeValue(MicroSeconds(40)));
    }
    else
    {
        Config::SetDefault("ns3::TcpSocket::ConnTimeout", TimeValue(MilliSeconds(5)));
    }
    Config::SetDefault("ns3::TcpSocket::InitialCwnd", UintegerValue(10));
    Config::SetDefault("ns3::TcpSocketBase::MinRto", TimeValue(MilliSeconds(5)));
    Config::SetDefault("ns3::TcpSocketBase::ClockGranularity", TimeValue(MicroSeconds(100)));
    Config::SetDefault("ns3::RttEstimator::InitialEstimation", TimeValue(MicroSeconds(80)));

    if (enableLargeDupAck)
    {
        Config::SetDefault("ns3::TcpSocketBase::ReTxThreshold", UintegerValue(1000));
    }

    if (enableLargeSynRetries)
    {
        Config::SetDefault("ns3::TcpSocket::ConnCount", UintegerValue(10000));
    }

    if (enableLargeDataRetries)
    {
        Config::SetDefault("ns3::TcpSocket::DataRetries", UintegerValue(10000));
    }

    NodeContainer spines;
    spines.Create(SPINE_COUNT);
    NodeContainer leaves;
    leaves.Create(LEAF_COUNT);
    NodeContainer servers;
    servers.Create(SERVER_COUNT * LEAF_COUNT);

    std::cout << "Created Nodes\n";

    NS_LOG_INFO("Install Internet stacks");
    InternetStackHelper internet;
    Ipv4StaticRoutingHelper staticRoutingHelper;
    Ipv4CongaRoutingHelper congaRoutingHelper;
    Ipv4GlobalRoutingHelper globalRoutingHelper;

    if (runMode == CONGA || runMode == CONGA_FLOW || runMode == CONGA_ECMP)
    {
        internet.SetRoutingHelper(staticRoutingHelper);
        internet.Install(servers);

        internet.SetRoutingHelper(congaRoutingHelper);
        internet.Install(spines);
        internet.Install(leaves);

        std::cout << "RUNMODE CONGA selected\n";
    }
    else if (runMode == ECMP)
    {
        internet.SetRoutingHelper(globalRoutingHelper);
        Config::SetDefault("ns3::Ipv4GlobalRouting::FlowEcmpRouting", BooleanValue(true));

        internet.Install(servers);
        internet.Install(spines);
        internet.Install(leaves);
    }

    NS_LOG_INFO("Install channels and assign addresses");

    PointToPointHelper p2p;
    Ipv4AddressHelper ipv4;

    NS_LOG_INFO("Configuring servers");

    std::cout << "Configuring servers" << std::endl;
    p2p.SetDeviceAttribute("DataRate", DataRateValue(DataRate(LEAF_SERVER_CAPACITY)));
    p2p.SetChannelAttribute("Delay", TimeValue(LINK_LATENCY));
    if (transportProt.compare("Tcp") == 0)
    {
        p2p.SetQueue("ns3::DropTailQueue<Packet>",
                     "MaxSize",
                     QueueSizeValue(QueueSize(std::to_string(BUFFER_SIZE) + "p")));
    }

    ipv4.SetBase("10.1.0.0", "255.255.255.0");

    std::vector<Ipv4Address> leafNetworks(LEAF_COUNT);

    for (int i = 0; i < LEAF_COUNT; i++)
    {
        Ipv4Address network = ipv4.NewNetwork();
        leafNetworks[i] = network;

        for (int j = 0; j < SERVER_COUNT; j++)
        {
            int serverIndex = i * SERVER_COUNT + j;
            NodeContainer nodeContainer = NodeContainer(leaves.Get(i), servers.Get(serverIndex));
            NetDeviceContainer netDeviceContainer = p2p.Install(nodeContainer);

            Ipv4InterfaceContainer interfaceContainer = ipv4.Assign(netDeviceContainer);

            NS_LOG_INFO("Leaf - " << i << " is connected to Server - " << j << " with address "
                                  << interfaceContainer.GetAddress(0) << " <-> "
                                  << interfaceContainer.GetAddress(1) << " with port "
                                  << netDeviceContainer.Get(0)->GetIfIndex() << " <-> "
                                  << netDeviceContainer.Get(1)->GetIfIndex());

            if (runMode == CONGA || runMode == CONGA_FLOW || runMode == CONGA_ECMP)
            {
                // All servers just forward the packet to leaf switch
                staticRoutingHelper.GetStaticRouting(servers.Get(serverIndex)->GetObject<Ipv4>())
                    ->AddNetworkRouteTo(Ipv4Address("0.0.0.0"),
                                        Ipv4Mask("0.0.0.0"),
                                        netDeviceContainer.Get(1)->GetIfIndex());
                // Conga leaf switches forward the packet to the correct servers
                congaRoutingHelper.GetCongaRouting(leaves.Get(i)->GetObject<Ipv4>())
                    ->AddRoute(interfaceContainer.GetAddress(1),
                               Ipv4Mask("255.255.255.255"),
                               netDeviceContainer.Get(0)->GetIfIndex());
                for (int k = 0; k < LEAF_COUNT; k++)
                {
                    congaRoutingHelper.GetCongaRouting(leaves.Get(k)->GetObject<Ipv4>())
                        ->AddAddressToLeafIdMap(interfaceContainer.GetAddress(1), i);
                }
                std::cout << "CONGA Routing Table created\n";
            }
        }
    }

    NS_LOG_INFO("Configuring switches");
    std::cout << "Configuring switches\n";
    p2p.SetDeviceAttribute("DataRate", DataRateValue(DataRate(SPINE_LEAF_CAPACITY)));

    for (int i = 0; i < LEAF_COUNT; i++)
    {
        if (runMode == CONGA || runMode == CONGA_FLOW || runMode == CONGA_ECMP)
        {
            Ptr<Ipv4CongaRouting> congaLeaf =
                congaRoutingHelper.GetCongaRouting(leaves.Get(i)->GetObject<Ipv4>());
            congaLeaf->SetLeafId(i);
            congaLeaf->SetTDre(MicroSeconds(30));
            congaLeaf->SetAlpha(0.2);
            congaLeaf->SetLinkCapacity(DataRate(SPINE_LEAF_CAPACITY));
            if (runMode == CONGA)
            {
                congaLeaf->SetFlowletTimeout(MicroSeconds(congaFlowletTimeout));
                if (powerof2)
                {
                    congaLeaf->EnablePowerOf2();
                }
            }
            if (runMode == CONGA_FLOW)
            {
                congaLeaf->SetFlowletTimeout(MilliSeconds(13));
                if (powerof2)
                {
                    congaLeaf->EnablePowerOf2();
                }
            }
            if (runMode == CONGA_ECMP)
            {
                congaLeaf->EnableEcmpMode();
            }
            std::cout << "CONGA Switches configured\n";
        }

        for (int j = 0; j < SPINE_COUNT; j++)
        {
            for (int l = 0; l < LINK_COUNT; l++)
            {
                bool isAsymCapacity = false;

                if (asymCapacity && static_cast<uint32_t>(rand() % 100) < asymCapacityPoss)
                {
                    isAsymCapacity = true;
                }

                if (asymCapacity2 && i == 0 && j == 0)
                {
                    isAsymCapacity = true;
                }

                if (asym == (i * SPINE_COUNT + j + 1))
                {
                    isAsymCapacity = true;
                }

                // Select the link capacity after applying the requested asymmetry.
                uint64_t spineLeafCapacity = SPINE_LEAF_CAPACITY;

                if (isAsymCapacity)
                {
                    spineLeafCapacity = SPINE_LEAF_CAPACITY / 2;
                }

                p2p.SetDeviceAttribute("DataRate", DataRateValue(DataRate(spineLeafCapacity)));
                ipv4.NewNetwork();

                NodeContainer nodeContainer = NodeContainer(leaves.Get(i), spines.Get(j));
                NetDeviceContainer netDeviceContainer = p2p.Install(nodeContainer);
                Ipv4InterfaceContainer ipv4InterfaceContainer = ipv4.Assign(netDeviceContainer);
                NS_LOG_INFO("Leaf - " << i << " is connected to Spine - " << j << " with address "
                                      << ipv4InterfaceContainer.GetAddress(0) << " <-> "
                                      << ipv4InterfaceContainer.GetAddress(1) << " with port "
                                      << netDeviceContainer.Get(0)->GetIfIndex() << " <-> "
                                      << netDeviceContainer.Get(1)->GetIfIndex()
                                      << " with data rate " << spineLeafCapacity);

                if (runMode == CONGA || runMode == CONGA_FLOW || runMode == CONGA_ECMP)
                {
                    // For each conga leaf switch, routing entry to route the packet to OTHER leaves
                    // should be added
                    for (int k = 0; k < LEAF_COUNT; k++)
                    {
                        if (k != i)
                        {
                            congaRoutingHelper.GetCongaRouting(leaves.Get(i)->GetObject<Ipv4>())
                                ->AddRoute(leafNetworks[k],
                                           Ipv4Mask("255.255.255.0"),
                                           netDeviceContainer.Get(0)->GetIfIndex());
                        }
                    }
                    // For each conga spine switch, routing entry to THIS leaf switch should be
                    // added
                    Ptr<Ipv4CongaRouting> congaSpine =
                        congaRoutingHelper.GetCongaRouting(spines.Get(j)->GetObject<Ipv4>());
                    congaSpine->SetTDre(MicroSeconds(30));
                    congaSpine->SetAlpha(0.2);
                    congaSpine->SetLinkCapacity(DataRate(SPINE_LEAF_CAPACITY));
                    if (powerof2)
                    {
                        congaSpine->EnablePowerOf2();
                    }
                    if (runMode == CONGA_ECMP)
                    {
                        congaSpine->EnableEcmpMode();
                    }
                    congaSpine->AddRoute(leafNetworks[i],
                                         Ipv4Mask("255.255.255.0"),
                                         netDeviceContainer.Get(1)->GetIfIndex());
                    if (isAsymCapacity)
                    {
                        uint64_t congaAwareCapacity =
                            congaAwareAsym ? spineLeafCapacity : SPINE_LEAF_CAPACITY;
                        Ptr<Ipv4CongaRouting> congaLeaf =
                            congaRoutingHelper.GetCongaRouting(leaves.Get(i)->GetObject<Ipv4>());
                        congaLeaf->SetLinkCapacity(netDeviceContainer.Get(0)->GetIfIndex(),
                                                   DataRate(congaAwareCapacity));
                        NS_LOG_INFO("Reducing Link Capacity of Conga Leaf: "
                                    << i
                                    << " with port: " << netDeviceContainer.Get(0)->GetIfIndex());
                        congaSpine->SetLinkCapacity(netDeviceContainer.Get(1)->GetIfIndex(),
                                                    DataRate(congaAwareCapacity));
                        NS_LOG_INFO("Reducing Link Capacity of Conga Spine: "
                                    << j
                                    << " with port: " << netDeviceContainer.Get(1)->GetIfIndex());
                        std::cout << "CONGA Asym\n";
                    }
                }
            }
        }
    }

    if (runMode == ECMP)
    {
        NS_LOG_INFO("Populate global routing tables");
        Ipv4GlobalRoutingHelper::PopulateRoutingTables();
    }

    double oversubRatio = static_cast<double>(SERVER_COUNT * LEAF_SERVER_CAPACITY) /
                          (SPINE_LEAF_CAPACITY * SPINE_COUNT * LINK_COUNT);
    NS_LOG_INFO("Over-subscription ratio: " << oversubRatio);

    std::vector<Flow> flows;

    try
    {
        flows = readFlowsFromCSV(inputfile);
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    double time = 0.0;

    for (const auto& flow : flows)
    {
        time = std::max(time, flow.start);
    }

    // Use the longer of the explicit drain period and a window ending at four
    // times the final flow start.  The relative window scales for long traces,
    // while the explicit drain still protects short traces.
    END_TIME = fixedEndTime >= 0
                   ? fixedEndTime
                   : (drainTime >= 0 ? std::max(time + drainTime, 4 * time) : 2 * time);

    std::cout << END_TIME << "\n";

    NS_LOG_INFO("Create applications");

    if (traceDelivery && !applicationTrace.empty())
    {
        trace_delivery::events.open(applicationTrace);
        NS_ABORT_MSG_IF(!trace_delivery::events, "Cannot open application event log");
        trace_delivery::events
            << "TimeNs,Event,TraceIndex,PacketUid,PayloadBytes,Sequence,Acknowledgment,Flags,"
               "SourcePort,DestinationPort,LocalAddress,PeerAddress,Socket\n";
    }
    if (!queueTracePath.empty())
    {
        NS_ABORT_MSG_IF(trace_delivery::selectedIndex < 0,
                        "queueTrace requires a non-negative traceIndex");
        queueTracePort = PORT_START + trace_delivery::selectedIndex;
        if (queueTracePort > PORT_END)
        {
            queueTracePort -= PORT_END - PORT_START;
        }
        queueTrace.open(queueTracePath);
        NS_ABORT_MSG_IF(!queueTrace, "Cannot open queue trace");
        queueTrace << "TimeNs,Event,Node,Device,QueuePackets,QueueBytes,PacketUid,PacketBytes,"
                      "Ipv4Id,SourceIp,DestinationIp,SourcePort,DestinationPort,TcpFlags,"
                      "Sequence,Acknowledgment\n";

        for (uint32_t nodeId = 0; nodeId < NodeList::GetNNodes(); ++nodeId)
        {
            Ptr<Node> node = NodeList::GetNode(nodeId);
            Ptr<TrafficControlLayer> trafficControl = node->GetObject<TrafficControlLayer>();
            if (!trafficControl)
            {
                continue;
            }
            for (uint32_t deviceId = 0; deviceId < node->GetNDevices(); ++deviceId)
            {
                Ptr<QueueDisc> rootQueueDisc =
                    trafficControl->GetRootQueueDiscOnDevice(node->GetDevice(deviceId));
                if (!rootQueueDisc)
                {
                    continue;
                }
                const bool enqueued = rootQueueDisc->TraceConnectWithoutContext(
                    "Enqueue",
                    MakeBoundCallback(&QueueDiscTrace,
                                      rootQueueDisc,
                                      nodeId,
                                      deviceId,
                                      std::string("enqueue")));
                const bool dequeued = rootQueueDisc->TraceConnectWithoutContext(
                    "Dequeue",
                    MakeBoundCallback(&QueueDiscTrace,
                                      rootQueueDisc,
                                      nodeId,
                                      deviceId,
                                      std::string("dequeue")));
                const bool dropped = rootQueueDisc->TraceConnectWithoutContext(
                    "Drop",
                    MakeBoundCallback(&QueueDiscTrace,
                                      rootQueueDisc,
                                      nodeId,
                                      deviceId,
                                      std::string("drop")));
                NS_ABORT_MSG_IF(!enqueued || !dequeued || !dropped,
                                "Cannot connect root queue-disc trace");
            }
        }
    }
    long flowCount = 0;
    long totalFlowSize = 0;

    for (int fromLeafId = 0; fromLeafId < LEAF_COUNT; fromLeafId++)
    {
        install_applications(fromLeafId,
                             servers,
                             flows,
                             flowCount,
                             totalFlowSize,
                             SERVER_COUNT,
                             LEAF_COUNT,
                             START_TIME,
                             END_TIME,
                             traceDelivery);
    }

    NS_LOG_INFO("Total flow: " << flowCount);
    std::cout << "Total flow: " << flowCount << std::endl;

    // A zero-flow run is valid when the input trace is empty.  Avoid printing
    // a NaN or infinity in that case.
    const double averageFlowSize =
        flowCount > 0 ? static_cast<double>(totalFlowSize) / flowCount : 0.0;
    NS_LOG_INFO("Actual average flow size: " << averageFlowSize);

    NS_LOG_INFO("Enabling flow monitor");

    Config::Connect("/NodeList/*/DeviceList/*/$ns3::PointToPointNetDevice/MacTxDrop",
                    MakeCallback(&MacTxDropSink));

    Config::Connect("/NodeList/*/DeviceList/*/$ns3::PointToPointNetDevice/PhyTxDrop",
                    MakeCallback(&PhyTxDropSink));

    Config::Connect("/NodeList/*/DeviceList/*/$ns3::PointToPointNetDevice/PhyRxDrop",
                    MakeCallback(&PhyRxDropSink));

    Config::Connect("/NodeList/*/DeviceList/*/$ns3::PointToPointNetDevice/TxQueue/Drop",
                    MakeCallback(&TxQueueDropCallback));

    Config::Connect("/NodeList/*/DeviceList/*/$ns3::PointToPointNetDevice/PhyTxEnd",
                    MakeCallback(&LinkTxTrace));

    PrintNodeContainerIPs("SPINE", spines);
    PrintNodeContainerIPs("LEAF", leaves);
    PrintNodeContainerIPs("SERVER", servers);

    Ptr<FlowMonitor> flowMonitor;
    FlowMonitorHelper flowHelper;
    flowMonitor = flowHelper.InstallAll();

    // FlowMonitor records IP-level packets and delays during the simulation.
    flowMonitor->CheckForLostPackets();

    if (progressInterval > 0.0)
    {
        std::cout << "Simulator queue progress every " << progressInterval << " s\n";
        Simulator::Schedule(Seconds(progressInterval),
                            &PrintSimTime,
                            servers,
                            Seconds(progressInterval));
    }

    if (!packetTrace.empty())
    {
        AsciiTraceHelper ascii;
        PointToPointHelper packetTraceHelper;
        packetTraceHelper.EnableAsciiAll(ascii.CreateFileStream(packetTrace));
    }
    NS_LOG_INFO("Start simulation");
    Simulator::Stop(Seconds(END_TIME));
    Simulator::Run();

    // Application payload sidecars exist only when delivery tracing is enabled.
    if (traceDelivery)
    {
        trace_delivery::Write(outputfile + ".flows.csv");
    }

    // Root queue-disc counters are route-mode independent. Collect them for
    // both CONGA and ECMP before Simulator::Destroy releases the devices.
    CollectRootQueueDiscDrops();

    // Freeze FlowMonitor's lost-packet accounting and obtain all observed IP flows.
    flowMonitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier =
        DynamicCast<Ipv4FlowClassifier>(flowHelper.GetClassifier());
    NS_ABORT_MSG_IF(!classifier, "Cannot create the IPv4 flow classifier");
    std::map<FlowId, FlowMonitor::FlowStats> stats = flowMonitor->GetFlowStats();
    const std::map<uint32_t, uint32_t> addressToNode = BuildAddressToNodeMap();

    std::cout << "Writing csv" << std::endl;

    // The main report contains one row for every FlowMonitor five-tuple.
    std::ofstream out(outputfile);
    NS_ABORT_MSG_IF(!out, "Cannot open FlowMonitor output file: " << outputfile);

    // Column meanings: packet times, flow-completion time, packet counts,
    // loss ratios, byte counts, throughput, delay, jitter, and hop count.
    out << "FlowID,Src,SrcPort,Dest,DestPort,"
           "TimeFirstRxPacket,TimeFirstTxPacket,"
           "TimeLastRxPacket,TimeLastTxPacket,"
           "FCT(s),TxPackets,RxPackets,LostPackets,LossRate,"
           "PDR,LossPercent,TxBytes,RxBytes,Throughput(Kbps),"
           "MeanDelay(ms),Jitter(ms),HopCount\n";

    // This map is a compact per-source total used by the second summary.
    std::map<uint32_t, uint32_t> totalLossPerHost;

    for (auto& flow : stats)
    {
        // FlowMonitor identifies the endpoints and transport ports for this row.
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(flow.first);
        const FlowMonitor::FlowStats& st = flow.second;

        uint32_t srcNode = GetNodeIdForAddress(addressToNode, t.sourceAddress);
        uint32_t dstNode = GetNodeIdForAddress(addressToNode, t.destinationAddress);

        // FCT is the time from the first transmitted packet to the last received packet.
        double fct = st.timeLastRxPacket.GetSeconds() - st.timeFirstTxPacket.GetSeconds();
        // Throughput is reported in Kbit/s; avoid division by zero for empty flows.
        double throughput = fct > 0 ? (st.rxBytes * 8.0) / (fct * 1000.0) : 0.0;

        // lostPackets is the number of packets FlowMonitor could not match at Rx.
        uint32_t lost = st.lostPackets;
        double lossRate = st.txPackets > 0 ? (double)lost / st.txPackets : 0;
        // PDR is the received-to-transmitted packet ratio.
        double pdr = st.txPackets > 0 ? (double)st.rxPackets / st.txPackets : 0;
        double lossPercent = lossRate * 100.0;

        // FlowMonitor stores delay and jitter as sums; divide by received packets
        // to obtain the mean values written in milliseconds.
        double meanDelayMs =
            (st.rxPackets > 0) ? st.delaySum.GetSeconds() / st.rxPackets * 1000.0 : 0;

        double jitterMs =
            (st.rxPackets > 0) ? st.jitterSum.GetSeconds() / st.rxPackets * 1000.0 : 0;

        // timesForwarded counts intermediate forwards; add one for the delivery hop.
        uint32_t totalForwards = st.timesForwarded;
        double avgHopCount = (st.rxPackets > 0) ? (double)totalForwards / st.rxPackets + 1.0 : 0;

        out << flow.first << "," << t.sourceAddress << "," << t.sourcePort << ","
            << t.destinationAddress << "," << t.destinationPort << "," << st.timeFirstRxPacket
            << "," << st.timeFirstTxPacket << "," << st.timeLastRxPacket << ","
            << st.timeLastTxPacket << "," << std::fixed << std::setprecision(6) << fct << ","
            << st.txPackets << "," << st.rxPackets << "," << lost << "," << lossRate << "," << pdr
            << "," << lossPercent << "," << st.txBytes << "," << st.rxBytes << "," << throughput
            << "," << meanDelayMs << "," << jitterMs << "," << std::fixed << std::setprecision(3)
            << avgHopCount << "\n";

        totalLossPerHost[srcNode] += lost;

        nodeTxPackets[srcNode] += st.txPackets;
        nodeRxPackets[dstNode] += st.rxPackets;
        nodeLostPackets[srcNode] += lost;
    }

    // Aggregate the same FlowMonitor rows by source node. Rx is indexed by the
    // destination node, while Tx and loss are indexed by the source node.
    out << "\n=== Per-Node Packet Loss Summary ===\n";
    out << "NodeID,TotalTxPackets,TotalRxPackets,TotalLostPackets,LossPercent,MacTxDrop,PhyTxDrop,"
           "PhyRxDrop,DeviceQueueDrop,RootQueueDiscDrop\n";

    std::set<uint32_t> summaryNodes;
    for (const auto& entry : nodeTxPackets)
    {
        summaryNodes.insert(entry.first);
    }
    for (const auto& entry : nodeRxPackets)
    {
        summaryNodes.insert(entry.first);
    }
    for (const auto& entry : macTxDrops)
    {
        summaryNodes.insert(entry.first);
    }
    for (const auto& entry : phyTxDrops)
    {
        summaryNodes.insert(entry.first);
    }
    for (const auto& entry : phyRxDrops)
    {
        summaryNodes.insert(entry.first);
    }
    for (const auto& entry : nodeQueueDrops)
    {
        summaryNodes.insert(entry.first);
    }
    for (const auto& entry : nodeRootQueueDiscDrops)
    {
        summaryNodes.insert(entry.first);
    }

    for (uint32_t node : summaryNodes)
    {
        uint64_t tx = nodeTxPackets[node];
        uint64_t rx = nodeRxPackets[node];
        uint64_t lost = nodeLostPackets[node];
        uint64_t queueDrops = nodeQueueDrops[node];

        double lossPct = (tx > 0) ? (double)lost * 100.0 / tx : 0.0;

        out << node << "," << tx << "," << rx << "," << lost << "," << std::fixed
            << std::setprecision(3) << lossPct << "," << macTxDrops[node] << "," << phyTxDrops[node]
            << "," << phyRxDrops[node] << "," << queueDrops << ","
            << nodeRootQueueDiscDrops[node] << "\n";
    }

    // Unlike the preceding table, this table contains only FlowMonitor loss,
    // grouped by the source host that sent the packets.
    out << "\n=== Total Packet Loss Per Node ===\n";
    out << "NodeID,TotalLostPackets\n";
    for (const auto& entry : totalLossPerHost)
    {
        out << entry.first << "," << entry.second << "\n";
    }

    // Queue drops are counted by the TxQueueDrop callback above.
    out << "\n=== Per Link Queue Drop ===\n";
    // Each row identifies the local interface where a packet was discarded.
    out << "\nNodeId,DeviceId,IP Address,DroppedPackets\n";
    for (const auto& e : queuedroplink)
    {
        uint32_t nodeId_link = std::get<0>(e.first);
        uint32_t devId_link = std::get<1>(e.first);
        Ipv4Address addr = std::get<2>(e.first);
        uint64_t packets = e.second;

        out << nodeId_link << "," << devId_link << "," << addr << "," << packets << "\n";
    }

    // Root queue discs run before the device TxQueue. Their counters explain
    // FlowMonitor queue-disc losses when the device queue itself has no drops.
    out << "\n=== Per Link Root Queue Disc Drop ===\n";
    out << "NodeId,DeviceId,IP Address,QueueDiscType,DroppedPackets,"
           "DroppedBeforeEnqueue,DroppedAfterDequeue,DroppedBytes\n";
    for (const auto& e : rootQueueDiscDrops)
    {
        const uint32_t nodeId = std::get<0>(e.first);
        const uint32_t deviceId = std::get<1>(e.first);
        const Ipv4Address address = std::get<2>(e.first);
        const RootQueueDiscSummary& summary = e.second;
        out << nodeId << "," << deviceId << "," << address << "," << summary.type << ","
            << summary.droppedPackets << "," << summary.droppedBeforeEnqueue << ","
            << summary.droppedAfterDequeue << "," << summary.droppedBytes << "\n";
    }

    out << "\n=== Root Queue Disc Drop Reasons ===\n";
    out << "NodeId,DeviceId,IP Address,Stage,Reason,DroppedPackets\n";
    for (const auto& e : rootQueueDiscDrops)
    {
        const uint32_t nodeId = std::get<0>(e.first);
        const uint32_t deviceId = std::get<1>(e.first);
        const Ipv4Address address = std::get<2>(e.first);
        const RootQueueDiscSummary& summary = e.second;
        for (const auto& reason : summary.droppedBeforeEnqueueByReason)
        {
            out << nodeId << "," << deviceId << "," << address << ",BeforeEnqueue,"
                << reason.first << "," << reason.second << "\n";
        }
        for (const auto& reason : summary.droppedAfterDequeueByReason)
        {
            out << nodeId << "," << deviceId << "," << address << ",AfterDequeue,"
                << reason.first << "," << reason.second << "\n";
        }
    }

    // LinkTxTrace accumulates transmitted bytes for each node/device/address.
    // Utilization is the fraction of link capacity used during [utilStart, utilEnd].
    out << "\n=== Per Link Utilization ===\n";
    out << "\nNodeId,DeviceId,IP Address,Bytes,UtilisationPercent\n";
    const double interval = utilEnd - utilStart;
    for (const auto& e : bytesPerLink)
    {
        uint32_t nodeId_link = std::get<0>(e.first);
        uint32_t devId_link = std::get<1>(e.first);
        Ipv4Address addr = std::get<2>(e.first);
        uint64_t bytes = e.second;

        Ptr<PointToPointNetDevice> device =
            DynamicCast<PointToPointNetDevice>(NodeList::GetNode(nodeId_link)->GetDevice(devId_link));
        DataRateValue dataRate;
        if (device)
        {
            device->GetAttribute("DataRate", dataRate);
        }
        const double linkRate = device ? dataRate.Get().GetBitRate() : 0.0;

        // Bytes are converted to bits, then divided by capacity over the
        // measurement interval. Reading the device rate keeps an asymmetric
        // 50 Mbps link distinct from a normal 100 Mbps link.
        double util = (interval > 0.0 && linkRate > 0.0)
                          ? ((bytes * 8.0) / (linkRate * interval)) * 100.0
                          : 0.0;

        out << nodeId_link << "," << devId_link << "," << addr << "," << bytes << "," << std::fixed
            << std::setprecision(2) << util << "\n";
    }

    Simulator::Destroy();
    NS_LOG_INFO("Stop simulation");
}
