/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef IPV4_CONGA_ROUTING_H
#define IPV4_CONGA_ROUTING_H

#include "ns3/data-rate.h"
#include "ns3/event-id.h"
#include "ns3/ipv4-header.h"
#include "ns3/ipv4-route.h"
#include "ns3/ipv4-routing-protocol.h"
#include "ns3/nstime.h"
#include "ns3/object.h"
#include "ns3/packet.h"

#include <map>
#include <tuple>
#include <vector>

namespace ns3
{

/**
 * @brief Cached uplink choice for an active CONGA flowlet.
 */
struct Flowlet
{
    uint32_t port;   ///< Uplink interface selected for the flowlet.
    Time activeTime; ///< Time at which the flowlet last sent a packet.
};

/**
 * @brief Congestion feedback received from a remote leaf switch.
 */
struct FeedbackInfo
{
    uint32_t ce;     ///< Quantized congestion estimate.
    bool change;     ///< True when the estimate has not yet been piggybacked.
    Time updateTime; ///< Time at which the estimate was last updated.
};

/**
 * @brief A destination prefix reachable through one output interface.
 */
struct CongaRouteEntry
{
    Ipv4Address network;  ///< Destination network prefix.
    Ipv4Mask networkMask; ///< Destination network mask.
    uint32_t port;        ///< Output interface for the prefix.
};

/**
 * @ingroup conga-routing
 * @brief IPv4 routing protocol implementing CONGA-style flowlet routing.
 *
 * Leaf switches select an uplink for each flowlet from local DRE congestion
 * estimates and remote feedback. Spine switches forward using ECMP. Packets
 * carry an Ipv4CongaTag to convey the chosen path and congestion feedback
 * between leaves.
 */
class Ipv4CongaRouting : public Ipv4RoutingProtocol
{
  public:
    /** Create an unconfigured CONGA routing protocol. */
    Ipv4CongaRouting();

    ~Ipv4CongaRouting() override;

    /**
     * Get the TypeId for this routing protocol.
     *
     * @return The CONGA routing TypeId.
     */
    static TypeId GetTypeId();

    /**
     * Mark this router as a leaf switch and assign its leaf identifier.
     *
     * @param leafId Identifier used in CONGA feedback tables.
     */
    void SetLeafId(uint32_t leafId);

    /**
     * Set the DRE exponential-decay coefficient.
     *
     * @param alpha Fraction removed from the DRE value at each update event.
     */
    void SetAlpha(double alpha);

    /**
     * Set the interval between DRE decay events.
     *
     * @param time DRE update interval.
     */
    void SetTDre(Time time);

    /**
     * Set the default capacity used to quantize DRE values.
     *
     * @param dataRate Default output-link capacity.
     */
    void SetLinkCapacity(DataRate dataRate);

    /**
     * Set the capacity used to quantize one output interface.
     *
     * @param interface Output interface index.
     * @param dataRate Capacity of the specified interface.
     */
    void SetLinkCapacity(uint32_t interface, DataRate dataRate);

    /**
     * Set the number of bits used to quantize the DRE value.
     *
     * @param q Quantization width in bits.
     */
    void SetQ(uint32_t q);

    /**
     * Set the idle interval that starts a new flowlet.
     *
     * @param timeout Maximum inter-packet gap for reuse of a cached uplink.
     */
    void SetFlowletTimeout(Time timeout);

    /**
     * Associate a host address with the leaf that serves it.
     *
     * @param addr Host IPv4 address.
     * @param leafId Serving leaf identifier.
     */
    void AddAddressToLeafIdMap(Ipv4Address addr, uint32_t leafId);

    /**
     * Add a destination prefix and its output interface.
     *
     * @param network Destination network.
     * @param networkMask Destination network mask.
     * @param port Output interface index.
     */
    void AddRoute(Ipv4Address network, Ipv4Mask networkMask, uint32_t port);

    /**
     * Initialize a remote congestion estimate.
     *
     * @param destLeafId Destination leaf identifier.
     * @param port Uplink interface for that destination.
     * @param congestion Initial quantized congestion estimate.
     */
    void InitCongestion(uint32_t destLeafId, uint32_t port, uint32_t congestion);

