/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2005,2006 INRIA
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

#include "ns3/simulator.h"
#include "cosim-simulator-impl.h"
#include "ns3/scheduler.h"
#include "ns3/event-impl.h"

#include "ns3/ptr.h"
#include "ns3/pointer.h"
#include "ns3/uinteger.h"
#include "ns3/assert.h"
#include "ns3/log.h"

#include <cmath>


/**
 * \file
 * \ingroup simulator
 * ns3::CosimSimulatorImpl implementation.
 */

namespace ns3 {

// Note:  Logging in this file is largely avoided due to the
// number of calls that are made to these functions and the possibility
// of causing recursions leading to stack overflow
NS_LOG_COMPONENT_DEFINE ("CosimSimulatorImpl");

NS_OBJECT_ENSURE_REGISTERED (CosimSimulatorImpl);

TypeId
CosimSimulatorImpl::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::CosimSimulatorImpl")
    .SetParent<SimulatorImpl> ()
    .SetGroupName ("Core")
    .AddConstructor<CosimSimulatorImpl> ()
  ;
  return tid;
}

CosimSimulatorImpl::CosimSimulatorImpl ()
{
  NS_LOG_FUNCTION (this);
  m_stop = false;
  // uids are allocated from 4.
  // uid 0 is "invalid" events
  // uid 1 is "now" events
  // uid 2 is "destroy" events
  m_uid = 4;
  // before ::Run is entered, the m_currentUid will be zero
  m_currentUid = 0;
  m_currentTs = 0;
  m_currentContext = Simulator::NO_CONTEXT;
  m_unscheduledEvents = 0;
  m_eventCount = 0;
  m_eventsWithContextEmpty = true;
  m_syncMode = true;
  m_main = SystemThread::Self ();
}

CosimSimulatorImpl::~CosimSimulatorImpl ()
{
  NS_LOG_FUNCTION (this);
  for(auto i = m_nsif.begin(); i != m_nsif.end(); i++) delete i->second;
  for(auto i = m_bifparam.begin(); i != m_bifparam.end(); i++) delete i->second;
}

bool CosimSimulatorImpl::Transmit (Ptr<Packet> packet, const Time& rxTime, uint32_t node, uint32_t dev)
{
  volatile union SimbricksProtoNetMsg *msg;
  volatile struct SimbricksProtoNetMsgPacket *recv;

  /*NS_ABORT_MSG_IF (packet->GetSize () > 2048,
          "CosimSimulatorImpl::Transmit: packet too large");*/

  Ptr<Node> nd = NodeList::GetNode(node);
  int systemId = nd->GetSystemId();
  msg = AllocTx (systemId);
  recv = &msg->packet;

  uint64_t t = rxTime.GetInteger ();
  uint64_t* pTime = (uint64_t *)(recv->data);
  *pTime++ = t;
  uint32_t* pData = reinterpret_cast<uint32_t *> (pTime);
  *pData++ = node;
  *pData++ = dev;

  recv->len = packet->GetSize ()+8+4+4;
  recv->port = 0;
  packet->CopyData (reinterpret_cast<uint8_t *> (pData), packet->GetSize ());

  SimbricksNetIfOutSend(m_nsif[systemId], msg, SIMBRICKS_PROTO_NET_MSG_PACKET);

  // if (m_syncMode) {
  //   Simulator::Cancel (m_syncTxEvent[systemId]);
  //   m_syncTxEvent[systemId] = Simulator::Schedule ( PicoSeconds (m_bifparam[systemId]->sync_interval),
  //           &CosimSimulatorImpl::SendSyncEvent, this, systemId);
  // }

  return true;
}

