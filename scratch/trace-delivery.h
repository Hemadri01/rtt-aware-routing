#ifndef TRACE_DELIVERY_H
#define TRACE_DELIVERY_H

#include "ns3/packet-sink.h"
#include "ns3/inet6-socket-address.h"
#include "ns3/inet-socket-address.h"
#include "ns3/ipv4-address.h"
#include "ns3/ipv6-address.h"
#include "ns3/simulator.h"
#include "ns3/tcp-header.h"
#include "ns3/tcp-socket-base.h"

#include <deque>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>

namespace trace_delivery
{
inline std::ofstream events; ///< Optional application event log.
inline std::map<int, uint64_t> acceptedBytes; ///< Bytes reported by sender Tx callbacks.
inline int selectedIndex = -1; ///< Trace row to log, or -1 for every row.

/**
 * Convert a tracing address to text without assuming it includes a TCP port.
 *
 * TcpSocketBase supplies bare Ipv4Address or Ipv6Address values in its
 * retransmission trace, whereas PacketSink supplies an InetSocketAddress.
 * Handling both forms keeps focused tracing observational and prevents a
 * diagnostic callback from changing the simulation outcome.
 *
 * @param address Address supplied by an ns-3 trace source.
 * @return Printable IP address, or ns-3's generic address representation.
 */
inline std::string
FormatHostAddress(const ns3::Address& address)
{
    std::ostringstream out;
    if (ns3::InetSocketAddress::IsMatchingType(address))
    {
        out << ns3::InetSocketAddress::ConvertFrom(address).GetIpv4();
    }
    else if (ns3::Inet6SocketAddress::IsMatchingType(address))
    {
        out << ns3::Inet6SocketAddress::ConvertFrom(address).GetIpv6();
    }
    else if (ns3::Ipv4Address::IsMatchingType(address))
    {
        out << ns3::Ipv4Address::ConvertFrom(address);
    }
    else if (ns3::Ipv6Address::IsMatchingType(address))
    {
        out << ns3::Ipv6Address::ConvertFrom(address);
    }
    else
    {
        out << address;
    }
    return out.str();
}

/**
 * Convert a tracing address to text, preserving a port when one is present.
 *
 * @param address Address supplied by an ns-3 trace source.
 * @return Printable endpoint or host address.
 */
inline std::string
FormatSocketAddress(const ns3::Address& address)
{
    std::ostringstream out;
    if (ns3::InetSocketAddress::IsMatchingType(address))
    {
        const auto endpoint = ns3::InetSocketAddress::ConvertFrom(address);
        out << endpoint.GetIpv4() << ':' << endpoint.GetPort();
        return out.str();
    }
    if (ns3::Inet6SocketAddress::IsMatchingType(address))
    {
        const auto endpoint = ns3::Inet6SocketAddress::ConvertFrom(address);
        out << endpoint.GetIpv6() << ':' << endpoint.GetPort();
        return out.str();
    }
    return FormatHostAddress(address);
}

/**
 * Record an application send accepted by TCP.
 * @param index Original trace index.
 * @param packet Accepted payload.
 */
inline void Sent(int index, ns3::Ptr<const ns3::Packet> packet)
{
    acceptedBytes[index] += packet->GetSize();
    if (events && (selectedIndex < 0 || selectedIndex == index))
    {
        events << ns3::Simulator::Now().GetNanoSeconds() << ",send," << index
               << ',' << packet->GetUid() << ',' << packet->GetSize() << ",,,,,,,,\n";
    }
}

/**
 * Record a TCP retransmission reported by one BulkSend application.
 *
 * The application exposes this trace directly from its TCP socket.  Keeping it
 * here lets a focused experiment correlate retransmissions with an original
 * workload row without enabling verbose TCP logging for every socket.
 *
 * @param index Original trace index.
 * @param packet TCP payload retransmitted by the socket.
 * @param header TCP header used for the retransmission.
 * @param localAddr Sender socket address.
 * @param peerAddr Receiver socket address.
 * @param socket TCP socket that retransmitted the packet.
 */
inline void
Retransmitted(int index,
              ns3::Ptr<const ns3::Packet> packet,
              const ns3::TcpHeader& header,
              const ns3::Address& localAddr,
              const ns3::Address& peerAddr,
              ns3::Ptr<const ns3::TcpSocketBase> socket)
{
    if (events && (selectedIndex < 0 || selectedIndex == index))
    {
        events << ns3::Simulator::Now().GetNanoSeconds() << ",retransmit," << index << ','
               << packet->GetUid() << ',' << packet->GetSize() << ',' << header.GetSequenceNumber()
               << ',' << header.GetAckNumber() << ',' << static_cast<uint32_t>(header.GetFlags())
               << ',' << header.GetSourcePort() << ',' << header.GetDestinationPort() << ','
               << FormatHostAddress(localAddr) << ':' << header.GetSourcePort() << ','
               << FormatHostAddress(peerAddr) << ':' << header.GetDestinationPort() << ',' << socket
               << '\n';
    }
}

/** Application payload accounting for one input trace row. */
struct Record
{
    int index;                ///< Input row index.
    std::string id;           ///< Original trace flow identifier.
    int source;               ///< Source host index.
    int destination;          ///< Destination host index.
    uint16_t port;            ///< Destination TCP port.
    uint64_t expected;        ///< Requested application bytes.
    double start;             ///< Requested start time in seconds.
    uint64_t received{0};     ///< Unique bytes delivered by TCP to PacketSink.
    ns3::Ptr<ns3::PacketSink> sink; ///< Sink for independent total-byte verification.
    double completion{-1};    ///< Time of the final payload byte, or -1 if incomplete.

