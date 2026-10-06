/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef IPV4_CONGA_ROUTING_HELPER_H
#define IPV4_CONGA_ROUTING_HELPER_H

#include "ns3/ipv4-conga-routing.h"
#include "ns3/ipv4-routing-helper.h"

namespace ns3
{

/**
 * @ingroup conga-routing
 * @brief Helper that creates and retrieves Ipv4CongaRouting instances.
 */
class Ipv4CongaRoutingHelper : public Ipv4RoutingHelper
{
  public:
    /** Create a CONGA routing helper. */
    Ipv4CongaRoutingHelper();

    /**
     * Copy a CONGA routing helper.
     *
     * @param other Helper to copy.
     */
    Ipv4CongaRoutingHelper(const Ipv4CongaRoutingHelper& other);

    /**
     * Create an independent copy of this helper.
     *
     * @return Dynamically allocated helper copy.
     */
    Ipv4CongaRoutingHelper* Copy() const override;

    /**
     * Create a CONGA routing protocol for an IPv4 node.
     *
     * @param node Node for which the protocol is being created.
     * @return New CONGA routing protocol.
     */
    Ptr<Ipv4RoutingProtocol> Create(Ptr<Node> node) const override;

    /**
     * Return the CONGA routing protocol installed directly on an IPv4 object.
     *
     * @param ipv4 IPv4 object to inspect.
     * @return Installed CONGA protocol, or null when a different protocol is used.
     */
    Ptr<Ipv4CongaRouting> GetCongaRouting(Ptr<Ipv4> ipv4) const;
};

} // namespace ns3

#endif /* IPV4_CONGA_ROUTING_HELPER_H */