    /** Enable the experimental ECMP-only leaf selection mode. */
    void EnableEcmpMode();

    /** Enable the experimental power-of-two candidate selection mode. */
    void EnablePowerOf2();

    /**
     * Route locally originated packets.
     *
     * @param packet Packet to route.
     * @param header IPv4 header for the packet.
     * @param oif Requested output device, if any.
     * @param sockerr Output socket error.
     * @return The selected route, or null when no route is supplied.
     */
    Ptr<Ipv4Route> RouteOutput(Ptr<Packet> packet,
                               const Ipv4Header& header,
                               Ptr<NetDevice> oif,
                               Socket::SocketErrno& sockerr) override;

    /**
     * Process and forward an incoming IPv4 packet.
     *
     * @param packet Packet to route.
     * @param header IPv4 header for the packet.
     * @param idev Incoming device.
     * @param ucb Unicast forwarding callback.
     * @param mcb Multicast forwarding callback.
     * @param lcb Local-delivery callback.
     * @param ecb Error callback.
     * @return True when the packet was handled.
     */
    bool RouteInput(Ptr<const Packet> packet,
                    const Ipv4Header& header,
                    Ptr<const NetDevice> idev,
                    const UnicastForwardCallback& ucb,
                    const MulticastForwardCallback& mcb,
                    const LocalDeliverCallback& lcb,
                    const ErrorCallback& ecb) override;

    /** @copydoc Ipv4RoutingProtocol::NotifyInterfaceUp */
    void NotifyInterfaceUp(uint32_t interface) override;

    /** @copydoc Ipv4RoutingProtocol::NotifyInterfaceDown */
    void NotifyInterfaceDown(uint32_t interface) override;

    /** @copydoc Ipv4RoutingProtocol::NotifyAddAddress */
    void NotifyAddAddress(uint32_t interface, Ipv4InterfaceAddress address) override;

    /** @copydoc Ipv4RoutingProtocol::NotifyRemoveAddress */
    void NotifyRemoveAddress(uint32_t interface, Ipv4InterfaceAddress address) override;

    /** @copydoc Ipv4RoutingProtocol::SetIpv4 */
    void SetIpv4(Ptr<Ipv4> ipv4) override;

    /** @copydoc Ipv4RoutingProtocol::PrintRoutingTable */
    void PrintRoutingTable(Ptr<OutputStreamWrapper> stream, Time::Unit unit = Time::S) const override;

    /** Release scheduled events and dynamically allocated flowlet state. */
    void DoDispose() override;

  private:
    /**
     * @brief Canonical bidirectional TCP or UDP flow identifier.
     *
     * Endpoint addresses and ports are sorted so both directions map to the
     * same flowlet entry.
     */
    struct BidirectionalFlowKey
    {
        uint32_t ip1;      ///< Lower ordered endpoint address.
        uint32_t ip2;      ///< Higher ordered endpoint address.
        uint16_t port1;    ///< Port paired with ip1.
        uint16_t port2;    ///< Port paired with ip2.
        uint8_t protocol;  ///< IPv4 transport protocol number.

        /**
         * Order keys for use in std::map.
         *
         * @param other Key to compare.
         * @return True when this key sorts before @p other.
         */
        bool operator<(const BidirectionalFlowKey& other) const
        {
            return std::tie(ip1, ip2, port1, port2, protocol) <
                   std::tie(other.ip1, other.ip2, other.port1, other.port2, other.protocol);
        }
    };

    /**
     * Add packet bytes to the local DRE value for an output interface.
     *
     * @param header IPv4 header whose serialized size is included.
     * @param packet Packet whose payload size is included.
     * @param port Output interface index.
     * @return Updated unquantized DRE value in bytes.
     */
    uint32_t UpdateLocalDre(const Ipv4Header& header, Ptr<Packet> packet, uint32_t port);

    /** Decay all local DRE values and reschedule while any value is nonzero. */
    void DreEvent();

    /** Age remote congestion and feedback entries, then reschedule if needed. */
    void AgingEvent();

    /**
     * Convert a DRE byte count to the configured congestion metric.
     *
     * @param interface Output interface index.
     * @param x Unquantized DRE byte count.
     * @return Quantized congestion metric.
     */
    uint32_t QuantizingX(uint32_t interface, uint32_t x);