    /**
     * Count application bytes and record completion.
     * @param packet Delivered application payload.
     * @param from Sender address.
     */
    void Receive(ns3::Ptr<const ns3::Packet> packet, const ns3::Address& from)
    {
        received += packet->GetSize();
        if (events && (selectedIndex < 0 || selectedIndex == index))
        {
            events << ns3::Simulator::Now().GetNanoSeconds() << ",receive," << index
                   << ',' << packet->GetUid() << ',' << packet->GetSize() << ",,,,,,,"
                   << FormatSocketAddress(from) << ",\n";
        }
        if (received >= expected && completion < 0)
        {
            completion = ns3::Simulator::Now().GetSeconds();
        }
    }
};

inline std::deque<Record> records; ///< Stable storage for receive callback targets.

/**
 * Attach payload accounting to a flow's sink.
 * @tparam Flow Input trace row type.
 * @param flow Input trace row.
 * @param port Destination TCP port.
 * @param application Installed PacketSink.
 */
template <typename Flow>
void Track(const Flow& flow, uint16_t port, ns3::Ptr<ns3::Application> application)
{
    records.push_back({flow.index, flow.flowid, flow.src, flow.dst, port, flow.size, flow.start});
    records.back().sink = ns3::DynamicCast<ns3::PacketSink>(application);
    const bool connected = application->TraceConnectWithoutContext(
        "Rx", ns3::MakeCallback(&Record::Receive, &records.back()));
    NS_ABORT_MSG_IF(!connected, "Cannot connect PacketSink payload accounting");
}

/**
 * Write application-level results separately from FlowMonitor's IP statistics.
 * @param path Output CSV path.
 */
inline void Write(const std::string& path)
{
    std::ofstream out(path);
    NS_ABORT_MSG_IF(!out, "Cannot open application flow results: " << path);
    out << "TraceIndex,FlowID,Src,Dst,DestPort,ExpectedBytes,ReceivedBytes,Complete,"
           "StartTime(s),CompletionTime(s),FCT(s)\n";
    out << std::setprecision(17);
    std::ofstream audit(path + ".audit.csv");
    NS_ABORT_MSG_IF(!audit, "Cannot open independent sink audit");
    audit << "TraceIndex,ExpectedBytes,SenderAcceptedBytes,CallbackBytes,SinkTotalBytes\n";
    std::size_t complete = 0;
    for (const auto& record : records)
    {
        audit << record.index << ',' << record.expected << ','
              << acceptedBytes[record.index] << ',' << record.received << ','
              << record.sink->GetTotalRx() << '\n';
        const bool done = record.received == record.expected;
        complete += done;
        out << record.index << ',' << record.id << ',' << record.source << ','
            << record.destination << ',' << record.port << ',' << record.expected << ','
            << record.received << ',' << done << ',' << record.start << ',';
        if (done)
        {
            out << record.completion << ',' << record.completion - record.start;
        }
        else
        {
            out << ',';
        }
        out << '\n';
    }
    std::cout << "Application flows complete: " << complete << '/' << records.size()
              << "; payload results: " << path << '\n';
}
} // namespace trace_delivery
#endif
