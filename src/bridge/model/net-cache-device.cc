/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Gustavo Carneiro  <gjc@inescporto.pt>
 */
#include "net-cache-device.h"
#include "ns3/node.h"
#include "ns3/channel.h"
#include "ns3/packet.h"
#include "ns3/log.h"
#include "ns3/boolean.h"
#include "ns3/simulator.h"
#include "ns3/uinteger.h"
#include "ns3/ipv4-header.h"
#include "ns3/tcp-header.h"
#include "ns3/udp-header.h"
#include <iostream>
#include <fstream>

/**
 * \file
 * \ingroup bridge
 * ns3::NetCacheDevice implementation.
 */

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("NetCacheDevice");

NS_OBJECT_ENSURE_REGISTERED (NetCacheDevice);


TypeId
NetCacheDevice::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::NetCacheDevice")
    .SetParent<NetDevice> ()
    .SetGroupName("Bridge")
    .AddConstructor<NetCacheDevice> ()
    .AddAttribute ("Mtu", "The MAC-level Maximum Transmission Unit",
                   UintegerValue (1500),
                   MakeUintegerAccessor (&NetCacheDevice::SetMtu,
                                         &NetCacheDevice::GetMtu),
                   MakeUintegerChecker<uint16_t> ())
    .AddAttribute ("EnableLearning",
                   "Enable the learning mode of the Learning Bridge",
                   BooleanValue (true),
                   MakeBooleanAccessor (&NetCacheDevice::m_enableLearning),
                   MakeBooleanChecker ())
    .AddAttribute ("ExpirationTime",
                   "Time it takes for learned MAC state entry to expire.",
                   TimeValue (Seconds (300)),
                   MakeTimeAccessor (&NetCacheDevice::m_expirationTime),
                   MakeTimeChecker ())
  ;
  return tid;
}


NetCacheDevice::NetCacheDevice ()
  : m_node (0),
    m_ifIndex (0),
    m_change_direction (false)
{
  NS_LOG_FUNCTION_NOARGS ();
  m_channel = CreateObject<BridgeChannel> ();
}

NetCacheDevice::NetCacheDevice (std::vector<std::string> keys)
  : m_node (0),
    m_ifIndex (0),
    m_change_direction (false)
{
  NS_LOG_FUNCTION_NOARGS ();
  m_channel = CreateObject<BridgeChannel> ();
  for (int i=0;i<keys.size();i++){
    m_cache[keys[i].substr(0,6)] = std::make_pair<std::string, bool>("v",false);
  }
}

NetCacheDevice::~NetCacheDevice()
{
  NS_LOG_FUNCTION_NOARGS ();
}

void
NetCacheDevice::DoDispose ()
{
  NS_LOG_FUNCTION_NOARGS ();
  for (std::vector< Ptr<NetDevice> >::iterator iter = m_ports.begin (); iter != m_ports.end (); iter++)
    {
      *iter = 0;
    }
  m_ports.clear ();
  m_channel = 0;
  m_node = 0;
  NetDevice::DoDispose ();
}

void
NetCacheDevice::ReceiveFromDevice (Ptr<NetDevice> incomingPort, Ptr<const Packet> packet, uint16_t protocol,
                                    Address const &src, Address const &dst, PacketType packetType)
{
  NS_LOG_FUNCTION_NOARGS ();
  NS_LOG_DEBUG ("UID is " << packet->GetUid ());

  Mac48Address src48 = Mac48Address::ConvertFrom (src);
  Mac48Address dst48 = Mac48Address::ConvertFrom (dst);

  if (!m_promiscRxCallback.IsNull ())
    {
      m_promiscRxCallback (this, packet, protocol, src, dst, packetType);
    }

  switch (packetType)
    {
    case PACKET_HOST:
      if (dst48 == m_address)
        {
          Learn (src48, incomingPort);
          m_rxCallback (this, packet, protocol, src);
        }
      break;

    case PACKET_BROADCAST:
    case PACKET_MULTICAST:
      m_rxCallback (this, packet, protocol, src);
      ForwardBroadcast (incomingPort, packet, protocol, src48, dst48);
      break;

    case PACKET_OTHERHOST:
      if (dst48 == m_address)
        {
          Learn (src48, incomingPort);
          m_rxCallback (this, packet, protocol, src);
        }
      else
        {
          if(protocol != 0x0800){
            ForwardUnicast (incomingPort, packet, protocol, src48, dst48);
            break;
          }
          Ptr<const Packet> pkt = P4(packet);
          if(m_change_direction){
            ForwardUnicast (GetLearnedState(dst48), pkt, protocol, dst48, src48);
            m_change_direction = false;
          }
          else{
            ForwardUnicast (incomingPort, pkt, protocol, src48, dst48);
          }
        }
      break;
    }
}

