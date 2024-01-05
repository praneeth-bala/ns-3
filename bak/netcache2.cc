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
#include "kvstore.h"
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
using namespace kv;
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

MSG::MSG()
    : buf_(nullptr), len_(0), dealloc_(false)
{
}

MSG::MSG(void *buf, size_t len, bool dealloc)
    : buf_(buf), len_(len), dealloc_(dealloc)
{
}

MSG::MSG(const std::string &str)
{
    this->buf_ = malloc(str.size());
    memcpy(this->buf_, str.data(), str.size());
    this->len_ = str.size();
    this->dealloc_ = true;
}

MSG::~MSG()
{
    if (this->dealloc_ && this->buf_ != nullptr) {
        free(this->buf_);
    }
}

const void *MSG::buf() const
{
    return this->buf_;
}

size_t MSG::len() const
{
    return this->len_;
}

void MSG::set_MSG(void *buf, size_t len, bool dealloc)
{
    if (this->buf_ != nullptr) {
        free(this->buf_);
    }
    this->buf_ = buf;
    this->len_ = len;
    this->dealloc_ = dealloc;
}

bool WireCodec::decode(const MSG &in, MemcacheKVMSG &out)
{
    const char *ptr = (const char*)in.buf();
    size_t buf_size = in.len();

    if (buf_size < PACKET_BASE_SIZE) {
        return false;
    }
    if (this->proto_enable && *(identifier_t*)ptr != PEGASUS) {
        return false;
    }
    if (!this->proto_enable && *(identifier_t*)ptr != STATIC) {
        return false;
    }
    // Header
    ptr += sizeof(identifier_t);
    op_type_t op_type = *(op_type_t*)ptr;
    ptr += sizeof(op_type_t);
    keyhash_t keyhash;
    convert_endian(&keyhash, ptr, sizeof(keyhash_t));
    ptr += sizeof(keyhash_t);
    uint8_t client_id = *(node_t*)ptr;
    ptr += sizeof(node_t);
    uint8_t server_id = *(node_t*)ptr;
    ptr += sizeof(node_t);
    load_t load;
    convert_endian(&load, ptr, sizeof(load_t));
    ptr += sizeof(load_t);
    ver_t ver;
    convert_endian(&ver, ptr, sizeof(ver_t));
    ptr += sizeof(ver_t);
    ptr += sizeof(bitmap_t);
    ptr += sizeof(hdr_req_id_t);

    // Payload
    switch (op_type) {
    case OP_GET:
    case OP_PUT:
    case OP_DEL:
    case OP_PUT_FWD: {
        // Request
        if (buf_size < REQUEST_BASE_SIZE) {
            return false;
        }
        out.type = MemcacheKVMSG::Type::REQUEST;
        out.request.client_id = client_id;
        out.request.server_id = server_id;
        out.request.req_id = *(req_id_t*)ptr;
        ptr += sizeof(req_id_t);
        out.request.req_time = *(req_time_t*)ptr;
        ptr += sizeof(req_time_t);
        out.request.op.op_type = static_cast<OpType>(*(op_type_t*)ptr);
        ptr += sizeof(op_type_t);
        out.request.op.keyhash = keyhash;
        out.request.op.ver = ver;
        key_len_t key_len = *(key_len_t*)ptr;
        ptr += sizeof(key_len_t);
        if (buf_size < REQUEST_BASE_SIZE + key_len) {
            return false;
        }
        out.request.op.key = string(ptr, key_len);
        ptr += key_len;
        if (op_type == OP_PUT || op_type == OP_PUT_FWD) {
            if (buf_size < REQUEST_BASE_SIZE + key_len + sizeof(value_len_t)) {
                return false;
            }
            value_len_t value_len = *(value_len_t*)ptr;
            ptr += sizeof(value_len_t);
            if (buf_size < REQUEST_BASE_SIZE + key_len + sizeof(value_len_t) + value_len) {
                return false;
            }
            out.request.op.value = string(ptr, value_len);
        }
        break;
    }
    case OP_REP_R:
    case OP_REP_W: {
        if (buf_size < REPLY_BASE_SIZE) {
            return false;
        }
        out.type = MemcacheKVMSG::Type::REPLY;
        out.reply.client_id = client_id;
        out.reply.server_id = server_id;
        out.reply.keyhash = keyhash;
        out.reply.load = load;
        out.reply.ver = ver;
        out.reply.req_id = *(req_id_t*)ptr;
        ptr += sizeof(req_id_t);
        out.reply.req_time = *(req_time_t*)ptr;
        ptr += sizeof(req_time_t);
        out.reply.op_type = static_cast<OpType>(*(op_type_t*)ptr);
        ptr += sizeof(op_type_t);
        out.reply.result = static_cast<Result>(*(result_t*)ptr);
        ptr += sizeof(result_t);
        value_len_t value_len = *(value_len_t*)ptr;
        ptr += sizeof(value_len_t);
        if (buf_size < REPLY_BASE_SIZE + value_len) {
            return false;
        }
        out.reply.value = string(ptr, value_len);
        break;
    }
    case OP_RC_REQ: {
        if (buf_size < RC_REQ_BASE_SIZE) {
            return false;
        }
        out.type = MemcacheKVMSG::Type::RC_REQ;
        out.rc_request.keyhash = keyhash;
        out.rc_request.ver = ver;
        key_len_t key_len = *(key_len_t *)ptr;
        ptr += sizeof(key_len_t);
        out.rc_request.key = string(ptr, key_len);
        ptr += key_len;
        value_len_t value_len = *(value_len_t *)ptr;
        ptr += sizeof(value_len_t);
        out.rc_request.value = string(ptr, value_len);
        ptr += value_len;
        break;
    }
    case OP_RC_ACK: {
        // panic("Server should never receive RC_ACK");
        break;
    }
    default:
        return false;
    }
    return true;
}

