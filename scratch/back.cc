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
#include <fstream>
#include "ns3/trace-helper.h"
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
#include "ns3/point-to-point-helper.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("CosimBridgeExample");

std::vector<std::string> cosimLeftPaths;
std::vector<std::string> cosimRightPaths;


bool AddCosimLeftPort (std::string arg)
{
  cosimLeftPaths.push_back (arg);
  return true;
}

bool AddCosimRightPort (std::string arg)
{
  cosimRightPaths.push_back (arg);
  return true;
}

class MyApp : public Application 
{
public:

  MyApp (std::string url);
  virtual ~MyApp();

  void Setup (Ptr<Socket> socket, Address address, uint32_t packetSize);

private:
  virtual void StartApplication (void);
  virtual void StopApplication (void);
  virtual void rcb (Ptr<Socket> s);
  virtual void Close (Ptr<Socket> s);

  // void ScheduleTx (void);
  void SendPacket (void);

  Ptr<Socket>     m_socket;
  std::string     m_url;
  Address         m_peer;
  uint32_t        m_packetSize;
  EventId         m_sendEvent;
  bool            m_running;
  uint32_t        m_packetsSent;
  HttpHeader      m_httpHeader;
  std::ofstream   m_file;
  int             m_bytes;
};

MyApp::MyApp (std::string url)
  : m_socket (0), 
    m_peer (), 
    m_packetSize (0), 
    m_sendEvent (), 
    m_running (false), 
    m_packetsSent (0),
    m_url(url),
    m_bytes(0)
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
  m_socket->SetCloseCallbacks(MakeCallback(&MyApp::Close, this), MakeCallback(&MyApp::Close, this));
  m_file.open("/workspaces/simbricks/env/"+m_url+std::to_string((unsigned long long)((void *)this)));
  m_url = "./"+m_url;
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
  m_file.close();

  // if (m_sendEvent.IsRunning ())
  //   {
  //     Simulator::Cancel (m_sendEvent);
  //   }

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
  m_httpHeader.SetUrl(m_url);
  m_httpHeader.SetVersion("HTTP/1.0");
  packet->AddHeader (m_httpHeader);
  m_socket->Send (packet);
}

void MyApp::Close(Ptr<Socket> s){
  m_socket->ShutdownSend();
  m_file.close();
}

void MyApp::rcb(Ptr<Socket> s){
  HttpHeader httpHeaderIn;
  Ptr<Packet> p = s->Recv();

  uint8_t *buffer = new uint8_t[p->GetSize ()];
  int size = p->CopyData(buffer, p->GetSize ());
  std::string st = std::string(buffer, buffer+p->GetSize());
  // std::cerr<<"Received:"<<st<<"\n";

  // std::string statusCode = httpHeaderIn.GetStatusCode();

  m_bytes += p->GetSize ();
  // std::cerr << m_bytes <<"\n";
  m_file << st;
  std::flush(m_file);
  delete buffer;
}