Ptr<const Packet> NetCacheDevice::P4 (Ptr<const Packet> pkt){
  Ptr<Packet> packet = pkt->Copy ();

  uint32_t len = packet->GetSize();
  uint8_t *buf = new uint8_t[len];
  packet->CopyData(buf, len);

  int offset = 28;
  uint8_t op = buf[offset+2];
  std::string key = std::string((char*)(&buf[offset+3]), 6);
  std::string value = std::string((char*)(&buf[offset+9]), 4);

  if(m_cache.find(key)!=m_cache.end()){
    if (op == 2) {
        m_cache[key].second = 0;
    }
    else if (op == 3 || op == 4) {
        m_cache[key].second = 1;
    }
    if (m_cache[key].second == 1) {
        if (op == 1) {
            memcpy(&buf[offset+9], m_cache[key].first.c_str(), 4);
            buf[offset+2] = (uint8_t)5;
            uint32_t* srcip = ((uint32_t*)(buf+12));
            uint32_t* destip = ((uint32_t*)(buf+16));
            uint32_t tempip = *srcip;
            *srcip = *destip;
            *destip = tempip;
            uint16_t* srcp = ((uint16_t*)(buf+20));
            uint16_t* destp = ((uint16_t*)(buf+22));
            uint16_t tempp = *srcp;
            *srcp = *destp;
            *destp = tempp;
            m_change_direction = true;
        }
        else if (op == 3 || op == 4) {
            m_cache[key].first = value;
        }
    }
  }

  packet = Create<Packet>(buf, len);
  return packet;
}

void
NetCacheDevice::ForwardUnicast (Ptr<NetDevice> incomingPort, Ptr<const Packet> packet,
                                 uint16_t protocol, Mac48Address src, Mac48Address dst)
{
  NS_LOG_FUNCTION_NOARGS ();
  NS_LOG_DEBUG ("LearningBridgeForward (incomingPort=" << incomingPort->GetInstanceTypeId ().GetName ()
                                                       << ", packet=" << packet << ", protocol="<<protocol
                                                       << ", src=" << src << ", dst=" << dst << ")");

  Learn (src, incomingPort);
  Ptr<NetDevice> outPort = GetLearnedState (dst);
  if (outPort != NULL && outPort != incomingPort)
    {
      NS_LOG_LOGIC ("Learning bridge state says to use port `" << outPort->GetInstanceTypeId ().GetName () << "'");
      outPort->SendFrom (packet->Copy (), src, dst, protocol);
    }
  else
    {
      NS_LOG_LOGIC ("No learned state: send through all ports");
      for (std::vector< Ptr<NetDevice> >::iterator iter = m_ports.begin ();
           iter != m_ports.end (); iter++)
        {
          Ptr<NetDevice> port = *iter;
          if (port != incomingPort)
            {
              NS_LOG_LOGIC ("LearningBridgeForward (" << src << " => " << dst << "): " 
                                                      << incomingPort->GetInstanceTypeId ().GetName ()
                                                      << " --> " << port->GetInstanceTypeId ().GetName ()
                                                      << " (UID " << packet->GetUid () << ").");
              port->SendFrom (packet->Copy (), src, dst, protocol);
            }
        }
    }
}