bool WireCodec::encode(MSG &out, const MemcacheKVMSG &in)
{
    // First determine buffer size
    size_t buf_size;
    switch (in.type) {
    case MemcacheKVMSG::Type::REQUEST: {
        buf_size = REQUEST_BASE_SIZE + in.request.op.key.size();
        if (in.request.op.op_type == OpType::PUT ||
            in.request.op.op_type == OpType::PUTFWD) {
            buf_size += sizeof(value_len_t) + in.request.op.value.size();
        }
        break;
    }
    case MemcacheKVMSG::Type::REPLY: {
        buf_size = REPLY_BASE_SIZE + in.reply.value.size();
        break;
    }
    case MemcacheKVMSG::Type::RC_REQ: {
        buf_size = RC_REQ_BASE_SIZE + in.rc_request.key.size() + in.rc_request.value.size();
        break;
    }
    case MemcacheKVMSG::Type::RC_ACK: {
        buf_size = RC_ACK_BASE_SIZE;
        break;
    }
    default:
        return false;
    }

    char *buf = (char*)malloc(buf_size);
    char *ptr = buf;
    // Header
    if (this->proto_enable) {
        *(identifier_t*)ptr = PEGASUS;
    } else {
        *(identifier_t*)ptr = STATIC;
    }
    ptr += sizeof(identifier_t);
    switch (in.type) {
    case MemcacheKVMSG::Type::REQUEST: {
        switch (in.request.op.op_type) {
        case OpType::GET:
            *(op_type_t*)ptr = OP_GET;
            break;
        case OpType::PUT:
            *(op_type_t*)ptr = OP_PUT;
            break;
        case OpType::DEL:
            *(op_type_t*)ptr = OP_DEL;
            break;
        case OpType::PUTFWD:
            *(op_type_t*)ptr = OP_PUT_FWD;
            break;
        default:
            return false;
        }
        ptr += sizeof(op_type_t);
        keyhash_t hash = (keyhash_t)compute_keyhash(in.request.op.key);
        convert_endian(ptr, &hash, sizeof(keyhash_t));
        ptr += sizeof(keyhash_t);
        *(node_t*)ptr = in.request.client_id;
        ptr += sizeof(node_t);
        *(node_t*)ptr = in.request.server_id;
        ptr += sizeof(node_t);
        ptr += sizeof(load_t);
        ver_t base_version = BASE_VERSION;
        convert_endian(ptr, &base_version, sizeof(ver_t));
        ptr += sizeof(ver_t);
        *(bitmap_t*)ptr = 0;
        ptr += sizeof(bitmap_t);
        *(hdr_req_id_t*)ptr = (hdr_req_id_t)in.request.req_id;
        ptr += sizeof(hdr_req_id_t);
        break;
    }
    case MemcacheKVMSG::Type::REPLY: {
        switch (in.reply.op_type) {
        case OpType::GET:
            *(op_type_t*)ptr = OP_REP_R;
            break;
        case OpType::PUT:
        case OpType::DEL:
            *(op_type_t*)ptr = OP_REP_W;
            break;
        default:
            return false;
        }
        ptr += sizeof(op_type_t);
        convert_endian(ptr, &in.reply.keyhash, sizeof(keyhash_t));
        ptr += sizeof(keyhash_t);
        *(node_t*)ptr = in.reply.client_id;
        ptr += sizeof(node_t);
        *(node_t*)ptr = in.reply.server_id;
        ptr += sizeof(node_t);
        convert_endian(ptr, &in.reply.load, sizeof(load_t));
        ptr += sizeof(load_t);
        convert_endian(ptr, &in.reply.ver, sizeof(ver_t));
        ptr += sizeof(ver_t);
        bitmap_t bitmap = 1 << in.reply.server_id;
        convert_endian(ptr, &bitmap, sizeof(bitmap_t));
        ptr += sizeof(bitmap_t);
        *(hdr_req_id_t*)ptr = (hdr_req_id_t)in.reply.req_id;
        ptr += sizeof(hdr_req_id_t);
        break;
    }
    case MemcacheKVMSG::Type::RC_REQ: {
        *(op_type_t*)ptr = OP_RC_REQ;
        ptr += sizeof(op_type_t);
        convert_endian(ptr, &in.rc_request.keyhash, sizeof(keyhash_t));
        ptr += sizeof(keyhash_t);
        ptr += sizeof(node_t);
        ptr += sizeof(node_t);
        ptr += sizeof(load_t);
        convert_endian(ptr, &in.rc_request.ver, sizeof(ver_t));
        ptr += sizeof(ver_t);
        ptr += sizeof(bitmap_t);
        ptr += sizeof(hdr_req_id_t);
        break;
    }
    case MemcacheKVMSG::Type::RC_ACK: {
        *(op_type_t*)ptr = OP_RC_ACK;
        ptr += sizeof(op_type_t);
        convert_endian(ptr, &in.rc_ack.keyhash, sizeof(keyhash_t));
        ptr += sizeof(keyhash_t);
        ptr += sizeof(node_t);
        *(node_t*)ptr = in.rc_ack.server_id;
        ptr += sizeof(node_t);
        ptr += sizeof(load_t);
        convert_endian(ptr, &in.rc_ack.ver, sizeof(ver_t));
        ptr += sizeof(ver_t);
        bitmap_t bitmap = 1 << in.rc_ack.server_id;
        convert_endian(ptr, &bitmap, sizeof(bitmap_t));
        ptr += sizeof(bitmap_t);
        ptr += sizeof(hdr_req_id_t);
        break;
    }
    default:
        return false;
    }

    // Payload
    switch (in.type) {
    case MemcacheKVMSG::Type::REQUEST: {
        *(req_id_t*)ptr = (req_id_t)in.request.req_id;
        ptr += sizeof(req_id_t);
        *(req_time_t*)ptr = (req_time_t)in.request.req_time;
        ptr += sizeof(req_time_t);
        *(op_type_t*)ptr = static_cast<op_type_t>(in.request.op.op_type);
        ptr += sizeof(op_type_t);
        *(key_len_t*)ptr = (key_len_t)in.request.op.key.size();
        ptr += sizeof(key_len_t);
        memcpy(ptr, in.request.op.key.data(), in.request.op.key.size());
        ptr += in.request.op.key.size();
        if (in.request.op.op_type == OpType::PUT ||
            in.request.op.op_type == OpType::PUTFWD) {
            *(value_len_t*)ptr = (value_len_t)in.request.op.value.size();
            ptr += sizeof(value_len_t);
            memcpy(ptr, in.request.op.value.data(), in.request.op.value.size());
            ptr += in.request.op.value.size();
        }
        break;
    }
    case MemcacheKVMSG::Type::REPLY: {
        *(req_id_t*)ptr = (req_id_t)in.reply.req_id;
        ptr += sizeof(req_id_t);
        *(req_time_t*)ptr = (req_time_t)in.reply.req_time;
        ptr += sizeof(req_time_t);
        *(op_type_t*)ptr = static_cast<op_type_t>(in.reply.op_type);
        ptr += sizeof(op_type_t);
        *(result_t *)ptr = (result_t)in.reply.result;
        ptr += sizeof(result_t);
        *(value_len_t *)ptr = (value_len_t)in.reply.value.size();
        ptr += sizeof(value_len_t);
        if (in.reply.value.size() > 0) {
            memcpy(ptr, in.reply.value.data(), in.reply.value.size());
            ptr += in.reply.value.size();
        }
        break;
    }
    case MemcacheKVMSG::Type::RC_REQ: {
        *(key_len_t *)ptr = (key_len_t)in.rc_request.key.size();
        ptr += sizeof(key_len_t);
        memcpy(ptr, in.rc_request.key.data(), in.rc_request.key.size());
        ptr += in.rc_request.key.size();
        *(value_len_t *)ptr = (value_len_t)in.rc_request.value.size();
        ptr += sizeof(value_len_t);
        memcpy(ptr, in.rc_request.value.data(), in.rc_request.value.size());
        ptr += in.rc_request.value.size();
        break;
    }
    case MemcacheKVMSG::Type::RC_ACK: {
        // empty
        break;
    }
    default:
        return false;
    }

    out.set_MSG(buf, buf_size, true);
    return true;
}