void CosimSimulatorImpl::ReceivedPacket (const void *buf, size_t len, uint64_t time)
{ 
    uint64_t* pTime = (uint64_t *)(buf);
    uint64_t tim = *pTime++;
    uint32_t* pData = reinterpret_cast<uint32_t *> (pTime);
    uint32_t node = *pData++;
    uint32_t dev  = *pData++;

    Time rxTime (tim);

    int count = sizeof (time) + sizeof (node) + sizeof (dev);
    Ptr<Packet> packet = Create<Packet> (reinterpret_cast<uint8_t *> (pData), len - 16);
    // Find the correct node/device to schedule receive event
    Ptr<Node> pNode = NodeList::GetNode (node);
    Ptr<SimbricksReceiver> pMpiRec = 0;
    uint32_t nDevices = pNode->GetNDevices ();
    for (uint32_t i = 0; i < nDevices; ++i)
      {
        Ptr<NetDevice> pThisDev = pNode->GetDevice (i);
        if (pThisDev->GetIfIndex () == dev)
          {
            pMpiRec = pThisDev->GetObject<SimbricksReceiver> ();
            break;
          }
      }

    NS_ASSERT (pNode && pMpiRec);

    // Schedule the rx event
    Simulator::ScheduleWithContext (pNode->GetId (), rxTime - Simulator::Now (),
                                    &SimbricksReceiver::Receive, pMpiRec, packet);
}


volatile union SimbricksProtoNetMsg* CosimSimulatorImpl::AllocTx (int systemId)
{
  volatile union SimbricksProtoNetMsg *msg;
  do {
    msg = SimbricksNetIfOutAlloc (m_nsif[systemId], Simulator::Now ().ToInteger (Time::PS));
  } while (!msg);

  //TODO: fix it to wait until alloc success
  NS_ABORT_MSG_IF (msg == NULL,
          "SimbricksAdapter::AllocTx: SimbricksNetIfOutAlloc failed");
  return msg;
}

void CosimSimulatorImpl::SendSyncEvent (int systemId)
{
  volatile union SimbricksProtoNetMsg *msg = AllocTx (systemId);
  NS_ABORT_MSG_IF (msg == NULL,
          "SimbricksAdapter::AllocTx: SimbricksNetIfOutAlloc failed");

  // msg->sync.own_type = SIMBRICKS_PROTO_NET_N2D_MSG_SYNC |
  //     SIMBRICKS_PROTO_NET_N2D_OWN_DEV;
  SimbricksBaseIfOutSend(&m_nsif[systemId]->base, &msg->base, SIMBRICKS_PROTO_MSG_TYPE_SYNC);
  // std::cout << Simulator:: Now() << "\n";

  m_syncTxEvent[systemId] = Simulator::Schedule ( PicoSeconds (m_bifparam[systemId]->sync_interval), &CosimSimulatorImpl::SendSyncEvent, this, systemId);
}

bool CosimSimulatorImpl::Poll (int systemId)
{
  volatile union SimbricksProtoNetMsg *msg;
  uint8_t ty;

  msg = SimbricksNetIfInPoll (m_nsif[systemId], Simulator::GetMaximumSimulationTime ().ToInteger (Time::PS));
  m_nextTime[systemId] = PicoSeconds (SimbricksNetIfInTimestamp (m_nsif[systemId]));
  
  if (!msg)
    return false;

  ty = SimbricksNetIfInType(m_nsif[systemId], msg);
  switch (ty) {
    case SIMBRICKS_PROTO_NET_MSG_PACKET:
      ReceivedPacket ((const void *) msg->packet.data, msg->packet.len, msg->base.header.timestamp);
      break;

    case SIMBRICKS_PROTO_MSG_TYPE_SYNC:
      break;

    default:
      NS_ABORT_MSG ("CosimSimulatorImpl::Poll: unsupported message type " << ty);
  }

  SimbricksNetIfInDone (m_nsif[systemId], msg);
  return true;
}