void
NetCacheDevice::ForwardBroadcast (Ptr<NetDevice> incomingPort, Ptr<const Packet> packet,
                                   uint16_t protocol, Mac48Address src, Mac48Address dst)
{
  NS_LOG_FUNCTION_NOARGS ();
  NS_LOG_DEBUG ("LearningBridgeForward (incomingPort=" << incomingPort->GetInstanceTypeId ().GetName ()
                                                       << ", packet=" << packet << ", protocol="<<protocol
                                                       << ", src=" << src << ", dst=" << dst << ")");
  Learn (src, incomingPort);

  for (std::vector< Ptr<NetDevice> >::iterator iter = m_ports.begin ();
       iter != m_ports.end (); iter++)
    {
      Ptr<NetDevice> port = *iter;
      if (port != incomingPort)
        {
          NS_LOG_LOGIC ("LearningBridgeForward (" << src << " => " << dst << "): " 
                                                  << incomingPort->GetInstanceTypeId ().GetName ()
                                                  << " --> " << port->GetInstanceTypeId ().GetName ()
                                                  << " (UID " << packet->GetUid () << ").");
          port->SendFrom (packet->Copy (), src, dst, protocol);
        }
    }
}

void NetCacheDevice::Learn (Mac48Address source, Ptr<NetDevice> port)
{
  NS_LOG_FUNCTION_NOARGS ();
  if (m_enableLearning)
    {
      LearnedState &state = m_learnState[source];
      state.associatedPort = port;
      state.expirationTime = Simulator::Now () + m_expirationTime;
    }
}

Ptr<NetDevice> NetCacheDevice::GetLearnedState (Mac48Address source)
{
  NS_LOG_FUNCTION_NOARGS ();
  if (m_enableLearning)
    {
      Time now = Simulator::Now ();
      std::map<Mac48Address, LearnedState>::iterator iter =
        m_learnState.find (source);
      if (iter != m_learnState.end ())
        {
          LearnedState &state = iter->second;
          if (state.expirationTime > now)
            {
              return state.associatedPort;
            }
          else
            {
              m_learnState.erase (iter);
            }
        }
    }
  return NULL;
}

uint32_t
NetCacheDevice::GetNBridgePorts (void) const
{
  NS_LOG_FUNCTION_NOARGS ();
  return m_ports.size ();
}


Ptr<NetDevice>
NetCacheDevice::GetBridgePort (uint32_t n) const
{
  NS_LOG_FUNCTION_NOARGS ();
  return m_ports[n];
}

void 
NetCacheDevice::AddBridgePort (Ptr<NetDevice> bridgePort)
{
  NS_LOG_FUNCTION_NOARGS ();
  NS_ASSERT (bridgePort != this);
  if (!Mac48Address::IsMatchingType (bridgePort->GetAddress ()))
    {
      NS_FATAL_ERROR ("Device does not support eui 48 addresses: cannot be added to bridge.");
    }
  if (!bridgePort->SupportsSendFrom ())
    {
      NS_FATAL_ERROR ("Device does not support SendFrom: cannot be added to bridge.");
    }
  if (m_address == Mac48Address ())
    {
      m_address = Mac48Address::ConvertFrom (bridgePort->GetAddress ());
    }

  NS_LOG_DEBUG ("RegisterProtocolHandler for " << bridgePort->GetInstanceTypeId ().GetName ());
  m_node->RegisterProtocolHandler (MakeCallback (&NetCacheDevice::ReceiveFromDevice, this),
                                   0, bridgePort, true);
  m_ports.push_back (bridgePort);
  m_channel->AddChannel (bridgePort->GetChannel ());
}

void 
NetCacheDevice::SetIfIndex (const uint32_t index)
{
  NS_LOG_FUNCTION_NOARGS ();
  m_ifIndex = index;
}

uint32_t 
NetCacheDevice::GetIfIndex (void) const
{
  NS_LOG_FUNCTION_NOARGS ();
  return m_ifIndex;
}

Ptr<Channel> 
NetCacheDevice::GetChannel (void) const
{
  NS_LOG_FUNCTION_NOARGS ();
  return m_channel;
}

void
NetCacheDevice::SetAddress (Address address)
{
  NS_LOG_FUNCTION_NOARGS ();
  m_address = Mac48Address::ConvertFrom (address);
}

Address 
NetCacheDevice::GetAddress (void) const
{
  NS_LOG_FUNCTION_NOARGS ();
  return m_address;
}

