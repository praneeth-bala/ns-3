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
#include "ns3/mpi-interface.h"
#include "ns3/ipv4-global-routing-helper.h"
#include <iomanip>
#include <mpi.h>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("FirstScriptExample");

int
main (int argc, char *argv[])
{
  bool nix = true;
  bool nullmsg = false;
  bool tracing = false;
  bool testing = false;
  bool verbose = false;

  // Parse command line
  CommandLine cmd(__FILE__);
  cmd.AddValue("nix", "Enable the use of nix-vector or global routing", nix);
  cmd.AddValue("nullmsg", "Enable the use of null-message synchronization", nullmsg);
  cmd.AddValue("tracing", "Enable pcap tracing", tracing);
  cmd.AddValue("verbose", "verbose output", verbose);
  cmd.AddValue("test", "Enable regression test output", testing);
  cmd.Parse(argc, argv);

  // Distributed simulation setup; by default use granted time window algorithm.
  if (nullmsg)
  {
      GlobalValue::Bind("SimulatorImplementationType",
                        StringValue("ns3::NullMessageSimulatorImpl"));
  }
  else
  {
      GlobalValue::Bind("SimulatorImplementationType",
                        StringValue("ns3::DistributedSimulatorImpl"));
  }

  // Enable parallel simulator with the command line arguments
  MpiInterface::Enable(&argc, &argv);

  uint32_t systemId = MpiInterface::GetSystemId();
  uint32_t systemCount = MpiInterface::GetSize();
  
  Time::SetResolution (Time::NS);
  // LogComponentEnable ("UdpEchoClientApplication", LOG_LEVEL_INFO);
  // LogComponentEnable ("UdpEchoServerApplication", LOG_LEVEL_INFO);
  
  // LogComponentEnable ("SimpleNetDevice", LOG_LEVEL_ALL);
  // LogComponentEnable ("SimpleChannel", LOG_LEVEL_ALL);

  NodeContainer n1,n2,n3;
  n1.Create (1,0);
  n1.Create (1,1);
  n2.Add (n1.Get(1));
  n2.Create(1,2);
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

  if(systemId==0){
    LogComponentEnable ("UdpEchoClientApplication", LOG_LEVEL_INFO);
    LogComponentEnable ("UdpEchoServerApplication", LOG_LEVEL_INFO);
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

  if(systemId==1){
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

  if(systemId==2){
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

	MPI_Barrier(MPI_COMM_WORLD); /* IMPORTANT */
	double start = MPI_Wtime();

	Simulator::Run ();

	MPI_Barrier(MPI_COMM_WORLD); /* IMPORTANT */
	double end = MPI_Wtime();
	if (systemId == 0) { /* use time on master node */
		printf("Runtime = %f\n", end-start);
	}
  Simulator::Destroy ();
  MpiInterface::Disable();
  return 0;
}