void CosimSimulatorImpl::PollEvent (int systemId)
{
  // std::cout << Simulator::Now () << "\n";
  while (Poll (systemId));

  if (m_syncMode){
    while (m_nextTime[systemId] <= Simulator::Now ()){
      Poll (systemId);}

    m_pollEvent[systemId] = Simulator::Schedule (m_nextTime[systemId] -  Simulator::Now (),
            &CosimSimulatorImpl::PollEvent, this, systemId);
  } else {
    m_pollEvent[systemId] = Simulator::Schedule (m_pollDelay[systemId],
            &CosimSimulatorImpl::PollEvent, this, systemId);

  }
}


void
CosimSimulatorImpl::DoDispose (void)
{
  NS_LOG_FUNCTION (this);
  ProcessEventsWithContext ();

  while (!m_events->IsEmpty ())
    {
      Scheduler::Event next = m_events->RemoveNext ();
      next.impl->Unref ();
    }
  m_events = 0;
  SimulatorImpl::DoDispose ();
}
void
CosimSimulatorImpl::Destroy ()
{
  NS_LOG_FUNCTION (this);
  while (!m_destroyEvents.empty ())
    {
      Ptr<EventImpl> ev = m_destroyEvents.front ().PeekEventImpl ();
      m_destroyEvents.pop_front ();
      NS_LOG_LOGIC ("handle destroy " << ev);
      if (!ev->IsCancelled ())
        {
          ev->Invoke ();
        }
    }

  for(auto i = m_nsif.begin(); i != m_nsif.end(); i++){
    if (m_syncMode)
      Simulator::Cancel (m_syncTxEvent[i->first]);
    Simulator::Cancel (m_pollEvent[i->first]);
  }
}