    /**
     * Find every configured route whose prefix contains a destination address.
     *
     * @param dest Destination IPv4 address.
     * @return Matching route entries.
     */
    std::vector<CongaRouteEntry> LookupCongaRouteEntries(Ipv4Address dest);

    /**
     * Build a next-hop route for an output interface.
     *
     * @param port Output interface index.
     * @param destAddress Final destination address.
     * @return Point-to-point route using @p port.
     */
    Ptr<Ipv4Route> ConstructIpv4Route(uint32_t port, Ipv4Address destAddress);

    /**
     * Build a direction-independent transport flow key.
     *
     * @param srcIp Source IPv4 address.
     * @param srcPort Source transport port.
     * @param dstIp Destination IPv4 address.
     * @param dstPort Destination transport port.
     * @param protocol IPv4 transport protocol number.
     * @return Canonical bidirectional flow key.
     */
    BidirectionalFlowKey MakeFlowKey(Ipv4Address srcIp,
                                     uint16_t srcPort,
                                     Ipv4Address dstIp,
                                     uint16_t dstPort,
                                     uint8_t protocol);

    /**
     * Hash a canonical flow key for legacy flowlet table use.
     *
     * @param srcIp Source IPv4 address.
     * @param srcPort Source transport port.
     * @param dstIp Destination IPv4 address.
     * @param dstPort Destination transport port.
     * @param protocol IPv4 transport protocol number.
     * @return Deterministic flow identifier.
     */
    uint32_t GetFlowId(Ipv4Address srcIp,
                       uint16_t srcPort,
                       Ipv4Address dstIp,
                       uint16_t dstPort,
                       uint8_t protocol);

    /** Log the Congestion-To-Leaf table when debug logging is enabled. */
    void PrintCongaToLeafTable();

    /** Log the Congestion-From-Leaf table when debug logging is enabled. */
    void PrintCongaFromLeafTable();

    /** Log the cached flowlet table when debug logging is enabled. */
    void PrintFlowletTable();

    /** Log local DRE values when debug logging is enabled. */
    void PrintDreTable();

    bool m_isLeaf;     ///< True when this router acts as a leaf switch.
    uint32_t m_leafId; ///< Leaf identifier when m_isLeaf is true.

    Time m_tdre;       ///< Interval between DRE decay events.
    double m_alpha;    ///< Fraction removed from DRE at each decay event.
    DataRate m_C;      ///< Default link capacity for DRE quantization.
    std::map<uint32_t, DataRate> m_Cs; ///< Per-interface capacity overrides.
    uint32_t m_Q;      ///< Width of the quantized congestion metric in bits.
    Time m_agingTime;  ///< Lifetime of remote congestion feedback.
    Time m_flowletTimeout; ///< Maximum idle interval that retains a flowlet.
    bool m_ecmpMode;   ///< Enables experimental ECMP-only leaf selection.
    bool m_powerof2;   ///< Enables experimental power-of-two selection.

    unsigned long m_feedbackIndex; ///< Round-robin index for feedback piggybacking.
    EventId m_dreEvent;            ///< Scheduled DRE decay event.
    EventId m_agingEvent;          ///< Scheduled remote-state aging event.
    Ptr<Ipv4> m_ipv4;              ///< IPv4 object served by this protocol.

    std::vector<CongaRouteEntry> m_routeEntryList; ///< Configured destination routes.
    std::map<Ipv4Address, uint32_t> m_ipLeafIdMap; ///< Host address to leaf identifier map.

    /// Destination leaf -> uplink -> (last update time, congestion estimate).
    std::map<uint32_t, std::map<uint32_t, std::pair<Time, uint32_t>>> m_congaToLeafTable;

    /// Source leaf -> uplink -> feedback available for piggybacking.
    std::map<uint32_t, std::map<uint32_t, FeedbackInfo>> m_congaFromLeafTable;

    /// Legacy hashed flow identifier -> cached flowlet; values are owned here.
    std::map<uint32_t, Flowlet*> m_flowletTable;

    /// Output interface -> unquantized local DRE byte count.
    std::map<uint32_t, uint32_t> m_XMap;
};

} // namespace ns3

#endif /* IPV4_CONGA_ROUTING_H */
