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
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include <chrono>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("FirstScriptExample");

int
main (int argc, char *argv[])
{

  // Parse command line
  CommandLine cmd(__FILE__);
  cmd.Parse(argc, argv);
  
  Time::SetResolution (Time::NS);
  // LogComponentEnable ("UdpEchoClientApplication", LOG_LEVEL_INFO);
  // LogComponentEnable ("UdpEchoServerApplication", LOG_LEVEL_INFO);
  
  // LogComponentEnable ("SimpleNetDevice", LOG_LEVEL_ALL);
  // LogComponentEnable ("SimpleChannel", LOG_LEVEL_ALL);

  NodeContainer n1,n2,n3;
  n1.Create (1);
  n1.Create (1);
  n2.Add (n1.Get(1));
  n2.Create(1);
  n3.Add(n2.Get(1));
  n3.Add(n1.Get(0));


  PointToPointHelper pointToPoint;
  pointToPoint.SetDeviceAttribute ("DataRate", StringValue ("5Mbps"));
  pointToPoint.SetChannelAttribute ("Delay", StringValue ("2ms"));

  NetDeviceContainer d1,d2,d3;
  d1 = pointToPoint.Install (n1);
  d2 = pointToPoint.Install (n2);
  d3 = pointToPoint.Install (n3);

  InternetStackHelper stack;
  stack.InstallAll ();

  Ipv4AddressHelper address;
  address.SetBase ("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer i1 = address.Assign (d1);
  address.SetBase ("10.2.1.0", "255.255.255.0");
  Ipv4InterfaceContainer i2 = address.Assign (d2);
  address.SetBase ("10.3.1.0", "255.255.255.0");
  Ipv4InterfaceContainer i3 = address.Assign (d3);

  if(1){
    UdpEchoServerHelper echoServer (9);

    ApplicationContainer serverApps = echoServer.Install (n1.Get (0));
    serverApps.Start (Seconds (1.0));
    serverApps.Stop (Seconds (10.0));

    UdpEchoClientHelper echoClient (i1.GetAddress (1), 9);
    echoClient.SetAttribute ("MaxPackets", UintegerValue (1));
    echoClient.SetAttribute ("Interval", TimeValue (Seconds (1.0)));
    echoClient.SetAttribute ("PacketSize", UintegerValue (1024));

    ApplicationContainer clientApps = echoClient.Install (n1.Get (0));
    clientApps.Start (Seconds (2.0));
    clientApps.Stop (Seconds (10.0));
  }

  if(1){
    UdpEchoServerHelper echoServer (9);

    ApplicationContainer serverApps = echoServer.Install (n1.Get (1));
    serverApps.Start (Seconds (1.0));
    serverApps.Stop (Seconds (10.0));

    UdpEchoClientHelper echoClient (i2.GetAddress (1), 9);
    echoClient.SetAttribute ("MaxPackets", UintegerValue (1));
    echoClient.SetAttribute ("Interval", TimeValue (Seconds (1.0)));
    echoClient.SetAttribute ("PacketSize", UintegerValue (1024));

    ApplicationContainer clientApps = echoClient.Install (n1.Get (1));
    clientApps.Start (Seconds (2.0));
    clientApps.Stop (Seconds (10.0));
  }

  if(1){
    UdpEchoServerHelper echoServer (9);

    ApplicationContainer serverApps = echoServer.Install (n2.Get (1));
    serverApps.Start (Seconds (1.0));
    serverApps.Stop (Seconds (10.0));

    UdpEchoClientHelper echoClient (i3.GetAddress (1), 9);
    echoClient.SetAttribute ("MaxPackets", UintegerValue (1));
    echoClient.SetAttribute ("Interval", TimeValue (Seconds (1.0)));
    echoClient.SetAttribute ("PacketSize", UintegerValue (1024));

    ApplicationContainer clientApps = echoClient.Install (n2.Get (1));
    clientApps.Start (Seconds (2.0));
    clientApps.Stop (Seconds (10.0));

    // LogComponentEnable ("Ipv4GlobalRouting", LOG_LEVEL_ALL);
  }

  Simulator::Stop (Seconds(10));
  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

  using std::chrono::high_resolution_clock;
  using std::chrono::duration_cast;
  using std::chrono::duration;
  using std::chrono::milliseconds;
	auto t1 = high_resolution_clock::now();

  Simulator::Run ();

  auto t2 = high_resolution_clock::now();
  duration<double, std::milli> ms_double = t2 - t1;
  std::cout << "Runtime = " << ms_double.count()/1000 << std::endl;

  Simulator::Destroy ();
  return 0;
}
