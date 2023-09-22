/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

/*
 * Copyright 2020 Max Planck Institute for Software Systems
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef COSIM_MANAGER_H
#define COSIM_MANAGER_H

#include "ns3/callback.h"
#include "ns3/nstime.h"
#include "ns3/packet.h"
#include "ns3/event-id.h"
#include "ns3/node.h"
#include "ns3/node-list.h"
#include <string>
#include <map>

#include <simbricks/base/cxxatomicfix.h>
extern "C" {
#include <simbricks/network/if.h>
#include <simbricks/network/proto.h>
}

namespace ns3 {
class CosimManager
{
public:
  std::map<uint32_t, SimbricksBaseIfParams*> m_bifparam;
  std::map<uint32_t, Time> m_pollDelay;
  std::string dir;
  
  CosimManager ();
  ~CosimManager ();

  void StartCreate (std::map<uint32_t, std::map<uint32_t,uint64_t>> &conns, int systemId);
  void Stop ();

  typedef Callback<void, Ptr<Packet>, uint64_t> RxCallback;

  bool Transmit (Ptr<Packet> p, const Time& rxTime, uint32_t node, uint32_t dev);

private:
  std::map<uint32_t, SimbricksNetIf*> m_nsif;
  std::map<uint32_t, bool> m_isConnected;
  std::map<uint32_t, Time> m_nextTime;
  std::map<uint32_t, EventId> m_syncTxEvent;
  std::map<uint32_t, EventId> m_pollEvent;
  bool m_syncMode;

  void ReceivedPacket (const void *buf, size_t len, uint64_t time);
  volatile union SimbricksProtoNetMsg *AllocTx (int systemId);
  bool Poll (int systemId);
  void PollEvent (int systemId);
  void SendSyncEvent (int systemId);

};

}

#endif /* COSIM_MANAGER_H */
