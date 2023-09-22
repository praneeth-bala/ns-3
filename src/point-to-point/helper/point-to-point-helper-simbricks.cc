/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2008 INRIA
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
 *
 * Author: Mathieu Lacage <mathieu.lacage@sophia.inria.fr>
 */

#include "ns3/abort.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/point-to-point-net-device.h"
#include "ns3/point-to-point-channel.h"
#include "ns3/queue.h"
#include "ns3/net-device-queue-interface.h"
#include "ns3/config.h"
#include "ns3/packet.h"
#include "ns3/names.h"

#ifdef NS3_MPI
#include "ns3/mpi-interface.h"
#include "ns3/mpi-receiver.h"
#include "ns3/point-to-point-remote-channel-simbricks.h"
#include "ns3/simbricks-receiver.h"
#endif

#include "ns3/trace-helper.h"
#include "point-to-point-helper-simbricks.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("PointToPointHelperSimbricks");

PointToPointHelperSimbricks::PointToPointHelperSimbricks (CosimManager &connector, int systemId)
{
  m_queueFactory.SetTypeId ("ns3::DropTailQueue<Packet>");
  m_deviceFactory.SetTypeId ("ns3::PointToPointNetDevice");
  m_channelFactory.SetTypeId ("ns3::PointToPointChannel");
  m_connector = &connector;
  currSystemId = systemId;
#ifdef NS3_MPI
  m_remoteChannelFactory.SetTypeId ("ns3::PointToPointRemoteChannelSimbricks");
#endif
}

void 
PointToPointHelperSimbricks::SetQueue (std::string type,
                              std::string n1, const AttributeValue &v1,
                              std::string n2, const AttributeValue &v2,
                              std::string n3, const AttributeValue &v3,
                              std::string n4, const AttributeValue &v4)
{
  QueueBase::AppendItemTypeIfNotPresent (type, "Packet");

  m_queueFactory.SetTypeId (type);
  m_queueFactory.Set (n1, v1);
  m_queueFactory.Set (n2, v2);
  m_queueFactory.Set (n3, v3);
  m_queueFactory.Set (n4, v4);
}

void 
PointToPointHelperSimbricks::SetDeviceAttribute (std::string n1, const AttributeValue &v1)
{
  m_deviceFactory.Set (n1, v1);
}

void 
PointToPointHelperSimbricks::SetChannelAttribute (std::string n1, const AttributeValue &v1)
{
  m_channelFactory.Set (n1, v1);
#ifdef NS3_MPI
  m_remoteChannelFactory.Set (n1, v1);
#endif
}

void 
PointToPointHelperSimbricks::EnablePcapInternal (std::string prefix, Ptr<NetDevice> nd, bool promiscuous, bool explicitFilename)
{
  //
  // All of the Pcap enable functions vector through here including the ones
  // that are wandering through all of devices on perhaps all of the nodes in
  // the system.  We can only deal with devices of type PointToPointNetDevice.
  //
  Ptr<PointToPointNetDevice> device = nd->GetObject<PointToPointNetDevice> ();
  if (device == 0)
    {
      NS_LOG_INFO ("PointToPointHelperSimbricks::EnablePcapInternal(): Device " << device << " not of type ns3::PointToPointNetDevice");
      return;
    }

  PcapHelper pcapHelper;

  std::string filename;
  if (explicitFilename)
    {
      filename = prefix;
    }
  else
    {
      filename = pcapHelper.GetFilenameFromDevice (prefix, device);
    }

  Ptr<PcapFileWrapper> file = pcapHelper.CreateFile (filename, std::ios::out, 
                                                     PcapHelper::DLT_PPP);
  pcapHelper.HookDefaultSink<PointToPointNetDevice> (device, "PromiscSniffer", file);
}

