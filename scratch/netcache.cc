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
#include <random>
#include <signal.h>
#include "ns3/trace-helper.h"
#include "ns3/abort.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-apps-module.h"
#include "ns3/ipv4-static-routing-helper.h"
#include "ns3/ipv4-list-routing-helper.h"
#include "ns3/net-cache-device.h"
#include "ns3/http-header.h"
#include "ns3/cosim.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/applications-module.h"

using namespace ns3;
using std::string;

NS_LOG_COMPONENT_DEFINE ("CosimBridgeExample");

std::vector<std::string> cosimPortPaths;

void INThandler(int sig)
{
    signal(sig, SIG_IGN);
    std::cerr << Simulator::Now().GetMicroSeconds() << "\n";
    std::flush(std::cerr);
    signal(SIGUSR1, INThandler);
}

bool AddCosimPort (std::string arg)
{
    cosimPortPaths.push_back (arg);
    return true;
}

bool pegasus = false;

void setupIp(Ptr<Node> node, Ptr<NetDevice> dev, Ipv4Address add){
    Ptr<Ipv4> ipv4 = node->GetObject<Ipv4> ();
    uint32_t interface = ipv4->AddInterface (dev);
    Ipv4InterfaceAddress address = Ipv4InterfaceAddress (add, Ipv4Mask("255.255.255.0"));
    ipv4->AddAddress (interface, address);
    ipv4->SetMetric (interface, 1);
    ipv4->SetUp (interface);
}

int main (int argc, char *argv[]){

    Time::SetResolution (Time::Unit::PS);
    signal(SIGUSR1, INThandler);

    CommandLine cmd (__FILE__);
    cmd.AddValue ("CosimPort", "Add a cosim ethernet port to the bridge",
        MakeCallback (&AddCosimPort));
    cmd.Parse (argc, argv);

    //LogComponentEnable("CosimNetDevice", LOG_LEVEL_ALL);

    GlobalValue::Bind ("ChecksumEnabled", BooleanValue (true));
    std::ifstream in;
    std::vector<std::string> keys;
    in.open("/workspaces/simbricks/experiments/pegasus/artifact_eval/ns3keys");
    std::string key;
    for (int i = 0; i < 128; i++) {
        getline(in, key);
        keys.push_back(key);
    }
    in.close();

    Time linkLatency(MicroSeconds (2));
    DataRate linkRate("100Gb/s");
    double ecnTh = 200000;

    SimpleNetDeviceHelper pointToPointSR;
    pointToPointSR.SetQueue("ns3::DevRedQueue", "MaxSize", StringValue("2666p"));
    pointToPointSR.SetQueue("ns3::DevRedQueue", "MinTh", DoubleValue (ecnTh));
    pointToPointSR.SetDeviceAttribute ("DataRate", DataRateValue(linkRate));
    pointToPointSR.SetChannelAttribute ("Delay", TimeValue (linkLatency));

    NS_LOG_INFO ("Create BridgeDevice");
    Ptr<NetCacheDevice> bridge = CreateObject<NetCacheDevice> (keys);
    // Ptr<BridgeNetDevice> bridge = CreateObject<BridgeNetDevice> ();
    bridge->SetAddress (Mac48Address::Allocate ());

    NS_LOG_INFO ("Create Node");
    int num_clients = 1;
    int num_servers = 2;
    int num_total = num_clients + num_servers;

    Ptr<Node> bridge_node = CreateObject<Node> ();
    bridge_node->AddDevice (bridge);

    NodeContainer pairs[num_total];
    NetDeviceContainer netpairs[num_total];
    NodeContainer nodes(bridge_node);
    
    for(int i=0;i<num_total;i++){
        Ptr<Node> n = CreateObject<Node> ();
        pairs[i] = NodeContainer(bridge_node);
        pairs[i].Add(n);
        nodes.Add(n);
        netpairs[i] = pointToPointSR.Install (pairs[i]);
        bridge->AddBridgePort(netpairs[i].Get(0));
    }

    NS_LOG_INFO ("Add Internet Stack");
    InternetStackHelper internetStackHelper;
    internetStackHelper.Install (nodes);

    Ipv4Address bridgeIp ("10.0.0.2");
    std::string start_add = "10.0.0.";
    std::vector<InetSocketAddress> servers, clients;
    setupIp(bridge_node, bridge, bridgeIp);

    int count_add = 3;
    int count_port = 12345;
    for(int i=0;i<num_servers;i++){
        Ipv4Address add = Ipv4Address((start_add + std::to_string(count_add)).c_str());
        setupIp(pairs[i].Get(1), netpairs[i].Get(1), add);
        servers.push_back(InetSocketAddress (add, count_port));
        count_add++;
        count_port++;  
    }

    for(int i=num_servers;i<num_total;i++){
        Ipv4Address add = Ipv4Address((start_add + std::to_string(count_add)).c_str());
        setupIp(pairs[i].Get(1), netpairs[i].Get(1), add);
        clients.push_back(InetSocketAddress (add, count_port));
        count_add++;
        count_port++;
    }
    
    for(int i=0;i<num_servers;i++){
        setupServer(pegasus, pairs[i].Get(1), servers[i], i, num_servers, 1, 10, clients);
    }

    for(int i=num_servers;i<num_total;i++){
        setupClient(pegasus, 10, 0.3, pairs[i].Get(1), clients[i-num_servers], i-num_servers, num_servers, 2, 3, servers);
    }

    NS_LOG_INFO ("Create CosimDevices and add them to bridge");
    std::vector <Ptr<CosimNetDevice>> cosimDevs;
    for (std::string cpp : cosimPortPaths) {
        Ptr<CosimNetDevice> device = CreateObject<CosimNetDevice> ();
        device->SetAttribute ("UnixSocket", StringValue (cpp));
        bridge_node->AddDevice (device);
        bridge->AddBridgePort (device);
        device->Start ();
    }

    NS_LOG_INFO ("Run Emulation.");
    Simulator::Run ();
    Simulator::Destroy ();
    NS_LOG_INFO ("Done.");
}
