#include <iostream>
#include <fstream>
#include <random>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "kvstore.h"

using std::string;

namespace ns3{

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

Server::Server (bool pegasus, uint32_t server_id, InetSocketAddress bind, uint32_t num_nodes)
  : m_nPackets (1000000), 
    m_sendEvent (), 
    m_running (false),
    m_packetsSent (0),
    m_bind(bind),
    m_server_id(server_id),
    m_num_nodes(num_nodes)
{
    if(pegasus)
        codec = new WireCodec(true);
    else
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

Client::Client (bool pegasus, uint32_t client_id, InetSocketAddress bind, uint32_t num_nodes, int interval, double get_ratio)
  : m_total_latency (0), 
    m_nPackets (1000000), 
    m_issued (0), 
    m_completed (0), 
    m_sendEvent (), 
    m_running (false),
    m_get_ratio (get_ratio),
    m_put_ratio (1-get_ratio),
    m_packetsSent (0),
    m_client_id (client_id),
    m_num_nodes (num_nodes),
    m_bind (bind)
{
    if(pegasus)
        codec = new WireCodec(true);
    else
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
    std::ofstream fs;
    std::string fname = "/workspaces/simbricks/experiments/out/ns3.client"+std::to_string(this->m_client_id);
    fs.open(fname.c_str(), std::ofstream::out | std::ofstream::trunc);
    uint64_t count = 0;
    for (const auto &latency : m_lats) {
        count += latency.second;
        fs << latency.first << " " << count << "\n";
    }
    fs.close();
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
    m_total_latency += Simulator::Now().GetMicroSeconds() - kvm.reply.req_time;
    m_lats[(Simulator::Now().GetMicroSeconds() - kvm.reply.req_time)]++;
}
}