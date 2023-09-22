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

#include "cosim-manager.h"

#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/nstime.h"
#include "ns3/simbricks-receiver.h"
#include "ns3/node.h"
#include "ns3/node-list.h"

#include <simbricks/base/cxxatomicfix.h>
#include <unistd.h>
extern "C" {
#include <simbricks/network/if.h>
#include <simbricks/network/proto.h>
#include <simbricks/nicif/nicif.h>
}

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("CosimManager");

CosimManager::CosimManager ()
{
  // SimbricksNetIfDefaultParams(&m_bifparam);
  // m_bifparam.sync_mode = (enum SimbricksBaseIfSyncMode)1;
  m_syncMode = true;
}

CosimManager::~CosimManager ()
{
  for(auto i = m_nsif.begin(); i != m_nsif.end(); i++) delete i->second;
  for(auto i = m_bifparam.begin(); i != m_bifparam.end(); i++) delete i->second;
}

void CosimManager::StartCreate (std::map<uint32_t, std::map<uint32_t,uint64_t>> &conns, int systemId)
{
  
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
      m_syncTxEvent[i->first] = Simulator::ScheduleNow (&CosimManager::SendSyncEvent, this, i->first);

    m_pollEvent[i->first] = Simulator::ScheduleNow (&CosimManager::PollEvent, this, i->first);
  }
}

void CosimManager::Stop ()
{
  for(auto i = m_nsif.begin(); i != m_nsif.end(); i++){
    if (m_syncMode)
      Simulator::Cancel (m_syncTxEvent[i->first]);
    Simulator::Cancel (m_pollEvent[i->first]);
  }
}

bool CosimManager::Transmit (Ptr<Packet> packet, const Time& rxTime, uint32_t node, uint32_t dev)
{
  volatile union SimbricksProtoNetMsg *msg;
  volatile struct SimbricksProtoNetMsgPacket *recv;

  /*NS_ABORT_MSG_IF (packet->GetSize () > 2048,
          "CosimManager::Transmit: packet too large");*/

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

  if (m_syncMode) {
    Simulator::Cancel (m_syncTxEvent[systemId]);
    m_syncTxEvent[systemId] = Simulator::Schedule ( PicoSeconds (m_bifparam[systemId]->sync_interval),
            &CosimManager::SendSyncEvent, this, systemId);
  }

  return true;
}

void CosimManager::ReceivedPacket (const void *buf, size_t len, uint64_t time)
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
    Simulator::ScheduleWithContext (pNode->GetId (), Seconds(0.0),
                                    &SimbricksReceiver::Receive, pMpiRec, packet);
}

volatile union SimbricksProtoNetMsg *CosimManager::AllocTx (int systemId)
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

bool CosimManager::Poll (int systemId)
{
  volatile union SimbricksProtoNetMsg *msg;
  uint8_t ty;

  msg = SimbricksNetIfInPoll (m_nsif[systemId], Simulator::Now ().ToInteger (Time::PS));
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
      NS_ABORT_MSG ("CosimManager::Poll: unsupported message type " << ty);
  }

  SimbricksNetIfInDone (m_nsif[systemId], msg);
  return true;
}


void CosimManager::PollEvent (int systemId)
{
  // std::cout << Simulator::Now () << "\n";
  while (Poll (systemId));

  if (m_syncMode){
    while (m_nextTime[systemId] <= Simulator::Now ()){
      Poll (systemId);}

    m_pollEvent[systemId] = Simulator::Schedule (m_nextTime[systemId] -  Simulator::Now (),
            &CosimManager::PollEvent, this, systemId);
  } else {
    m_pollEvent[systemId] = Simulator::Schedule (m_pollDelay[systemId],
            &CosimManager::PollEvent, this, systemId);

  }
}

void CosimManager::SendSyncEvent (int systemId)
{
  volatile union SimbricksProtoNetMsg *msg = AllocTx (systemId);
  NS_ABORT_MSG_IF (msg == NULL,
          "SimbricksAdapter::AllocTx: SimbricksNetIfOutAlloc failed");

  // msg->sync.own_type = SIMBRICKS_PROTO_NET_N2D_MSG_SYNC |
  //     SIMBRICKS_PROTO_NET_N2D_OWN_DEV;
  SimbricksBaseIfOutSend(&m_nsif[systemId]->base, &msg->base, SIMBRICKS_PROTO_MSG_TYPE_SYNC);
  // std::cout << Simulator:: Now() << "\n";

  m_syncTxEvent[systemId] = Simulator::Schedule ( PicoSeconds (m_bifparam[systemId]->sync_interval), &CosimManager::SendSyncEvent, this, systemId);
}

}