int
main (int argc, char *argv[])
{
  Time::SetResolution (Time::Unit::PS);

  Time linkLatency(NanoSeconds (500));
  DataRate linkRate("1000Mb/s");
  double ecnTh = 200000;

  CommandLine cmd (__FILE__);
  cmd.AddValue ("LinkLatency", "Propagation delay through link", linkLatency);
  cmd.AddValue ("LinkRate", "Link bandwidth", linkRate);
  cmd.AddValue ("EcnTh", "ECN Threshold queue size", ecnTh);
  cmd.AddValue ("CosimPortLeft", "Add a cosim ethernet port to the bridge",
      MakeCallback (&AddCosimLeftPort));
  cmd.AddValue ("CosimPortRight", "Add a cosim ethernet port to the bridge",
      MakeCallback (&AddCosimRightPort));
  cmd.Parse (argc, argv);

  // LogComponentEnable("CosimNetDevice", LOG_LEVEL_ALL);

  GlobalValue::Bind ("ChecksumEnabled", BooleanValue (true));

  NS_LOG_INFO ("Create Nodes");
  Ptr<Node> nodeLeft = CreateObject<Node> ();
  Ptr<Node> nodeRight = CreateObject<Node> ();
  NodeContainer nodes (nodeLeft);
  nodes.Add(nodeRight);
  NS_LOG_INFO ("Node Num: " << nodes.GetN());

  NS_LOG_INFO ("Create BridgeDevice");
  Ptr<BridgeNetDevice> bridgeLeft = CreateObject<BridgeNetDevice> ();
  Ptr<BridgeNetDevice> bridgeRight = CreateObject<BridgeNetDevice> ();
  bridgeLeft->SetAddress (Mac48Address::Allocate ());
  bridgeRight->SetAddress (Mac48Address::Allocate ());
  nodeLeft->AddDevice (bridgeLeft);
  nodeRight->AddDevice (bridgeRight);

  NS_LOG_INFO ("Create simple channel link between the two");
  Ptr<SimpleChannel> ptpChan = CreateObject<SimpleChannel> ();

  SimpleNetDeviceHelper pointToPointSR;
  pointToPointSR.SetQueue("ns3::DevRedQueue", "MaxSize", StringValue("2666p"));
  pointToPointSR.SetQueue("ns3::DevRedQueue", "MinTh", DoubleValue (ecnTh));
  pointToPointSR.SetDeviceAttribute ("DataRate", DataRateValue(linkRate));
  pointToPointSR.SetChannelAttribute ("Delay", TimeValue (linkLatency));

  NetDeviceContainer ptpDev = pointToPointSR.Install (nodes, ptpChan);

  NS_LOG_INFO ("num node device" << nodeLeft->GetNDevices() << " type: ");
  bridgeLeft->AddBridgePort (nodeLeft->GetDevice(1));
  
  bridgeRight->AddBridgePort (nodeRight->GetDevice(1));

  NS_LOG_INFO ("Add Internet Stack");
  InternetStackHelper internetStackHelper;
  internetStackHelper.Install (nodes);

  NS_LOG_INFO ("Create IPv4 Interface");
  Ipv4Address hostIp ("10.0.0.3");
  Ipv4Address clientIp ("10.0.0.4");
  Ipv4Mask hostMask ("255.255.255.0");

  Ptr<Ipv4> ipv4A = nodeLeft->GetObject<Ipv4> ();
  uint32_t interface = ipv4A->AddInterface (bridgeLeft);
  Ipv4InterfaceAddress address = Ipv4InterfaceAddress (hostIp, hostMask);
  ipv4A->AddAddress (interface, address);
  ipv4A->SetMetric (interface, 1);
  ipv4A->SetUp (interface);

  Ptr<Ipv4> ipv4B = nodeRight->GetObject<Ipv4> ();
  interface = ipv4B->AddInterface (bridgeRight);
  address = Ipv4InterfaceAddress (clientIp, hostMask);
  ipv4B->AddAddress (interface, address);
  ipv4B->SetMetric (interface, 1);
  ipv4B->SetUp (interface);

  uint16_t sinkPort = 9000;
  Address sinkAddress (InetSocketAddress (Ipv4Address ("10.0.0.2"), sinkPort));
  Ptr<Socket> ns3TcpSocket1 = Socket::CreateSocket (nodeRight, TcpSocketFactory::GetTypeId ());
  Ptr<MyApp> app1 = CreateObject<MyApp> ("curl.txt");
  app1->Setup (ns3TcpSocket1, sinkAddress, 100);
  nodeRight->AddApplication (app1);
  app1->SetStartTime (Seconds (1.));
  app1->SetStopTime (Seconds (200.));
  
  Ptr<Socket> ns3TcpSocket2 = Socket::CreateSocket (nodeRight, TcpSocketFactory::GetTypeId ());
  Ptr<MyApp> app2 = CreateObject<MyApp> ("hello.txt");
  app2->Setup (ns3TcpSocket2, sinkAddress, 100);
  nodeRight->AddApplication (app2);
  app2->SetStartTime (Seconds (2.));
  app2->SetStopTime (Seconds (200.));

  // Ptr<Socket> ns3TcpSocket3 = Socket::CreateSocket (nodeRight, TcpSocketFactory::GetTypeId ());
  // Ptr<MyApp> app3 = CreateObject<MyApp> ("curl.txt");
  // app3->Setup (ns3TcpSocket3, sinkAddress, 100);
  // nodeRight->AddApplication (app3);
  // app3->SetStartTime (Seconds (1.));
  // app3->SetStopTime (Seconds (200.));

  for (std::string cpp : cosimLeftPaths) {
    Ptr<CosimNetDevice> device = CreateObject<CosimNetDevice> ();
    device->SetAttribute ("UnixSocket", StringValue (cpp));
    nodeLeft->AddDevice (device);
    bridgeLeft->AddBridgePort (device);
    device->Start ();
  }

  for (std::string cpp : cosimRightPaths) {
    Ptr<CosimNetDevice> device = CreateObject<CosimNetDevice> ();
    device->SetAttribute ("UnixSocket", StringValue (cpp));
    nodeRight->AddDevice (device);
    bridgeRight->AddBridgePort (device);
    device->Start ();
  }



  // Ptr<Socket> ns3TcpSocket4 = Socket::CreateSocket (node, TcpSocketFactory::GetTypeId ());
  // Ptr<MyApp> app4 = CreateObject<MyApp> ("trace.pcap");
  // app4->Setup (ns3TcpSocket4, sinkAddress, 100);
  // node->AddApplication (app4);
  // app4->SetStartTime (Seconds (2));
  // app4->SetStopTime (Seconds (200.));

  // NS_LOG_INFO ("Create CosimDevices and add them to bridge");
  // std::vector <Ptr<CosimNetDevice>> cosimDevs;
  // for (std::string cpp : cosimPortPaths) {
  //   Ptr<CosimNetDevice> device = CreateObject<CosimNetDevice> ();
  //   device->SetAttribute ("UnixSocket", StringValue (cpp));
  //   node->AddDevice (device);
  //   bridge->AddBridgePort (device);
  //   device->Start ();
  // }

  // Ipv4StaticRoutingHelper ipv4RoutingHelper;
  // Ptr<Ipv4StaticRouting> staticRoutingA = ipv4RoutingHelper.GetStaticRouting (ipv4A);
  // // The ifIndex for this outbound route is 1; the first p2p link added
  // staticRoutingA->AddHostRouteTo (Ipv4Address ("10.0.0.4"), 2);
  // Ptr<Ipv4StaticRouting> staticRoutingB = ipv4RoutingHelper.GetStaticRouting (ipv4B);
  // // The ifIndex we want on node B is 2; 0 corresponds to loopback, and 1 to the first point to point link
  // staticRoutingB->AddHostRouteTo (Ipv4Address ("10.0.0.2"), 1);

  NS_LOG_INFO ("Run Emulation.");
  Simulator::Run ();
  Simulator::Destroy ();
  NS_LOG_INFO ("Done.");
}
