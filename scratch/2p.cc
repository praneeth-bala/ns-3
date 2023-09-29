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
 */

#include "ns3/core-module.h"
#include "ns3/cosim-simulator-impl.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/ipv4-global-routing-helper.h"
 
using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("FirstScriptExample");

int
main (int argc, char *argv[])
{
  
  int systemId = -1;
  std::string dir = "";
  
  CommandLine cmd(__FILE__);
  cmd.AddValue("systemId", "System ID", systemId);
  cmd.AddValue("envDir", "Absolute environment dir to store socket and shm files", dir);
  cmd.Parse(argc, argv);

  //Invalid args
	if(systemId == -1 || dir == ""){
		std::cout << "Invalid arguments, please specify a valid system ID and environment directory!" << "\n";
	}

  GlobalValue::Bind("SimulatorImplementationType", StringValue("ns3::CosimSimulatorImpl"));
  dynamic_cast<CosimSimulatorImpl*>(PeekPointer(Simulator::GetImplementation()))->systemId = systemId;
  dynamic_cast<CosimSimulatorImpl*>(PeekPointer(Simulator::GetImplementation()))->dir = dir;

  Time::SetResolution (Time::NS);
  LogComponentEnable ("UdpEchoClientApplication", LOG_LEVEL_INFO);
  LogComponentEnable ("UdpEchoServerApplication", LOG_LEVEL_INFO);
  // LogComponentEnable ("Ipv4GlobalRouting", LOG_LEVEL_ALL);
  // LogComponentEnable ("PointToPointNetDevice", LOG_LEVEL_ALL);
  // LogComponentEnable ("SimpleChannel", LOG_LEVEL_ALL);

  NodeContainer n1;
  n1.Create (1,0);
  n1.Create (1,1);


  PointToPointHelperSimbricks pointToPoint;
  pointToPoint.SetDeviceAttribute ("DataRate", StringValue ("5Mbps"));
  pointToPoint.SetChannelAttribute ("Delay", StringValue ("20ms"));

  NetDeviceContainer d1;
  d1 = pointToPoint.Install (n1);

  InternetStackHelper stack;
  stack.InstallAll ();

  Ipv4AddressHelper address;
  address.SetBase ("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer i1 = address.Assign (d1);

  if(systemId==0){
    UdpEchoServerHelper echoServer (9);

    ApplicationContainer serverApps = echoServer.Install (n1.Get (0));
    serverApps.Start (Seconds (1.0));
    serverApps.Stop (Seconds (10.0));
  }

  if(systemId==1){

    UdpEchoClientHelper echoClient (i1.GetAddress (0), 9);
    echoClient.SetAttribute ("Interval", TimeValue (MilliSeconds (0.1)));
    echoClient.SetAttribute ("PacketSize", UintegerValue (1024));

    ApplicationContainer clientApps = echoClient.Install (n1.Get (1));
    clientApps.Start (Seconds (2.0));
    clientApps.Stop (Seconds (10.0));
  }

  Simulator::Stop (Seconds(10));
  
  Simulator::Run ();

  Simulator::Destroy ();
  return 0;
}
