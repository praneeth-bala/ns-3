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

#ifndef COSIM_SIMULATOR_IMPL_H
#define COSIM_SIMULATOR_IMPL_H

#include "ns3/simbricks-receiver.h"
#include "ns3/simulator-impl.h"
#include "ns3/scheduler.h"
#include "ns3/event-impl.h"
#include "ns3/system-thread.h"
#include "ns3/callback.h"
#include "ns3/nstime.h"
#include "ns3/system-mutex.h"
#include "ns3/node.h"
#include "ns3/node-list.h"
#include "ns3/node-container.h"
#include "ns3/event-id.h"
#include "ns3/ptr.h"

#include <ns3/channel.h>
#include <chrono>
#include <list>
#include <string>
#include <map>
#include <simbricks/base/cxxatomicfix.h>
#include <unistd.h>
extern "C" {
#include <simbricks/network/if.h>
#include <simbricks/network/proto.h>
#include <simbricks/nicif/nicif.h>
}

/**
 * \file
 * \ingroup simulator
 * ns3::CosimSimulatorImpl declaration.
 */

namespace ns3 {

/**
 * \ingroup simulator
 *
 * The Cosim single process simulator implementation.
 */
class CosimSimulatorImpl : public SimulatorImpl
{
public:
  /**
   *  Register this type.
   *  \return The object TypeId.
   */
  std::map<uint32_t, SimbricksBaseIfParams*> m_bifparam;
  std::map<uint32_t, Time> m_pollDelay;
  std::string dir;
  int systemId;

  static TypeId GetTypeId (void);

  /** Constructor. */
  CosimSimulatorImpl ();
  /** Destructor. */
  ~CosimSimulatorImpl ();

  // Inherited
  virtual void Destroy ();
  virtual bool IsFinished (void) const;
  virtual void Stop (void);
  virtual void Stop (const Time &delay);
  virtual EventId Schedule (const Time &delay, EventImpl *event);
  virtual void ScheduleWithContext (uint32_t context, const Time &delay, EventImpl *event);
  virtual EventId ScheduleNow (EventImpl *event);
  virtual EventId ScheduleDestroy (EventImpl *event);
  virtual void Remove (const EventId &id);
  virtual void Cancel (const EventId &id);
  virtual bool IsExpired (const EventId &id) const;
  virtual void Run (void);
  virtual Time Now (void) const;
  virtual Time GetDelayLeft (const EventId &id) const;
  virtual Time GetMaximumSimulationTime (void) const;
  virtual void SetScheduler (ObjectFactory schedulerFactory);
  virtual uint32_t GetSystemId (void) const;
  virtual uint32_t GetContext (void) const;
  virtual uint64_t GetEventCount (void) const;
  bool Transmit (Ptr<Packet> p, const Time& rxTime, uint32_t node, uint32_t dev);

private:
  virtual void DoDispose (void);

  /** Process the next event. */
  void ProcessOneEvent (void);
  /** Move events from a different context into the main event queue. */
  void ProcessEventsWithContext (void);

  void InitMap (void);
  void SetupInterconnections (void);

  void ReceivedPacket (const void *buf, size_t len, uint64_t time);
  volatile union SimbricksProtoNetMsg *AllocTx (int systemId);
  bool Poll (int systemId);
  void PollEvent (int systemId);
  void SendSyncEvent (int systemId);

  std::map<uint32_t, SimbricksNetIf*> m_nsif;
  std::map<uint32_t, bool> m_isConnected;
  std::map<uint32_t, Time> m_nextTime;
  std::map<uint32_t, EventId> m_syncTxEvent;
  std::map<uint32_t, EventId> m_pollEvent;
  bool m_syncMode;

  /** Wrap an event with its execution context. */
  struct EventWithContext
  {
    /** The event context. */
    uint32_t context;
    /** Event timestamp. */
    uint64_t timestamp;
    /** The event implementation. */
    EventImpl *event;
  };
  /** Container type for the events from a different context. */
  typedef std::list<struct EventWithContext> EventsWithContext;
  /** The container of events from a different context. */
  EventsWithContext m_eventsWithContext;
  /**
   * Flag \c true if all events with context have been moved to the
   * primary event queue.
   */
  bool m_eventsWithContextEmpty;
  /** Mutex to control access to the list of events with context. */
  SystemMutex m_eventsWithContextMutex;

  /** Container type for the events to run at Simulator::Destroy() */
  typedef std::list<EventId> DestroyEvents;
  /** The container of events to run at Destroy. */
  DestroyEvents m_destroyEvents;
  /** Flag calling for the end of the simulation. */
  bool m_stop;
  /** The event priority queue. */
  Ptr<Scheduler> m_events;

  /** Next event unique id. */
  uint32_t m_uid;
  /** Unique id of the current event. */
  uint32_t m_currentUid;
  /** Timestamp of the current event. */
  uint64_t m_currentTs;
  /** Execution context of the current event. */
  uint32_t m_currentContext;
  /** The event count. */
  uint64_t m_eventCount;
  /**
   * Number of events that have been inserted but not yet scheduled,
   *  not counting the Destroy events; this is used for validation
   */
  int m_unscheduledEvents;

  /** Main execution thread. */
  SystemThread::ThreadId m_main;

  std::map<uint32_t, std::map<uint32_t,uint64_t>> conns;
};

} // namespace ns3

#endif /* Cosim_SIMULATOR_IMPL_H */
