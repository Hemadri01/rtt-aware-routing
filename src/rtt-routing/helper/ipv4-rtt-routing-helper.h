/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef IPV4_RTT_ROUTING_HELPER_H
#define IPV4_RTT_ROUTING_HELPER_H

#include "ns3/ipv4-routing-helper.h"
#include "ns3/ipv4-rtt-routing.h"
#include "ns3/object-factory.h"

#include <string>

namespace ns3
{

/**
 * @ingroup rtt-routing
 * @brief Helper that creates, configures, and retrieves RTT routing instances.
 */
class Ipv4RttRoutingHelper : public Ipv4RoutingHelper
{
  public:
    /** Create an RTT routing helper. */
    Ipv4RttRoutingHelper();

    /**
     * Copy an RTT routing helper.
     *
     * @param other Helper to copy.
     */
    Ipv4RttRoutingHelper(const Ipv4RttRoutingHelper& other);

    ~Ipv4RttRoutingHelper() override;

    /**
     * Create an independent copy of this helper.
     *
     * @return Dynamically allocated helper copy.
     */
    Ipv4RttRoutingHelper* Copy() const override;

    /**
     * Create an RTT routing protocol for an IPv4 node.
     *
     * @param node Node for which the protocol is being created.
     * @return New RTT routing protocol.
     */
    Ptr<Ipv4RoutingProtocol> Create(Ptr<Node> node) const override;

    /**
     * Set an RTT routing object-factory attribute before protocol creation.
     *
     * @param name Attribute name.
     * @param value Attribute value.
     */
    void Set(std::string name, const AttributeValue& value);

    /**
     * Mark a node's RTT routing protocol as a leaf switch.
     *
     * @param node Node whose routing protocol is configured.
     * @param leafId Leaf identifier carried in RTT tags.
     */
    void SetLeaf(Ptr<Node> node, uint32_t leafId);

    /**
     * Add a destination prefix and output interface to a node's RTT protocol.
     *
     * @param node Node whose routing protocol is configured.
     * @param network Destination network prefix.
     * @param mask Destination network mask.
     * @param port Output interface index.
     */
    void AddRoute(Ptr<Node> node, Ipv4Address network, Ipv4Mask mask, uint32_t port);

    /**
     * Set a node's RTT path-selection policy.
     *
     * @param node Node whose routing protocol is configured.
     * @param mode RTT path-selection policy.
     */
    void SetSelectionMode(Ptr<Node> node, Ipv4RttRouting::SelectionMode mode);

    /**
     * Set a node's RTT smoothing coefficient.
     *
     * @param node Node whose routing protocol is configured.
     * @param alpha Weight assigned to a new RTT sample.
     */
    void SetAlpha(Ptr<Node> node, double alpha);

    /**
     * Return the RTT routing protocol installed directly on an IPv4 object.
     *
     * @param ipv4 IPv4 object to inspect.
     * @return Installed RTT protocol, or null when a different protocol is used.
     */
    Ptr<Ipv4RttRouting> GetRttRouting(Ptr<Ipv4> ipv4) const;

  private:
    ObjectFactory m_factory; ///< Factory used to create RTT routing protocols.
};

} // namespace ns3

#endif /* IPV4_RTT_ROUTING_HELPER_H */