void CosimSimulatorImpl::SetupInterconnections (){
  NodeContainer c = NodeContainer::GetGlobal ();
  int systemId = GetSystemId();
  for (NodeContainer::Iterator iter = c.Begin (); iter != c.End (); ++iter)
    {
      if ((*iter)->GetSystemId () != systemId)
        {
          continue;
        }

      for (uint32_t i = 0; i < (*iter)->GetNDevices (); ++i)
        {
          Ptr<NetDevice> localNetDevice = (*iter)->GetDevice (i);
          // only works for p2p links currently
          if (!localNetDevice->IsPointToPoint ())
            {
              continue;
            }
          Ptr<Channel> channel = localNetDevice->GetChannel ();
          if (channel == 0)
            {
              continue;
            }

          // grab the adjacent node
          Ptr<Node> remoteNode;
          if (channel->GetDevice (0) == localNetDevice)
            {
              remoteNode = (channel->GetDevice (1))->GetNode ();
            }
          else
            {
              remoteNode = (channel->GetDevice (0))->GetNode ();
            }

          // if it's not remote, don't consider it
          if (remoteNode->GetSystemId () == systemId)
            {
              continue;
            }

          TimeValue delay;
          channel->GetAttribute ("Delay", delay);
          conns[systemId][remoteNode->GetSystemId ()] = delay.Get().ToInteger (Time::PS);
          conns[remoteNode->GetSystemId ()][systemId] = delay.Get().ToInteger (Time::PS);
        }
    }

  int num_conns = conns[systemId].size();

  for(auto i = conns[systemId].begin(); i != conns[systemId].end(); i++){
    m_bifparam[i->first] = new SimbricksBaseIfParams();
    SimbricksNetIfDefaultParams(m_bifparam[i->first]);

    m_bifparam[i->first]->sync_mode = (enum SimbricksBaseIfSyncMode)1;
    m_bifparam[i->first]->sync_interval = i->second;
    m_pollDelay[systemId] = Time(PicoSeconds (m_bifparam[i->first]->sync_interval));
    m_bifparam[i->first]->link_latency = i->second;

    int a,b;
    if(systemId>i->first){
      a=i->first;
      b=systemId;
    }
    else{
      a=systemId;
      b=i->first;
    }
    
    std::string shm_path = dir+"sim_shm"+std::to_string(a)+"_"+std::to_string(b), sock_path=dir+"sim_socket"+std::to_string(a)+"_"+std::to_string(b);
    m_bifparam[i->first]->sock_path = sock_path.c_str();

    int ret;
    int sync = m_bifparam[i->first]->sync_mode;
    m_nsif[i->first] = new SimbricksNetIf();
    int res = access(shm_path.c_str(), R_OK);
    if (res < 0) {
        if (errno == ENOENT) {
          // File not exist
          struct SimbricksBaseIfSHMPool pool;
          struct SimbricksBaseIf *netif = &m_nsif[i->first]->base;

          // first allocate pool
          size_t shm_size = 3200*8192;
          if (SimbricksBaseIfSHMPoolCreate(&pool, shm_path.c_str(), shm_size)) {
            perror("SimbricksNicIfInit: SimbricksBaseIfSHMPoolCreate failed");
            return;
          }

          struct SimBricksBaseIfEstablishData ests[1];
          struct SimbricksProtoNetIntro net_intro;
          unsigned n_bifs = 0;

          if (SimbricksBaseIfInit(netif, m_bifparam[i->first])) {
            perror("SimbricksNicIfInit: SimbricksBaseIfInit net failed");
            return;
          }

          if (SimbricksBaseIfListen(netif, &pool)) {
            perror("SimbricksNicIfInit: SimbricksBaseIfListen net failed");
            return;
          }

          memset(&net_intro, 0, sizeof(net_intro));
          ests[0].base_if = netif;
          ests[0].tx_intro = &net_intro;
          ests[0].tx_intro_len = sizeof(net_intro);
          ests[0].rx_intro = &net_intro;
          ests[0].rx_intro_len = sizeof(net_intro);
          n_bifs++;
          
          SimBricksBaseIfEstablish(ests, n_bifs);
        }
    }
    else{
      ret = SimbricksNetIfInit(m_nsif[i->first], m_bifparam[i->first], m_bifparam[i->first]->sock_path, &sync);
    }

    NS_ABORT_MSG_IF (m_bifparam[i->first]->sock_path == NULL, "SimbricksAdapter::Connect: unix socket"
            " path empty");

    NS_ABORT_MSG_IF (m_bifparam[i->first]->sync_mode && !sync,
            "SimbricksAdapter::Connect: request for sync failed");

    if (m_syncMode)
      m_syncTxEvent[i->first] = Simulator::ScheduleNow (&CosimSimulatorImpl::SendSyncEvent, this, i->first);

    m_pollEvent[i->first] = Simulator::ScheduleNow (&CosimSimulatorImpl::PollEvent, this, i->first);
  }
}

void
CosimSimulatorImpl::SetScheduler (ObjectFactory schedulerFactory)
{
  NS_LOG_FUNCTION (this << schedulerFactory);
  Ptr<Scheduler> scheduler = schedulerFactory.Create<Scheduler> ();

  if (m_events != 0)
    {
      while (!m_events->IsEmpty ())
        {
          Scheduler::Event next = m_events->RemoveNext ();
          scheduler->Insert (next);
        }
    }
  m_events = scheduler;
}

// System ID for non-distributed simulation is always zero
uint32_t
CosimSimulatorImpl::GetSystemId (void) const
{
  return systemId;
}

void
CosimSimulatorImpl::ProcessOneEvent (void)
{
  Scheduler::Event next = m_events->RemoveNext ();

  NS_ASSERT (next.key.m_ts >= m_currentTs);
  m_unscheduledEvents--;
  m_eventCount++;

  NS_LOG_LOGIC ("handle " << next.key.m_ts);
  m_currentTs = next.key.m_ts;
  m_currentContext = next.key.m_context;
  m_currentUid = next.key.m_uid;
  next.impl->Invoke ();
  next.impl->Unref ();

  ProcessEventsWithContext ();
}

bool
CosimSimulatorImpl::IsFinished (void) const
{
  return m_events->IsEmpty () || m_stop;
}