void 
PointToPointHelperSimbricks::EnableAsciiInternal (
  Ptr<OutputStreamWrapper> stream, 
  std::string prefix, 
  Ptr<NetDevice> nd,
  bool explicitFilename)
{
  //
  // All of the ascii enable functions vector through here including the ones
  // that are wandering through all of devices on perhaps all of the nodes in
  // the system.  We can only deal with devices of type PointToPointNetDevice.
  //
  Ptr<PointToPointNetDevice> device = nd->GetObject<PointToPointNetDevice> ();
  if (device == 0)
    {
      NS_LOG_INFO ("PointToPointHelperSimbricks::EnableAsciiInternal(): Device " << device << 
                   " not of type ns3::PointToPointNetDevice");
      return;
    }

  //
  // Our default trace sinks are going to use packet printing, so we have to 
  // make sure that is turned on.
  //
  Packet::EnablePrinting ();

  //
  // If we are not provided an OutputStreamWrapper, we are expected to create 
  // one using the usual trace filename conventions and do a Hook*WithoutContext
  // since there will be one file per context and therefore the context would
  // be redundant.
  //
  if (stream == 0)
    {
      //
      // Set up an output stream object to deal with private ofstream copy 
      // constructor and lifetime issues.  Let the helper decide the actual
      // name of the file given the prefix.
      //
      AsciiTraceHelper asciiTraceHelper;

      std::string filename;
      if (explicitFilename)
        {
          filename = prefix;
        }
      else
        {
          filename = asciiTraceHelper.GetFilenameFromDevice (prefix, device);
        }

      Ptr<OutputStreamWrapper> theStream = asciiTraceHelper.CreateFileStream (filename);

      //
      // The MacRx trace source provides our "r" event.
      //
      asciiTraceHelper.HookDefaultReceiveSinkWithoutContext<PointToPointNetDevice> (device, "MacRx", theStream);

      //
      // The "+", '-', and 'd' events are driven by trace sources actually in the
      // transmit queue.
      //
      Ptr<Queue<Packet> > queue = device->GetQueue ();
      asciiTraceHelper.HookDefaultEnqueueSinkWithoutContext<Queue<Packet> > (queue, "Enqueue", theStream);
      asciiTraceHelper.HookDefaultDropSinkWithoutContext<Queue<Packet> > (queue, "Drop", theStream);
      asciiTraceHelper.HookDefaultDequeueSinkWithoutContext<Queue<Packet> > (queue, "Dequeue", theStream);

      // PhyRxDrop trace source for "d" event
      asciiTraceHelper.HookDefaultDropSinkWithoutContext<PointToPointNetDevice> (device, "PhyRxDrop", theStream);

      return;
    }

  //
  // If we are provided an OutputStreamWrapper, we are expected to use it, and
  // to providd a context.  We are free to come up with our own context if we
  // want, and use the AsciiTraceHelper Hook*WithContext functions, but for 
  // compatibility and simplicity, we just use Config::Connect and let it deal
  // with the context.
  //
  // Note that we are going to use the default trace sinks provided by the 
  // ascii trace helper.  There is actually no AsciiTraceHelper in sight here,
  // but the default trace sinks are actually publicly available static 
  // functions that are always there waiting for just such a case.
  //
  uint32_t nodeid = nd->GetNode ()->GetId ();
  uint32_t deviceid = nd->GetIfIndex ();
  std::ostringstream oss;

  oss << "/NodeList/" << nd->GetNode ()->GetId () << "/DeviceList/" << deviceid << "/$ns3::PointToPointNetDevice/MacRx";
  Config::Connect (oss.str (), MakeBoundCallback (&AsciiTraceHelper::DefaultReceiveSinkWithContext, stream));

  oss.str ("");
  oss << "/NodeList/" << nodeid << "/DeviceList/" << deviceid << "/$ns3::PointToPointNetDevice/TxQueue/Enqueue";
  Config::Connect (oss.str (), MakeBoundCallback (&AsciiTraceHelper::DefaultEnqueueSinkWithContext, stream));

  oss.str ("");
  oss << "/NodeList/" << nodeid << "/DeviceList/" << deviceid << "/$ns3::PointToPointNetDevice/TxQueue/Dequeue";
  Config::Connect (oss.str (), MakeBoundCallback (&AsciiTraceHelper::DefaultDequeueSinkWithContext, stream));

  oss.str ("");
  oss << "/NodeList/" << nodeid << "/DeviceList/" << deviceid << "/$ns3::PointToPointNetDevice/TxQueue/Drop";
  Config::Connect (oss.str (), MakeBoundCallback (&AsciiTraceHelper::DefaultDropSinkWithContext, stream));

  oss.str ("");
  oss << "/NodeList/" << nodeid << "/DeviceList/" << deviceid << "/$ns3::PointToPointNetDevice/PhyRxDrop";
  Config::Connect (oss.str (), MakeBoundCallback (&AsciiTraceHelper::DefaultDropSinkWithContext, stream));
}