bool NetcacheCodec::decode(const MSG &in, MemcacheKVMSG &out)
{
    const char *ptr = (const char*)in.buf();
    size_t buf_size = in.len();

    if (buf_size < PACKET_BASE_SIZE) {
        return false;
    }
    if (*(identifier_t*)ptr != NETCACHE) {
        return false;
    }
    ptr += sizeof(identifier_t);
    op_type_t op_type = *(op_type_t*)ptr;
    ptr += sizeof(op_type_t);
    ptr += KEY_SIZE;
    string cached_value = string(ptr, VALUE_SIZE);
    ptr += VALUE_SIZE;

    switch (op_type) {
    case OP_READ:
    case OP_WRITE: {
        if (buf_size < REQUEST_BASE_SIZE) {
            return false;
        }
        out.type = MemcacheKVMSG::Type::REQUEST;
        out.request.client_id = *(client_id_t*)ptr;
        ptr += sizeof(client_id_t);
        out.request.req_id = *(req_id_t*)ptr;
        ptr += sizeof(req_id_t);
        out.request.req_time = *(req_time_t*)ptr;
        ptr += sizeof(req_time_t);
        out.request.op.op_type = static_cast<OpType>(*(op_type_t*)ptr);
        ptr += sizeof(op_type_t);
        key_len_t key_len = *(key_len_t*)ptr;
        ptr += sizeof(key_len_t);
        if (buf_size < REQUEST_BASE_SIZE + key_len) {
            return false;
        }
        out.request.op.key = string(ptr, key_len);
        ptr += key_len;
        if (out.request.op.op_type == OpType::PUT) {
            if (buf_size < REQUEST_BASE_SIZE + key_len + sizeof(value_len_t)) {
                return false;
            }
            value_len_t value_len = *(value_len_t*)ptr;
            ptr += sizeof(value_len_t);
            if (buf_size < REQUEST_BASE_SIZE + key_len + sizeof(value_len_t) + value_len) {
                return false;
            }
            out.request.op.value = string(ptr, value_len);
        }
        break;
    }
    case OP_REP_R:
    case OP_REP_W: {
        if (buf_size < REPLY_BASE_SIZE) {
            return false;
        }
        out.type = MemcacheKVMSG::Type::REPLY;
        out.reply.server_id = *(server_id_t*)ptr;
        ptr += sizeof(server_id_t);
        out.reply.client_id = *(client_id_t*)ptr;
        ptr += sizeof(client_id_t);
        out.reply.req_id = *(req_id_t*)ptr;
        ptr += sizeof(req_id_t);
        out.reply.req_time = *(req_time_t*)ptr;
        ptr += sizeof(req_time_t);
        out.reply.op_type = static_cast<OpType>(*(op_type_t*)ptr);
        ptr += sizeof(op_type_t);
        out.reply.result = static_cast<Result>(*(result_t*)ptr);
        ptr += sizeof(result_t);
        value_len_t value_len = *(value_len_t*)ptr;
        ptr += sizeof(value_len_t);
        if (buf_size < REPLY_BASE_SIZE + value_len) {
            return false;
        }
        out.reply.value = string(ptr, value_len);
        break;
    }
    case OP_CACHE_HIT: {
        if (buf_size < REQUEST_BASE_SIZE) {
            return false;
        }
        out.type = MemcacheKVMSG::Type::REPLY;
        out.reply.server_id = SWITCH_ID;
        out.reply.client_id = *(client_id_t*)ptr;
        ptr += sizeof(client_id_t);
        out.reply.req_id = *(req_id_t*)ptr;
        ptr += sizeof(req_id_t);
        out.reply.req_time = *(req_time_t*)ptr;
        ptr += sizeof(req_time_t);
        out.reply.op_type = static_cast<OpType>(*(op_type_t*)ptr);
        ptr += sizeof(op_type_t);
        out.reply.result = Result::OK;
        out.reply.value = cached_value;
        break;
    }
    default:
        return false;
    }
    return true;
}

