/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef IPV4_RTT_ROUTING_H
#define IPV4_RTT_ROUTING_H

#include "ipv4-rtt-tag.h"

#include "ns3/data-rate.h"
#include "ns3/ipv4-route.h"
#include "ns3/ipv4-routing-protocol.h"
#include "ns3/ipv4.h"
#include "ns3/net-device.h"
#include "ns3/nstime.h"
#include "ns3/object.h"
#include "ns3/output-stream-wrapper.h"
#include "ns3/packet.h"
#include "ns3/random-variable-stream.h"

#include <map>
#include <tuple>
#include <vector>

namespace ns3
{

/**
 * @ingroup rtt-routing
 * @brief IPv4 routing protocol that selects leaf uplinks from RTT estimates.
 *
 * A source leaf tags TCP data packets with the selected uplink and transmit
 * time. The destination leaf preserves that information until it observes the
 * corresponding ACK. When the ACK returns, the source leaf updates the RTT
 * estimate for the tagged uplink. The configured selection mode then uses
 * those estimates for subsequent flowlets.
 */
class Ipv4RttRouting : public Ipv4RoutingProtocol
{
  public:
    /**
     * Get the TypeId for this routing protocol.
     *
     * @return The RTT routing TypeId.
     */
    static TypeId GetTypeId();

    /** Create an unconfigured RTT routing protocol. */
    Ipv4RttRouting();

    ~Ipv4RttRouting() override;

    /** Available leaf-uplink selection policies. */
    enum SelectionMode
    {
        WEIGHTED_ECMP,     ///< Choose an uplink with inverse-RTT weights.
        POWER_OF_2_RANDOM, ///< Compare two randomly selected uplinks.
        POWER_OF_2_TOP2,   ///< Compare the two currently best uplinks.
        LOWEST_RTT         ///< Always choose the smallest effective RTT.
    };

    /**
     * Select the policy used for new flowlets.
     *
     * @param mode RTT-based uplink selection policy.
     */
    void SetSelectionMode(SelectionMode mode);

    /**
     * Set the smoothing coefficient used for RTT estimates.
     *
     * @param alpha Weight assigned to a new RTT sample.
     */
    void SetAlpha(double alpha);

    /**
     * Enable or disable the routing timeout behavior used by this prototype.
     *
     * @param timeouttrue True to enable the timeout behavior.
     */
    void SetTimeouttrue(bool timeouttrue);

    /**
     * Mark this router as a leaf switch and assign its identifier.
     *
     * @param leafId Identifier carried in RTT tags.
     */
    void SetLeafId(uint32_t leafId);

    /**
     * Set the common capacity used when initializing RTT state.
     *
     * @param dataRate Default output-link capacity.
     */
    void SetLinkCapacity(DataRate dataRate);

    /**
     * Declare a per-interface capacity override.
     *
     * This declaration is retained for compatibility with existing experiment
     * code.
     *
     * @param interface Output interface index.
     * @param dataRate Capacity of the specified interface.
     */
    void SetLinkCapacity(uint32_t interface, DataRate dataRate);

    /**
     * Add a destination prefix and its output interface.
     *
     * @param network Destination network prefix.
     * @param mask Destination network mask.
     * @param port Output interface index.
     */
    void AddRoute(Ipv4Address network, Ipv4Mask mask, uint32_t port);

    /** @copydoc Ipv4RoutingProtocol::RouteOutput */
    Ptr<Ipv4Route> RouteOutput(Ptr<Packet> packet,
                               const Ipv4Header& header,
                               Ptr<NetDevice> oif,
                               Socket::SocketErrno& sockerr) override;

    /** @copydoc Ipv4RoutingProtocol::RouteInput */
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
    void PrintRoutingTable(Ptr<OutputStreamWrapper> stream,
                           Time::Unit unit = Time::S) const override;

  protected:
    /** Release dynamically allocated flowlet state. */
    void DoDispose() override;

  private:
    /** @brief A destination prefix reachable through one output interface. */
    struct RttRouteEntry
    {
        Ipv4Address network; ///< Destination network prefix.
        Ipv4Mask mask;       ///< Destination network mask.
        uint32_t port;       ///< Output interface for the prefix.
    };

    /** @brief Cached uplink choice for an active bidirectional flowlet. */
    struct Flowlet
    {
        uint32_t port;   ///< Uplink interface selected for the flowlet.
        Time activeTime; ///< Time at which the flowlet last sent a packet.
    };

    /** @brief State retained for a tagged TCP packet. */
    struct PacketState
    {
        uint32_t seq;        ///< TCP sequence number represented by the state.
        uint32_t pathId;     ///< Uplink used for the packet.
        uint32_t sourceLeaf; ///< Source leaf carried in the packet tag.
        Time txTime;         ///< Original transmit time carried in the packet tag.
        Time lastSeen;       ///< Last time the state was refreshed.
    };

