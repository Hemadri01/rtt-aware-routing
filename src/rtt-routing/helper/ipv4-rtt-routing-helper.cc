/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include "ipv4-rtt-routing-helper.h"
#include "ns3/log.h"
#include "ns3/node.h"
#include "ns3/ipv4.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("Ipv4RttRoutingHelper");

Ipv4RttRoutingHelper::Ipv4RttRoutingHelper ()
{
  m_factory.SetTypeId ("ns3::Ipv4RttRouting");
}

Ipv4RttRoutingHelper::Ipv4RttRoutingHelper (const Ipv4RttRoutingHelper &o)
  : Ipv4RoutingHelper (o)
{
  m_factory = o.m_factory;
}

Ipv4RttRoutingHelper::~Ipv4RttRoutingHelper ()
{
}

Ipv4RttRoutingHelper*
Ipv4RttRoutingHelper::Copy (void) const
{
  return new Ipv4RttRoutingHelper (*this);
}

Ptr<Ipv4RoutingProtocol>
Ipv4RttRoutingHelper::Create (Ptr<Node> node) const
{
  Ptr<Ipv4RttRouting> routing = m_factory.Create<Ipv4RttRouting> ();
  Ptr<Ipv4> ipv4 = node->GetObject<Ipv4> ();
  NS_ASSERT(ipv4 != nullptr);

  routing->SetIpv4 (ipv4);

  return routing;
}

void
Ipv4RttRoutingHelper::Set (std::string name, const AttributeValue &value)
{
  m_factory.Set (name, value);
}

Ptr<Ipv4RttRouting>
Ipv4RttRoutingHelper::GetRttRouting (Ptr<Ipv4> ipv4) const
{
  Ptr<Ipv4RoutingProtocol> proto = ipv4->GetRoutingProtocol ();
  return DynamicCast<Ipv4RttRouting> (proto);
}

void
Ipv4RttRoutingHelper::SetLeaf (Ptr<Node> node, uint32_t leafId)
{
  Ptr<Ipv4> ipv4 = node->GetObject<Ipv4> ();
  Ptr<Ipv4RttRouting> rtt = GetRttRouting (ipv4);
  if (rtt)
  {
    rtt->SetLeafId (leafId);
  }
}

void
Ipv4RttRoutingHelper::AddRoute (Ptr<Node> node,
                                Ipv4Address network,
                                Ipv4Mask mask,
                                uint32_t port)
{
  Ptr<Ipv4> ipv4 = node->GetObject<Ipv4> ();
  Ptr<Ipv4RttRouting> rtt = GetRttRouting (ipv4);
  if (rtt)
  {
    rtt->AddRoute (network, mask, port);
  }
}

void
Ipv4RttRoutingHelper::SetSelectionMode (Ptr<Node> node, Ipv4RttRouting::SelectionMode mode)
{
  Ptr<Ipv4> ipv4 = node->GetObject<Ipv4> ();
  Ptr<Ipv4RttRouting> rtt = GetRttRouting (ipv4);
  if (rtt)
  {
    rtt->SetSelectionMode (mode);
  }
}

void
Ipv4RttRoutingHelper::SetAlpha (Ptr<Node> node, double alpha)
{
  Ptr<Ipv4> ipv4 = node->GetObject<Ipv4> ();
  Ptr<Ipv4RttRouting> rtt = GetRttRouting (ipv4);
  if (rtt)
  {
    rtt->SetAlpha (alpha);
  }
}

} // namespace ns3