bool NetcacheCodec::encode(MSG &out, const MemcacheKVMSG &in)
{
    // First determine buffer size
    size_t buf_size;
    switch (in.type) {
    case MemcacheKVMSG::Type::REQUEST: {
        buf_size = REQUEST_BASE_SIZE + in.request.op.key.size();
        if (in.request.op.op_type == OpType::PUT) {
            buf_size += sizeof(value_len_t) + in.request.op.value.size();
        }
        break;
    }
    case MemcacheKVMSG::Type::REPLY: {
        buf_size = REPLY_BASE_SIZE + in.reply.value.size();
        break;
    }
    default:
        return false;
    }

    char *buf = (char*)malloc(buf_size);
    char *ptr = buf;
    // App header
    *(identifier_t*)ptr = NETCACHE;
    ptr += sizeof(identifier_t);
    switch (in.type) {
    case MemcacheKVMSG::Type::REQUEST: {
        switch (in.request.op.op_type) {
        case OpType::GET:
            *(op_type_t*)ptr = OP_READ;
            break;
        case OpType::PUT:
        case OpType::DEL:
            *(op_type_t*)ptr = OP_WRITE;
            break;
        default:
            return false;
        }
        ptr += sizeof(op_type_t);
        memset(ptr, 0, KEY_SIZE);
        memcpy(ptr, in.request.op.key.data(), std::min(in.request.op.key.size(),
                                                       KEY_SIZE));
        ptr += KEY_SIZE;
        ptr += VALUE_SIZE;
        break;
    }
    case MemcacheKVMSG::Type::REPLY: {
        switch (in.reply.op_type) {
        case OpType::GET:
            *(op_type_t*)ptr = OP_REP_R;
            break;
        case OpType::PUT:
        case OpType::DEL:
            *(op_type_t*)ptr = OP_REP_W;
            break;
        default:
            return false;
        }
        ptr += sizeof(op_type_t);
        memset(ptr, 0, KEY_SIZE);
        memcpy(ptr, in.reply.key.data(), std::min(in.reply.key.size(),
                                                  KEY_SIZE));
        ptr += KEY_SIZE;
        memset(ptr, 0, VALUE_SIZE);
        memcpy(ptr, in.reply.value.data(), std::min(in.reply.value.size(),
                                                    VALUE_SIZE));
        ptr += VALUE_SIZE;
        break;
    }
    default:
        return false;
    }

    // Payload
    switch (in.type) {
    case MemcacheKVMSG::Type::REQUEST: {
        *(client_id_t*)ptr = (client_id_t)in.request.client_id;
        ptr += sizeof(client_id_t);
        *(req_id_t*)ptr = (req_id_t)in.request.req_id;
        ptr += sizeof(req_id_t);
        *(req_time_t*)ptr = (req_time_t)in.request.req_time;
        ptr += sizeof(req_time_t);
        *(op_type_t*)ptr = static_cast<op_type_t>(in.request.op.op_type);
        ptr += sizeof(op_type_t);
        *(key_len_t*)ptr = (key_len_t)in.request.op.key.size();
        ptr += sizeof(key_len_t);
        memcpy(ptr, in.request.op.key.data(), in.request.op.key.size());
        ptr += in.request.op.key.size();
        if (in.request.op.op_type == OpType::PUT) {
            *(value_len_t*)ptr = (value_len_t)in.request.op.value.size();
            ptr += sizeof(value_len_t);
            memcpy(ptr, in.request.op.value.data(), in.request.op.value.size());
            ptr += in.request.op.value.size();
        }
        break;
    }
    case MemcacheKVMSG::Type::REPLY: {
        *(server_id_t*)ptr = (server_id_t)in.reply.server_id;
        ptr += sizeof(server_id_t);
        *(client_id_t*)ptr = (client_id_t)in.reply.client_id;
        ptr += sizeof(client_id_t);
        *(req_id_t*)ptr = (req_id_t)in.reply.req_id;
        ptr += sizeof(req_id_t);
        *(req_time_t*)ptr = (req_time_t)in.reply.req_time;
        ptr += sizeof(req_time_t);
        *(op_type_t*)ptr = static_cast<op_type_t>(in.reply.op_type);
        ptr += sizeof(op_type_t);
        *(result_t *)ptr = (result_t)in.reply.result;
        ptr += sizeof(result_t);
        *(value_len_t *)ptr = (value_len_t)in.reply.value.size();
        ptr += sizeof(value_len_t);
        if (in.reply.value.size() > 0) {
            memcpy(ptr, in.reply.value.data(), in.reply.value.size());
            ptr += in.reply.value.size();
        }
        break;
    }
    default:
        return false;
    }

    out.set_MSG(buf, buf_size, true);
    return true;
}