    /** @brief Raw and smoothed RTT measurements for one output interface. */
    struct PortRttState
    {
        /** Construct an RTT state with no valid sample. */
        PortRttState()
            : initialized(false)
        {
        }

        Time rawRtt;      ///< Most recent raw RTT sample.
        Time smoothRtt;   ///< Smoothed RTT used for path selection.
        bool initialized; ///< True after the first valid RTT sample.
    };

    /** @brief RTT tag saved at a destination leaf until the ACK is observed. */
    struct StoredTag
    {
        Ipv4RttTag tag;  ///< Data-packet RTT tag to copy onto the ACK.
        Time lastAccess; ///< Time at which the entry was last updated.
    };

    /**
     * @brief Canonical bidirectional TCP flow identifier.
     *
     * Endpoint addresses and ports are sorted so packets in both directions
     * share the same per-flow state.
     */
    struct BidirectionalFlowKey
    {
        uint32_t ip1;     ///< Lower ordered endpoint address.
        uint32_t ip2;     ///< Higher ordered endpoint address.
        uint16_t port1;   ///< Port paired with ip1.
        uint16_t port2;   ///< Port paired with ip2.
        uint8_t protocol; ///< IPv4 transport protocol number.

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

    /** @brief Cached reverse-path information for one flow. */
    struct ReverseInfo
    {
        /** Construct an invalid reverse-path record. */
        ReverseInfo()
            : destLeaf(0),
              destTime(Seconds(0)),
              valid(false)
        {
        }

        uint32_t destLeaf; ///< Leaf that received the tagged data packet.
        Time destTime;     ///< Time at which the data packet reached that leaf.
        bool valid;        ///< True when this record contains usable data.
    };

    /**
     * Find every configured route whose prefix contains a destination address.
     *
     * @param dest Destination IPv4 address.
     * @return Matching route entries.
     */
    std::vector<RttRouteEntry> LookupRouteEntries(Ipv4Address dest);

    /**
     * Build a next-hop route for an output interface.
     *
     * @param port Output interface index.
     * @param dest Final destination address.
     * @return Point-to-point route using @p port.
     */
    Ptr<Ipv4Route> ConstructIpv4Route(uint32_t port, Ipv4Address dest);

    /** Initialize baseline RTT values for configured uplinks. */
    void InitializeBaseRtts();

    /**
     * Update the raw and smoothed RTT for an uplink.
     *
     * @param port Output interface index.
     * @param rttSample Newly observed end-to-end RTT sample.
     */
    void UpdatePortRtt(uint32_t port, Time rttSample);

    /**
     * Return the RTT metric currently used for one output interface.
     *
     * @param port Output interface index.
     * @return Effective RTT, including the initial baseline when necessary.
     */
    Time GetEffectiveRtt(uint32_t port);

    /** Remove expired packet, tag, reverse-path, and flowlet state. */
    void CleanupOldState();

    /**
     * Select a route using inverse-effective-RTT weights.
     *
     * @param routes Candidate routes.
     * @return Selected output interface.
     */
    uint32_t SelectWeightedPort(const std::vector<RttRouteEntry>& routes);

    /**
     * Select the lower-RTT member of two random candidate routes.
     *
     * @param routes Candidate routes.
     * @return Selected output interface.
     */
    uint32_t SelectPowerOf2Random(const std::vector<RttRouteEntry>& routes);

    /**
     * Select between the two routes with the smallest effective RTT.
     *
     * @param routes Candidate routes.
     * @return Selected output interface.
     */
    uint32_t SelectPowerOf2Top2(const std::vector<RttRouteEntry>& routes);

    /**
     * Select the route with the smallest effective RTT.
     *
     * @param routes Candidate routes.
     * @return Selected output interface.
     */
    uint32_t SelectLowestRttPort(const std::vector<RttRouteEntry>& routes);

    /**
     * Dispatch to the currently configured selection policy.
     *
     * @param routes Candidate routes.
     * @return Selected output interface.
     */
    uint32_t SelectPort(const std::vector<RttRouteEntry>& routes);

    /**
     * Process data leaving a source leaf, tag it, and select its uplink.
     *
     * @param packet Packet being forwarded.
     * @param flowKey Canonical transport flow key.
     * @param seq TCP sequence number.
     * @param now Current simulation time.
     * @param routes Candidate routes.
     * @param header IPv4 header for the packet.
     * @param ucb Unicast forwarding callback.
     */
    void HandleOutgoingData(Ptr<Packet> packet,
                            const BidirectionalFlowKey& flowKey,
                            uint32_t seq,
                            Time now,
                            const std::vector<RttRouteEntry>& routes,
                            const Ipv4Header& header,
                            UnicastForwardCallback ucb);

