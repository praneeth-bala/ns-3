/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2012 University of Washington, 2012 INRIA
 *
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
 */

#include <iostream>

#include "ns3/abort.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-apps-module.h"
#include "ns3/ipv4-static-routing-helper.h"
#include "ns3/ipv4-list-routing-helper.h"
#include "ns3/bridge-net-device.h"
#include "ns3/http-header.h"
#include "ns3/cosim.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("CosimBridgeExample");

std::vector<std::string> cosimPortPaths;

bool AddCosimPort (std::string arg)
{
  cosimPortPaths.push_back (arg);
  return true;
}

class MyApp : public Application 
{
public:

  MyApp ();
  virtual ~MyApp();

  void Setup (Ptr<Socket> socket, Address address, uint32_t packetSize);

private:
  virtual void StartApplication (void);
  virtual void StopApplication (void);
  virtual void rcb (Ptr<Socket> s);

  // void ScheduleTx (void);
  void SendPacket (void);

  Ptr<Socket>     m_socket;
  Address         m_peer;
  uint32_t        m_packetSize;
  EventId         m_sendEvent;
  bool            m_running;
  uint32_t        m_packetsSent;
  HttpHeader      m_httpHeader;
};

MyApp::MyApp ()
  : m_socket (0), 
    m_peer (), 
    m_packetSize (0), 
    m_sendEvent (), 
    m_running (false), 
    m_packetsSent (0)
{
}

MyApp::~MyApp()
{
  m_socket = 0;
}

void
MyApp::Setup (Ptr<Socket> socket, Address address, uint32_t packetSize)
{
  m_socket = socket;
  m_peer = address;
  m_packetSize = packetSize;
  m_socket->SetRecvCallback(MakeCallback(&MyApp::rcb, this));
}

void
MyApp::StartApplication (void)
{
  m_running = true;
  m_packetsSent = 0;
  m_socket->Bind ();
  m_socket->Connect (m_peer);
  SendPacket ();
}

void 
MyApp::StopApplication (void)
{
  m_running = false;

  if (m_sendEvent.IsRunning ())
    {
      Simulator::Cancel (m_sendEvent);
    }

  if (m_socket)
    {
      m_socket->Close ();
    }
}

void 
MyApp::SendPacket (void)
{
  Ptr<Packet> packet = Create<Packet> (m_packetSize);
  m_httpHeader.SetRequest(true);
  m_httpHeader.SetMethod("GET");
  m_httpHeader.SetUrl("/");
  m_httpHeader.SetVersion("HTTP/1.1");
  packet->AddHeader (m_httpHeader);
  m_socket->Send (packet);
}

void MyApp::rcb(Ptr<Socket> s){
  HttpHeader httpHeaderIn;
  Ptr<Packet> p = s->Recv();

  uint8_t *buffer = new uint8_t[p->GetSize ()];
  int size = p->CopyData(buffer, p->GetSize ());
  std::string st = std::string(buffer, buffer+p->GetSize());
  std::cerr<<"Received:"<<st<<"\n";

  // std::string statusCode = httpHeaderIn.GetStatusCode();

  uint32_t bytesReceived = p->GetSize ();
  std::cerr << "Size " << bytesReceived << "\n";
  std::flush(std::cerr);
}


int
main (int argc, char *argv[])
{
  Time::SetResolution (Time::Unit::PS);

  CommandLine cmd (__FILE__);
  cmd.AddValue ("CosimPort", "Add a cosim ethernet port to the bridge",
      MakeCallback (&AddCosimPort));
  cmd.Parse (argc, argv);

  LogComponentEnable("CosimNetDevice", LOG_LEVEL_ALL);

  Ipv4Address hostIp ("10.0.0.3");
  Ipv4Mask hostMask ("255.255.255.0");

  GlobalValue::Bind ("ChecksumEnabled", BooleanValue (true));

  NS_LOG_INFO ("Create Node");
  Ptr<Node> node = CreateObject<Node> ();

  NS_LOG_INFO ("Create BridgeDevice");
  Ptr<BridgeNetDevice> bridge = CreateObject<BridgeNetDevice> ();
  Mac48Address::Allocate ();
  bridge->SetAddress (Mac48Address::Allocate ());
  node->AddDevice (bridge);

  NS_LOG_INFO ("Add Internet Stack");
  InternetStackHelper internetStackHelper;
  internetStackHelper.Install (node);

  NS_LOG_INFO ("Create IPv4 Interface");
  Ptr<Ipv4> ipv4 = node->GetObject<Ipv4> ();
  uint32_t interface = ipv4->AddInterface (bridge);
  Ipv4InterfaceAddress address = Ipv4InterfaceAddress (hostIp, hostMask);
  ipv4->AddAddress (interface, address);
  ipv4->SetMetric (interface, 1);
  ipv4->SetUp (interface);


  uint16_t sinkPort = 9000;
  Address sinkAddress (InetSocketAddress (Ipv4Address ("10.0.0.2"), sinkPort));
  Ptr<Socket> ns3TcpSocket = Socket::CreateSocket (node, TcpSocketFactory::GetTypeId ());
  Ptr<MyApp> app = CreateObject<MyApp> ();
  app->Setup (ns3TcpSocket, sinkAddress, 100);
  node->AddApplication (app);
  app->SetStartTime (Seconds (1.));
  app->SetStopTime (Seconds (20.));

  NS_LOG_INFO ("Create CosimDevices and add them to bridge");
  std::vector <Ptr<CosimNetDevice>> cosimDevs;
  for (std::string cpp : cosimPortPaths) {
    Ptr<CosimNetDevice> device = CreateObject<CosimNetDevice> ();
    device->SetAttribute ("UnixSocket", StringValue (cpp));
    node->AddDevice (device);
    bridge->AddBridgePort (device);
    device->Start ();
  }

  NS_LOG_INFO ("Run Emulation.");
  Simulator::Run ();
  Simulator::Destroy ();
  NS_LOG_INFO ("Done.");
}