class Client : public Application 
{
public:

    Client (uint32_t client_id, InetSocketAddress bind, uint32_t num_nodes, int interval);
    virtual ~Client();

    void Setup (Address address, uint32_t server_id);

private:
    virtual void StartApplication (void);
    virtual void StopApplication (void);
    virtual void RcvCallbk (Ptr<Socket> s);

    void ScheduleTx (long time);
    void SendPacket ();

    std::map<uint32_t, Ptr<Socket>>     m_socket;
    std::map<uint32_t, Address>         m_peer;
    uint32_t        m_nPackets;
    uint32_t        m_client_id;
    uint32_t        m_req_id;
    uint32_t        m_num_nodes;
    EventId         m_sendEvent;
    bool            m_running;
    uint32_t        m_packetsSent;
    uint32_t        m_issued;
    uint32_t        m_completed;
    double          m_total_latency;
    MemcacheKVMSG   m_kvmsg;
    std::vector<float>          zipfs;
    std::vector<std::string>    keys;
    float           alpha;
    float           m_get_ratio;
    float           m_put_ratio;
    std::uniform_real_distribution<float> m_unif_real_dist;
    std::poisson_distribution<long> m_poisson_dist;
    std::default_random_engine m_generator;
    InetSocketAddress m_bind;
    MSGCodec        *codec;

};

class Server : public Application 
{
public:

    Server (uint32_t server_id, InetSocketAddress bind, uint32_t num_nodes);
    virtual ~Server();

    void Setup (Address address, uint32_t client_id);

private:
    virtual void StartApplication (void);
    virtual void StopApplication (void);
    virtual void RcvCallbk (Ptr<Socket> s);
    virtual void process_op(const Operation &op, MemcacheKVReply &reply);

    void SendPacket (uint32_t node);

    std::map<uint32_t, Ptr<Socket>>     m_socket;
    std::map<uint32_t, Address>         m_peer;
    std::map<std::string,std::pair<ver_t, std::string>>   keys;
    uint32_t        m_nPackets;
    uint32_t        m_server_id;
    uint32_t        m_req_id;
    uint32_t        m_num_nodes;
    EventId         m_sendEvent;
    bool            m_running;
    uint32_t        m_packetsSent;
    MemcacheKVMSG   m_kvmsg;
    InetSocketAddress m_bind;
    MSGCodec        *codec;

};

Server::Server (uint32_t server_id, InetSocketAddress bind, uint32_t num_nodes)
  : m_nPackets (1000000), 
    m_sendEvent (), 
    m_running (false),
    m_packetsSent (0),
    m_bind(bind),
    m_server_id(server_id),
    m_num_nodes(num_nodes)
{
    codec = new NetcacheCodec();
    long mean_interval = 10;
    std::ifstream in;
    in.open("/workspaces/simbricks/experiments/pegasus/artifact_eval/keys");
    std::string key;
    for (int i = 0; i < 1000000; i++) {
        getline(in, key);
        keys[key]=make_pair(1, key);
    }
    in.close();
}

Server::~Server()
{
}

void
Server::Setup (Address address, uint32_t client_id)
{
    m_peer[client_id] = address;
}