bool 
NetCacheDevice::SetMtu (const uint16_t mtu)
{
  NS_LOG_FUNCTION_NOARGS ();
  m_mtu = mtu;
  return true;
}

uint16_t 
NetCacheDevice::GetMtu (void) const
{
  NS_LOG_FUNCTION_NOARGS ();
  return m_mtu;
}


bool 
NetCacheDevice::IsLinkUp (void) const
{
  NS_LOG_FUNCTION_NOARGS ();
  return true;
}


void 
NetCacheDevice::AddLinkChangeCallback (Callback<void> callback)
{}


bool 
NetCacheDevice::IsBroadcast (void) const
{
  NS_LOG_FUNCTION_NOARGS ();
  return true;
}


Address
NetCacheDevice::GetBroadcast (void) const
{
  NS_LOG_FUNCTION_NOARGS ();
  return Mac48Address ("ff:ff:ff:ff:ff:ff");
}

bool
NetCacheDevice::IsMulticast (void) const
{
  NS_LOG_FUNCTION_NOARGS ();
  return true;
}

Address
NetCacheDevice::GetMulticast (Ipv4Address multicastGroup) const
{
  NS_LOG_FUNCTION (this << multicastGroup);
  Mac48Address multicast = Mac48Address::GetMulticast (multicastGroup);
  return multicast;
}


bool 
NetCacheDevice::IsPointToPoint (void) const
{
  NS_LOG_FUNCTION_NOARGS ();
  return false;
}

bool 
NetCacheDevice::IsBridge (void) const
{
  NS_LOG_FUNCTION_NOARGS ();
  return true;
}


bool 
NetCacheDevice::Send (Ptr<Packet> packet, const Address& dest, uint16_t protocolNumber)
{
  NS_LOG_FUNCTION_NOARGS ();
  return SendFrom (packet, m_address, dest, protocolNumber);
}

bool 
NetCacheDevice::SendFrom (Ptr<Packet> packet, const Address& src, const Address& dest, uint16_t protocolNumber)
{
  NS_LOG_FUNCTION_NOARGS ();
  Mac48Address dst = Mac48Address::ConvertFrom (dest); 

  // try to use the learned state if data is unicast
  if (!dst.IsGroup ())
    {
      Ptr<NetDevice> outPort = GetLearnedState (dst);
      if (outPort != NULL) 
        {
          outPort->SendFrom (packet, src, dest, protocolNumber);
          return true;
        }
    }

  // data was not unicast or no state has been learned for that mac
  // address => flood through all ports.
  Ptr<Packet> pktCopy;
  for (std::vector< Ptr<NetDevice> >::iterator iter = m_ports.begin ();
       iter != m_ports.end (); iter++)
    {
      pktCopy = packet->Copy ();
      Ptr<NetDevice> port = *iter;
      port->SendFrom (pktCopy, src, dest, protocolNumber);
    }

  return true;
}


Ptr<Node> 
NetCacheDevice::GetNode (void) const
{
  NS_LOG_FUNCTION_NOARGS ();
  return m_node;
}


void 
NetCacheDevice::SetNode (Ptr<Node> node)
{
  NS_LOG_FUNCTION_NOARGS ();
  m_node = node;
}


bool 
NetCacheDevice::NeedsArp (void) const
{
  NS_LOG_FUNCTION_NOARGS ();
  return true;
}


void 
NetCacheDevice::SetReceiveCallback (NetDevice::ReceiveCallback cb)
{
  NS_LOG_FUNCTION_NOARGS ();
  m_rxCallback = cb;
}

void 
NetCacheDevice::SetPromiscReceiveCallback (NetDevice::PromiscReceiveCallback cb)
{
  NS_LOG_FUNCTION_NOARGS ();
  m_promiscRxCallback = cb;
}

bool
NetCacheDevice::SupportsSendFrom () const
{
  NS_LOG_FUNCTION_NOARGS ();
  return true;
}

Address NetCacheDevice::GetMulticast (Ipv6Address addr) const
{
  NS_LOG_FUNCTION (this << addr);
  return Mac48Address::GetMulticast (addr);
}

} // namespace ns3