NetDeviceContainer 
PointToPointHelperSimbricks::Install (NodeContainer c)
{
  NS_ASSERT (c.GetN () == 2);
  return Install (c.Get (0), c.Get (1));
}

NetDeviceContainer 
PointToPointHelperSimbricks::Install (Ptr<Node> a, Ptr<Node> b)
{
  NetDeviceContainer container;

  Ptr<PointToPointNetDevice> devA = m_deviceFactory.Create<PointToPointNetDevice> ();
  devA->SetAddress (Mac48Address::Allocate ());
  a->AddDevice (devA);
  Ptr<Queue<Packet> > queueA = m_queueFactory.Create<Queue<Packet> > ();
  devA->SetQueue (queueA);
  Ptr<PointToPointNetDevice> devB = m_deviceFactory.Create<PointToPointNetDevice> ();
  devB->SetAddress (Mac48Address::Allocate ());
  b->AddDevice (devB);
  Ptr<Queue<Packet> > queueB = m_queueFactory.Create<Queue<Packet> > ();
  devB->SetQueue (queueB);
  // Aggregate NetDeviceQueueInterface objects
  Ptr<NetDeviceQueueInterface> ndqiA = CreateObject<NetDeviceQueueInterface> ();
  ndqiA->GetTxQueue (0)->ConnectQueueTraces (queueA);
  devA->AggregateObject (ndqiA);
  Ptr<NetDeviceQueueInterface> ndqiB = CreateObject<NetDeviceQueueInterface> ();
  ndqiB->GetTxQueue (0)->ConnectQueueTraces (queueB);
  devB->AggregateObject (ndqiB);

  Ptr<PointToPointChannel> channel = 0;
  Ptr<PointToPointRemoteChannelSimbricks> chan_ptr = 0;

  // If MPI is enabled, we need to see if both nodes have the same system id 
  // (rank), and the rank is the same as this instance.  If both are true, 
  // use a normal p2p channel, otherwise use a remote channel
  bool useNormalChannel = true;
  uint32_t n1SystemId = a->GetSystemId ();
  uint32_t n2SystemId = b->GetSystemId ();
  if (n1SystemId != currSystemId || n2SystemId != currSystemId) 
    {
      useNormalChannel = false;
      channel = m_remoteChannelFactory.Create<PointToPointRemoteChannelSimbricks> ();
      chan_ptr = dynamic_cast<PointToPointRemoteChannelSimbricks*> (PeekPointer(channel));
      conns[n2SystemId][n1SystemId] = chan_ptr->getDelay().ToInteger (Time::PS);
      conns[n1SystemId][n2SystemId] = chan_ptr->getDelay().ToInteger (Time::PS);
    }
  if (useNormalChannel)
    {
      channel = m_channelFactory.Create<PointToPointChannel> ();
    }
  else
    {
      chan_ptr->m_connector = m_connector;
      Ptr<SimbricksReceiver> mpiRecA = CreateObject<SimbricksReceiver> ();
      Ptr<SimbricksReceiver> mpiRecB = CreateObject<SimbricksReceiver> ();
      mpiRecA->SetReceiveCallback (MakeCallback (&PointToPointNetDevice::Receive, devA));
      mpiRecB->SetReceiveCallback (MakeCallback (&PointToPointNetDevice::Receive, devB));
      devA->AggregateObject (mpiRecA);
      devB->AggregateObject (mpiRecB);
    }

  devA->Attach (channel);
  devB->Attach (channel);
  container.Add (devA);
  container.Add (devB);

  return container;
}

NetDeviceContainer 
PointToPointHelperSimbricks::Install (Ptr<Node> a, std::string bName)
{
  Ptr<Node> b = Names::Find<Node> (bName);
  return Install (a, b);
}

NetDeviceContainer 
PointToPointHelperSimbricks::Install (std::string aName, Ptr<Node> b)
{
  Ptr<Node> a = Names::Find<Node> (aName);
  return Install (a, b);
}

NetDeviceContainer 
PointToPointHelperSimbricks::Install (std::string aName, std::string bName)
{
  Ptr<Node> a = Names::Find<Node> (aName);
  Ptr<Node> b = Names::Find<Node> (bName);
  return Install (a, b);
}

} // namespace ns3