void
CosimSimulatorImpl::ProcessEventsWithContext (void)
{
  if (m_eventsWithContextEmpty)
    {
      return;
    }

  // swap queues
  EventsWithContext eventsWithContext;
  {
    CriticalSection cs (m_eventsWithContextMutex);
    m_eventsWithContext.swap (eventsWithContext);
    m_eventsWithContextEmpty = true;
  }
  while (!eventsWithContext.empty ())
    {
      EventWithContext event = eventsWithContext.front ();
      eventsWithContext.pop_front ();
      Scheduler::Event ev;
      ev.impl = event.event;
      ev.key.m_ts = m_currentTs + event.timestamp;
      ev.key.m_context = event.context;
      ev.key.m_uid = m_uid;
      m_uid++;
      m_unscheduledEvents++;
      m_events->Insert (ev);
    }
}

void
CosimSimulatorImpl::Run (void)
{
  NS_LOG_FUNCTION (this);
  // Set the current threadId as the main threadId
  SetupInterconnections ();

  using std::chrono::high_resolution_clock;
  using std::chrono::duration_cast;
  using std::chrono::duration;
  using std::chrono::milliseconds;
	auto t1 = high_resolution_clock::now();

  m_main = SystemThread::Self ();
  ProcessEventsWithContext ();
  m_stop = false;

  while (!m_events->IsEmpty () && !m_stop)
    {
      ProcessOneEvent ();
    }

  // If the simulator stopped naturally by lack of events, make a
  // consistency test to check that we didn't lose any events along the way.
  NS_ASSERT (!m_events->IsEmpty () || m_unscheduledEvents == 0);

  auto t2 = high_resolution_clock::now();
  duration<double, std::milli> ms_double = t2 - t1;
  std::cout << "Runtime for SystemID:" << GetSystemId() << " = " << ms_double.count()/1000 << std::endl;
}

void
CosimSimulatorImpl::Stop (void)
{
  NS_LOG_FUNCTION (this);
  m_stop = true;
}

void
CosimSimulatorImpl::Stop (Time const &delay)
{
  NS_LOG_FUNCTION (this << delay.GetTimeStep ());
  Simulator::Schedule (delay, &Simulator::Stop);
}

//
// Schedule an event for a _relative_ time in the future.
//
EventId
CosimSimulatorImpl::Schedule (Time const &delay, EventImpl *event)
{
  NS_LOG_FUNCTION (this << delay.GetTimeStep () << event);
  NS_ASSERT_MSG (SystemThread::Equals (m_main), "Simulator::Schedule Thread-unsafe invocation!");

  NS_ASSERT_MSG (delay.IsPositive (), "CosimSimulatorImpl::Schedule(): Negative delay");
  Time tAbsolute = delay + TimeStep (m_currentTs);

  Scheduler::Event ev;
  ev.impl = event;
  ev.key.m_ts = (uint64_t) tAbsolute.GetTimeStep ();
  ev.key.m_context = GetContext ();
  ev.key.m_uid = m_uid;
  m_uid++;
  m_unscheduledEvents++;
  m_events->Insert (ev);
  return EventId (event, ev.key.m_ts, ev.key.m_context, ev.key.m_uid);
}

void
CosimSimulatorImpl::ScheduleWithContext (uint32_t context, Time const &delay, EventImpl *event)
{
  NS_LOG_FUNCTION (this << context << delay.GetTimeStep () << event);

  if (SystemThread::Equals (m_main))
    {
      Time tAbsolute = delay + TimeStep (m_currentTs);
      Scheduler::Event ev;
      ev.impl = event;
      ev.key.m_ts = (uint64_t) tAbsolute.GetTimeStep ();
      ev.key.m_context = context;
      ev.key.m_uid = m_uid;
      m_uid++;
      m_unscheduledEvents++;
      m_events->Insert (ev);
    }
  else
    {
      EventWithContext ev;
      ev.context = context;
      // Current time added in ProcessEventsWithContext()
      ev.timestamp = delay.GetTimeStep ();
      ev.event = event;
      {
        CriticalSection cs (m_eventsWithContextMutex);
        m_eventsWithContext.push_back (ev);
        m_eventsWithContextEmpty = false;
      }
    }
}