void
Server::StartApplication (void)
{
    m_running = true;
    m_packetsSent = 0;
    for(auto node: m_peer){
        m_socket[node.first] = Socket::CreateSocket (this->GetNode(), UdpSocketFactory::GetTypeId ());
        m_socket[node.first]->SetRecvCallback(MakeCallback(&Server::RcvCallbk, this));
        m_socket[node.first]->Bind (m_bind);
        m_socket[node.first]->Connect (node.second);
    }
    m_kvmsg.type = MemcacheKVMSG::Type::REPLY;
    m_kvmsg.reply.server_id = m_server_id;
    // InetSocketAddress (Ipv4Address ("10.0.0.4"), 12345)
}

void 
Server::StopApplication (void)
{
    m_running = false;
    if (m_sendEvent.IsRunning ())
    {
      Simulator::Cancel (m_sendEvent);
    }
    for(auto node: m_peer){
        m_socket[node.first]->Close ();
    }
}

void 
Server::SendPacket (uint32_t node)
{
    MSG msg;
    codec->encode(msg, m_kvmsg);

    Ptr<Packet> packet = Create<Packet> ((uint8_t*)msg.buf(), (int)msg.len());
    m_socket[node]->Send (packet);
}

void
Server::process_op(const Operation &op, MemcacheKVReply &reply)
{
    reply.op_type = op.op_type;
    reply.keyhash = op.keyhash;
    reply.key = op.key;
    switch (op.op_type) {
    case OpType::GET: {
        if (keys.find(op.key)!=keys.end()) {
            // Key is present
            reply.ver = keys[op.key].first;
            reply.value = keys[op.key].second;
            reply.result = Result::OK;
        } else {
            // Key not found
            reply.ver = BASE_VERSION;
            reply.value = std::string("");
            reply.result = Result::NOT_FOUND;
        }
        break;
    }
    case OpType::PUT:
    case OpType::PUTFWD: {
        {
            keys[op.key] = make_pair(op.ver, op.value);
        }
        reply.ver = op.ver;
        reply.value = op.value; // for netcache
        reply.result = Result::OK;
        reply.op_type = OpType::PUT; // client doesn't expect PUTFWD
        break;
    }
    default:
        // panic("Unknown memcachekv op type");
        break;
    }
}

void Server::RcvCallbk(Ptr<Socket> s){
    Ptr<Packet> p = s->Recv();
    uint8_t *buffer = new uint8_t[p->GetSize ()];
    int size = p->CopyData(buffer, p->GetSize ());
    MSG msg;
    MemcacheKVMSG kvm;
    msg.set_MSG(buffer, size, true);
    codec->decode(msg, kvm);
    switch (kvm.type) {
    case MemcacheKVMSG::Type::REQUEST: {
        process_op(kvm.request.op, m_kvmsg.reply);

        m_kvmsg.type = MemcacheKVMSG::Type::REPLY;
        m_kvmsg.reply.client_id = kvm.request.client_id;
        m_kvmsg.reply.server_id = m_server_id;
        m_kvmsg.reply.req_id = kvm.request.req_id;
        m_kvmsg.reply.req_time = kvm.request.req_time;

        this->SendPacket(kvm.request.client_id);
        break;
    }
    case MemcacheKVMSG::Type::RC_REQ: {
        bool reply = false;
        if(keys.find(kvm.rc_request.key)!=keys.end()){
            if(keys[kvm.rc_request.key].first<=kvm.rc_request.ver){
                keys[kvm.rc_request.key] = make_pair(kvm.rc_request.ver, kvm.rc_request.value);
                reply = true;
            }
        }
        else{
            keys[kvm.rc_request.key] = make_pair(kvm.rc_request.ver, kvm.rc_request.value);
            reply = true;
        }
        if(reply){
            m_kvmsg.type = MemcacheKVMSG::Type::RC_ACK;
            m_kvmsg.rc_ack.keyhash = kvm.rc_request.keyhash;
            m_kvmsg.rc_ack.ver = kvm.rc_request.ver;
            m_kvmsg.rc_ack.server_id = m_server_id;

            this->SendPacket(kvm.request.client_id);
        }
        break;
    }
    }
}

Client::Client (uint32_t client_id, InetSocketAddress bind, uint32_t num_nodes, int interval)
  : m_total_latency (0.0), 
    m_nPackets (1000000), 
    m_issued (0), 
    m_completed (0), 
    m_sendEvent (), 
    m_running (false),
    m_get_ratio (0.3),
    m_put_ratio (0.7),
    m_packetsSent (0),
    m_client_id (client_id),
    m_num_nodes (num_nodes),
    m_bind (bind)
{
    codec = new NetcacheCodec();
    long mean_interval = interval;
    m_generator = std::default_random_engine(Simulator::Now().GetMicroSeconds()+m_client_id);
    m_unif_real_dist = std::uniform_real_distribution<float>(0.0, 1.0);
    m_poisson_dist = std::poisson_distribution<long>(mean_interval);
    std::ifstream in;
    alpha = 1.8;
    in.open("/workspaces/simbricks/experiments/pegasus/artifact_eval/keys");
    std::string key;
    for (int i = 0; i < 1000000; i++) {
        getline(in, key);
        keys.push_back(key);
    }
    in.close();
    float c = 0;
    for (unsigned int i = 0; i < keys.size(); i++) {
        c = c + (1.0 / pow((float)(i+1), alpha));
    }
    c = 1 / c;
    float sum = 0;
    for (unsigned int i = 0; i < keys.size(); i++) {
        sum += (c / pow((float)(i+1), alpha));
        this->zipfs.push_back(sum);
    }
}