    /**
     * Process an ACK leaving a destination leaf and preserve its return path.
     *
     * @param packet Packet being forwarded.
     * @param flowKey Canonical transport flow key.
     * @param ackNo TCP acknowledgement number.
     * @param now Current simulation time.
     * @param routes Candidate routes.
     * @param header IPv4 header for the packet.
     * @param ucb Unicast forwarding callback.
     */
    void HandleOutgoingAck(Ptr<Packet> packet,
                           const BidirectionalFlowKey& flowKey,
                           uint32_t ackNo,
                           Time now,
                           const std::vector<RttRouteEntry>& routes,
                           const Ipv4Header& header,
                           UnicastForwardCallback ucb);

    /**
     * Save a received data-packet tag until its ACK passes through this leaf.
     *
     * @param packet Packet received at the destination leaf.
     * @param flowKey Canonical transport flow key.
     * @param seq TCP sequence number.
     * @param tag RTT tag carried by the data packet.
     * @param now Current simulation time.
     */
    void HandleIncomingData(Ptr<Packet> packet,
                            const BidirectionalFlowKey& flowKey,
                            uint32_t seq,
                            const Ipv4RttTag& tag,
                            Time now);

    /**
     * Use a returning ACK to update the RTT of its recorded data path.
     *
     * @param packet Packet received at the source leaf.
     * @param flowKey Canonical transport flow key.
     * @param ackNo TCP acknowledgement number.
     * @param tag RTT tag carried by the ACK.
     * @param now Current simulation time.
     */
    void HandleIncomingAck(Ptr<Packet> packet,
                           const BidirectionalFlowKey& flowKey,
                           uint32_t ackNo,
                           const Ipv4RttTag& tag,
                           Time now);

    /**
     * Record the uplink selected for a flowlet.
     *
     * @param flowKey Canonical transport flow key.
     * @param port Selected output interface.
     * @param now Current simulation time.
     */
    void UpdateFlowlet(const BidirectionalFlowKey& flowKey, uint32_t port, Time now);

    /**
     * Invoke the IPv4 unicast callback using an output interface.
     *
     * @param packet Packet being forwarded.
     * @param port Selected output interface.
     * @param header IPv4 header for the packet.
     * @param ucb Unicast forwarding callback.
     */
    void Forward(Ptr<Packet> packet,
                 uint32_t port,
                 const Ipv4Header& header,
                 UnicastForwardCallback ucb);

    /**
     * Build a direction-independent TCP flow key.
     *
     * @param srcIp Source IPv4 address.
     * @param srcPort Source TCP port.
     * @param dstIp Destination IPv4 address.
     * @param dstPort Destination TCP port.
     * @param protocol IPv4 transport protocol number.
     * @return Canonical bidirectional flow key.
     */
    BidirectionalFlowKey MakeFlowKey(Ipv4Address srcIp,
                                     uint16_t srcPort,
                                     Ipv4Address dstIp,
                                     uint16_t dstPort,
                                     uint8_t protocol);

    /**
     * Write a flow key to the diagnostic stream.
     *
     * @param key Flow key to print.
     */
    void PrintFlowKeyLine(const BidirectionalFlowKey& key) const;

    Ptr<UniformRandomVariable> m_rand; ///< Random source for randomized policies.
    Ptr<Ipv4> m_ipv4;                  ///< IPv4 object served by this protocol.
    bool m_isLeaf;                     ///< True when this router acts as a leaf switch.
    uint32_t m_leafId;                 ///< Leaf identifier carried in RTT tags.
    double m_alpha;                    ///< Weight assigned to a new RTT sample.
    bool m_timeouttrue;                ///< Enables the prototype timeout behavior.
    DataRate m_C;                      ///< Common link capacity for initial RTT state.
    SelectionMode m_mode;              ///< Selection policy for new flowlets.

    /// Canonical flow key -> cached flowlet; values are owned by this object.
    std::map<BidirectionalFlowKey, Flowlet*> m_flowletTable;

    /// Output interface -> most recent raw and smoothed RTT measurements.
    std::map<uint32_t, PortRttState> m_portRttMap;

    /// Output interface -> configured initial RTT baseline.
    std::map<uint32_t, Time> m_baseRttMap;

    std::vector<RttRouteEntry> m_routeEntryList; ///< Configured destination routes.

    /// Flow key -> TCP sequence number -> state awaiting a returning ACK.
    std::map<BidirectionalFlowKey, std::map<uint32_t, PacketState>> m_pendingPackets;

    /// Flow key -> TCP sequence number -> RTT tag retained at the destination leaf.
    std::map<BidirectionalFlowKey, std::map<uint32_t, StoredTag>> m_destStorage;

    /// Flow key -> uplink on which the corresponding data packet arrived.
    std::map<BidirectionalFlowKey, uint32_t> m_reversePath;

    /// Flow key -> TCP sequence number -> data-packet state retained by a leaf.
    std::map<BidirectionalFlowKey, std::map<uint32_t, PacketState>> m_packetTable;

    std::map<BidirectionalFlowKey, ReverseInfo> m_reverseInfo; ///< Per-flow ACK metadata.
};

} // namespace ns3

#endif /* IPV4_RTT_ROUTING_H */