EventId
CosimSimulatorImpl::ScheduleNow (EventImpl *event)
{
  NS_ASSERT_MSG (SystemThread::Equals (m_main), "Simulator::ScheduleNow Thread-unsafe invocation!");

  Scheduler::Event ev;
  ev.impl = event;
  ev.key.m_ts = m_currentTs;
  ev.key.m_context = GetContext ();
  ev.key.m_uid = m_uid;
  m_uid++;
  m_unscheduledEvents++;
  m_events->Insert (ev);
  return EventId (event, ev.key.m_ts, ev.key.m_context, ev.key.m_uid);
}

EventId
CosimSimulatorImpl::ScheduleDestroy (EventImpl *event)
{
  NS_ASSERT_MSG (SystemThread::Equals (m_main), "Simulator::ScheduleDestroy Thread-unsafe invocation!");

  EventId id (Ptr<EventImpl> (event, false), m_currentTs, 0xffffffff, 2);
  m_destroyEvents.push_back (id);
  m_uid++;
  return id;
}

Time
CosimSimulatorImpl::Now (void) const
{
  // Do not add function logging here, to avoid stack overflow
  return TimeStep (m_currentTs);
}

Time
CosimSimulatorImpl::GetDelayLeft (const EventId &id) const
{
  if (IsExpired (id))
    {
      return TimeStep (0);
    }
  else
    {
      return TimeStep (id.GetTs () - m_currentTs);
    }
}

void
CosimSimulatorImpl::Remove (const EventId &id)
{
  if (id.GetUid () == 2)
    {
      // destroy events.
      for (DestroyEvents::iterator i = m_destroyEvents.begin (); i != m_destroyEvents.end (); i++)
        {
          if (*i == id)
            {
              m_destroyEvents.erase (i);
              break;
            }
        }
      return;
    }
  if (IsExpired (id))
    {
      return;
    }
  Scheduler::Event event;
  event.impl = id.PeekEventImpl ();
  event.key.m_ts = id.GetTs ();
  event.key.m_context = id.GetContext ();
  event.key.m_uid = id.GetUid ();
  m_events->Remove (event);
  event.impl->Cancel ();
  // whenever we remove an event from the event list, we have to unref it.
  event.impl->Unref ();

  m_unscheduledEvents--;
}

void
CosimSimulatorImpl::Cancel (const EventId &id)
{
  if (!IsExpired (id))
    {
      id.PeekEventImpl ()->Cancel ();
    }
}

bool
CosimSimulatorImpl::IsExpired (const EventId &id) const
{
  if (id.GetUid () == 2)
    {
      if (id.PeekEventImpl () == 0
          || id.PeekEventImpl ()->IsCancelled ())
        {
          return true;
        }
      // destroy events.
      for (DestroyEvents::const_iterator i = m_destroyEvents.begin (); i != m_destroyEvents.end (); i++)
        {
          if (*i == id)
            {
              return false;
            }
        }
      return true;
    }
  if (id.PeekEventImpl () == 0
      || id.GetTs () < m_currentTs
      || (id.GetTs () == m_currentTs && id.GetUid () <= m_currentUid)
      || id.PeekEventImpl ()->IsCancelled ())
    {
      return true;
    }
  else
    {
      return false;
    }
}

Time
CosimSimulatorImpl::GetMaximumSimulationTime (void) const
{
  return TimeStep (0x7fffffffffffffffLL);
}

uint32_t
CosimSimulatorImpl::GetContext (void) const
{
  return m_currentContext;
}

uint64_t
CosimSimulatorImpl::GetEventCount (void) const
{
  return m_eventCount;
}

} // namespace ns3
