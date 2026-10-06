/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include "ns3/ipv4-rtt-routing.h"
#include "ns3/ipv4-rtt-tag.h"
#include "ns3/packet.h"
#include "ns3/test.h"

using namespace ns3;

class Ipv4RttRoutingTestCase1 : public TestCase
{
public:
  Ipv4RttRoutingTestCase1 ();
  virtual ~Ipv4RttRoutingTestCase1 ();

private:
  virtual void DoRun (void);
};

Ipv4RttRoutingTestCase1::Ipv4RttRoutingTestCase1 ()
  : TestCase ("RTT routing test case")
{
}

Ipv4RttRoutingTestCase1::~Ipv4RttRoutingTestCase1 ()
{
}

void
Ipv4RttRoutingTestCase1::DoRun (void)
{
  Ptr<Ipv4RttRouting> routing =
      CreateObject<Ipv4RttRouting> ();

  NS_TEST_ASSERT_MSG_NE (
      routing,
      nullptr,
      "Routing object creation failed");

  // Exercise serialization across the old signed 32-bit nanosecond boundary.
  Ipv4RttTag tag;
  tag.SetTxTime(Seconds(3));
  tag.SetDestTime(Seconds(3) + MicroSeconds(100));
  Ptr<Packet> packet = Create<Packet>(1);
  packet->AddPacketTag(tag);
  Ipv4RttTag decoded;
  NS_TEST_ASSERT_MSG_EQ(packet->PeekPacketTag(decoded), true, "RTT tag missing");
  NS_TEST_ASSERT_MSG_EQ(decoded.GetTxTime(), Seconds(3), "Transmit timestamp overflowed");
  NS_TEST_ASSERT_MSG_EQ(decoded.GetDestTime(), Seconds(3) + MicroSeconds(100),
                        "Destination timestamp overflowed");
}

class RttRoutingTestSuite : public TestSuite
{
public:
  RttRoutingTestSuite ();
};

RttRoutingTestSuite::RttRoutingTestSuite ()
  : TestSuite ("rtt-routing", Type::UNIT)
{
  AddTestCase (
      new Ipv4RttRoutingTestCase1,
      TestCase::Duration::QUICK);
}

static RttRoutingTestSuite rttRoutingTestSuite;