Client::~Client()
{
}

void
Client::Setup (Address address, uint32_t client_id)
{
    m_peer[client_id] = address;
}

void
Client::StartApplication (void)
{
    m_running = true;
    m_packetsSent = 0;
    for(auto node: m_peer){
        m_socket[node.first] = Socket::CreateSocket (this->GetNode(), UdpSocketFactory::GetTypeId ());
        m_socket[node.first]->SetRecvCallback(MakeCallback(&Client::RcvCallbk, this));
        m_socket[node.first]->Bind (m_bind);
        m_socket[node.first]->Connect (node.second);
    }
    m_kvmsg.type = MemcacheKVMSG::Type::REQUEST;
    m_kvmsg.request.client_id = m_client_id;
    SendPacket ();
}

void 
Client::StopApplication (void)
{
    m_running = false;
    std::cerr << "Issued: " << m_issued << " Completed: " << m_completed << "\n";
    std::cerr << "Latency: " << m_total_latency/(m_completed+0.00000000001) << "\n";
    if (m_sendEvent.IsRunning ())
    {
      Simulator::Cancel (m_sendEvent);
    }
    for(auto node: m_peer){
        m_socket[node.first]->Close();
    }
    std::flush(std::cerr);
    // exit(1);
}

void 
Client::ScheduleTx (long time)
{
  if (m_running)
    {
      Time tNext (MicroSeconds (time));
      m_sendEvent = Simulator::Schedule (tNext, &Client::SendPacket, this);
    }
}

void 
Client::SendPacket ()
{
    int tid;
    long time;

    float random = 0.0;
    while (random == 0.0) {
        random = m_unif_real_dist(m_generator);
    }

    int l = 0, r = this->keys.size(), mid = 0;
    while (l < r) {
        mid = (l + r) / 2;
        if (random > this->zipfs.at(mid)) {
            l = mid + 1;
        } else if (random < this->zipfs.at(mid)) {
            r = mid - 1;
        } else {
            break;
        }
    }

    m_kvmsg.request.op.key = this->keys.at(mid);

    float op_choice = m_unif_real_dist(m_generator);
    OpType op_type;
    if (op_choice < m_get_ratio) {
        op_type = OpType::GET;
    } else if (op_choice < m_get_ratio + m_put_ratio) {
        op_type = OpType::PUT;
    } else {
        op_type = OpType::DEL;
    }

    m_kvmsg.request.op.op_type = op_type;
    if (m_kvmsg.request.op.op_type == OpType::PUT) {
        m_kvmsg.request.op.value = string(32, 'v');
    }
    time = m_poisson_dist(m_generator);
    m_kvmsg.request.req_time = Simulator::Now().GetMicroSeconds();
    m_kvmsg.request.server_id = key_to_node_id(m_kvmsg.request.op.key, m_num_nodes);
    m_kvmsg.request.req_id = m_req_id++;
    MSG msg;
    codec->encode(msg, m_kvmsg);

    Ptr<Packet> packet = Create<Packet> ((uint8_t*)msg.buf(), (int)msg.len());
    m_socket[m_kvmsg.request.server_id]->Send (packet);
    m_issued++;
    if (++m_packetsSent < m_nPackets)
    {
      ScheduleTx (time);
    }
}

void Client::RcvCallbk(Ptr<Socket> s){
    Ptr<Packet> p = s->Recv();
    m_completed++;
    uint8_t *buffer = new uint8_t[p->GetSize ()];
    int size = p->CopyData(buffer, p->GetSize ());
    MSG msg;
    MemcacheKVMSG kvm;
    msg.set_MSG(buffer, size, true);
    codec->decode(msg, kvm);
    m_total_latency += ((double)(Simulator::Now().GetMicroSeconds() - kvm.reply.req_time))/1000;
}

void setupIp(Ptr<Node> node, Ptr<NetDevice> dev, Ipv4Address add){
    Ptr<Ipv4> ipv4 = node->GetObject<Ipv4> ();
    uint32_t interface = ipv4->AddInterface (dev);
    Ipv4InterfaceAddress address = Ipv4InterfaceAddress (add, Ipv4Mask("255.255.255.0"));
    ipv4->AddAddress (interface, address);
    ipv4->SetMetric (interface, 1);
    ipv4->SetUp (interface);
}

void setupServer(Ptr<Node> node, InetSocketAddress add, int id, int num_nodes, double start, double stop, std::vector<InetSocketAddress> clients){
    Ptr<Server> appA = CreateObject<Server> (id, add, num_nodes);
    for(int i=0;i<clients.size();i++){
        appA->Setup (clients[i], i);
    }
    node->AddApplication (appA);
    appA->SetStartTime (Seconds (start));
    appA->SetStopTime (Seconds (stop));
}

void setupClient(Ptr<Node> node, InetSocketAddress add, int id, int num_nodes, double start, double stop, std::vector<InetSocketAddress> clients){
    int interval = 3;
    Ptr<Client> appA = CreateObject<Client> (id, add, num_nodes, interval);
    for(int i=0;i<clients.size();i++){
        appA->Setup (clients[i], i);
    }
    node->AddApplication (appA);
    appA->SetStartTime (Seconds (start));
    appA->SetStopTime (Seconds (stop));
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

    NS_LOG_INFO ("Create Node");
    Ptr<Node> bridge_node = CreateObject<Node> ();
    Ptr<Node> nodeA = CreateObject<Node> ();
    Ptr<Node> nodeB = CreateObject<Node> ();
    Ptr<Node> nodeC = CreateObject<Node> ();
    Ptr<Node> nodeD = CreateObject<Node> ();
    Ptr<Node> nodeE = CreateObject<Node> ();
    NodeContainer nodesA (bridge_node);
    NodeContainer nodesB (bridge_node);
    NodeContainer nodesC (bridge_node);
    NodeContainer nodesD (bridge_node);
    NodeContainer nodesE (bridge_node);
    NodeContainer nodes (bridge_node);
    nodesA.Add(nodeA);
    nodesB.Add(nodeB);
    nodesC.Add(nodeC);
    nodesD.Add(nodeD);
    nodesE.Add(nodeE);
    nodes.Add(nodeA);
    nodes.Add(nodeB);
    nodes.Add(nodeC);
    nodes.Add(nodeD);
    nodes.Add(nodeE);

    NS_LOG_INFO ("Create BridgeDevice");
    Ptr<NetCacheDevice> bridge = CreateObject<NetCacheDevice> (keys);
    // Ptr<BridgeNetDevice> bridge = CreateObject<BridgeNetDevice> ();
    bridge->SetAddress (Mac48Address::Allocate ());
    bridge_node->AddDevice (bridge);

    Time linkLatency(MicroSeconds (2));
    DataRate linkRate("100Gb/s");
    double ecnTh = 200000;

    SimpleNetDeviceHelper pointToPointSR;
    pointToPointSR.SetQueue("ns3::DevRedQueue", "MaxSize", StringValue("2666p"));
    pointToPointSR.SetQueue("ns3::DevRedQueue", "MinTh", DoubleValue (ecnTh));
    pointToPointSR.SetDeviceAttribute ("DataRate", DataRateValue(linkRate));
    pointToPointSR.SetChannelAttribute ("Delay", TimeValue (linkLatency));

    NetDeviceContainer ptpDevA = pointToPointSR.Install (nodesA);
    NetDeviceContainer ptpDevB = pointToPointSR.Install (nodesB);
    NetDeviceContainer ptpDevC = pointToPointSR.Install (nodesC);
    NetDeviceContainer ptpDevD = pointToPointSR.Install (nodesD);
    NetDeviceContainer ptpDevE = pointToPointSR.Install (nodesE);
    bridge->AddBridgePort (ptpDevA.Get(0));
    bridge->AddBridgePort (ptpDevB.Get(0));
    bridge->AddBridgePort (ptpDevC.Get(0));
    bridge->AddBridgePort (ptpDevD.Get(0));
    bridge->AddBridgePort (ptpDevE.Get(0));

    NS_LOG_INFO ("Add Internet Stack");
    InternetStackHelper internetStackHelper;
    internetStackHelper.Install (nodes);

    Ipv4Address bridgeIp ("10.0.0.2");
    Ipv4Address IpA ("10.0.0.3");
    Ipv4Address IpB ("10.0.0.4");
    Ipv4Address IpC ("10.0.0.5");
    Ipv4Address IpD ("10.0.0.6");
    Ipv4Address IpE ("10.0.0.7");
    Ipv4Mask hostMask ("255.255.255.0");

    NS_LOG_INFO ("Create IPv4 Interface");
    
    setupIp(bridge_node, bridge, bridgeIp);
    setupIp(nodeA, ptpDevA.Get(1), IpA);
    setupIp(nodeB, ptpDevB.Get(1), IpB);
    setupIp(nodeC, ptpDevC.Get(1), IpC);
    setupIp(nodeD, ptpDevD.Get(1), IpD);
    setupIp(nodeE, ptpDevE.Get(1), IpE);

    std::vector<InetSocketAddress> servers, clients;
    servers.push_back(InetSocketAddress (Ipv4Address ("10.0.0.3"), 12345));
    servers.push_back(InetSocketAddress (Ipv4Address ("10.0.0.4"), 12346));
    clients.push_back(InetSocketAddress (Ipv4Address ("10.0.0.5"), 12347));
    clients.push_back(InetSocketAddress (Ipv4Address ("10.0.0.6"), 12348));
    clients.push_back(InetSocketAddress (Ipv4Address ("10.0.0.7"), 12349));

    setupServer(nodeA, servers[0], 0, 2, 1, 10, clients);
    setupServer(nodeB, servers[1], 1, 2, 1, 10, clients);

    setupClient(nodeC, clients[0], 0, 2, 2, 3, servers);
    setupClient(nodeD, clients[1], 1, 2, 2, 3, servers);
    setupClient(nodeE, clients[2], 2, 2, 2, 3, servers);

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
