#include <iostream>
#include <random>
#include <fstream>
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-apps-module.h"

using std::string;
#define BASE_VERSION 1
#define N_VIRTUAL_NODES 16
#define KEYHASH_MASK 0x7FFFFFFF
#define KEYHASH_RANGE 0x80000000

namespace ns3{

class MSG {
public:
    MSG();
    MSG(void *buf, size_t len, bool dealloc);
    MSG(const std::string &str);
    ~MSG();

    const void *buf() const;
    size_t len() const;
    void set_MSG(void *buf, size_t len, bool dealloc);

private:
    void *buf_;
    size_t len_;
    bool dealloc_;
};

inline uint32_t compute_keyhash(const std::string &key)
{
    uint64_t hash = 5381;
    for (auto c : key) {
        hash = ((hash << 5) + hash) + (uint64_t)c;
    }
    return (uint32_t)(hash & KEYHASH_MASK);
}

inline int key_to_node_id(const std::string &key, int num_nodes)
{
    uint32_t keyhash = compute_keyhash(key);
    uint32_t interval = (uint32_t)KEYHASH_RANGE / (num_nodes * N_VIRTUAL_NODES);
    return (int)((keyhash / interval) % num_nodes);
}

inline void convert_endian(void *dst, const void *src, size_t size)
{
    uint8_t *dptr, *sptr;
    for (dptr = (uint8_t*)dst, sptr = (uint8_t*)src + size - 1;
         size > 0;
         size--) {
        *dptr++ = *sptr--;
    }
}

typedef uint32_t keyhash_t;
typedef uint16_t load_t;
typedef uint32_t ver_t;

/*
 * KV MSGs
 */
enum class OpType {
    GET,
    PUT,
    DEL,
    PUTFWD,
};
struct Operation {
    Operation()
        : op_type(OpType::GET), keyhash(0), ver(0), key(""), value("") {};

    OpType op_type;
    keyhash_t keyhash;
    ver_t ver;

    std::string key;
    std::string value;
};

struct MemcacheKVRequest {
    MemcacheKVRequest()
        : client_id(0), server_id(0), req_id(0), req_time(0) {};

    int client_id;
    int server_id;
    uint32_t req_id;
    uint32_t req_time;
    Operation op;
};

enum class Result {
    OK,
    NOT_FOUND
};

struct MemcacheKVReply {
    MemcacheKVReply()
        : client_id(0), server_id(0), req_id(0), req_time(0),
        op_type(OpType::GET), keyhash(0), ver(0), key(""), value(""),
        result(Result::OK), load(0) {};

    int client_id;
    int server_id;
    uint32_t req_id;
    uint32_t req_time;

    OpType op_type;
    keyhash_t keyhash;
    ver_t ver;
    std::string key;
    std::string value;

    Result result;
    load_t load;
};

struct ReplicationRequest {
    keyhash_t keyhash;
    ver_t ver;

    std::string key;
    std::string value;
};

struct ReplicationAck {
    int server_id;

    keyhash_t keyhash;
    ver_t ver;
};

struct MemcacheKVMSG {
    enum class Type {
        REQUEST,
        REPLY,
        RC_REQ,
        RC_ACK,
        UNKNOWN
    };
    MemcacheKVMSG()
        : type(Type::UNKNOWN) {};

    Type type;
    MemcacheKVRequest request;
    MemcacheKVReply reply;
    ReplicationRequest rc_request;
    ReplicationAck rc_ack;
};

class MSGCodec {
public:
    virtual ~MSGCodec() {};

    virtual bool decode(const MSG &in, MemcacheKVMSG &out) = 0;
    virtual bool encode(MSG &out, const MemcacheKVMSG &in) = 0;
};

class WireCodec : public MSGCodec {
public:
    WireCodec()
        : proto_enable(false) {};
    WireCodec(bool proto_enable)
        : proto_enable(proto_enable) {};
    ~WireCodec() {};

    virtual bool decode(const MSG &in, MemcacheKVMSG &out) override final;
    virtual bool encode(MSG &out, const MemcacheKVMSG &in) override final;

private:
    bool proto_enable;
    /* Wire format:
     * Header:
     * identifier (16) + op_type (8) + key_hash (32) + client_id (8) + server_id
     * (8) + load (16) + version (32) + bitmap (32) + hdr_req_id (8) + MSG payload
     *
     * MSG payload:
     * Request:
     * req_id (32) + req_time (32) + op_type (8) + key_len (16) + key (+
     * value_len(16) + value)
     *
     * Reply:
     * req_id (32) + req_time (32) + op_type (8) + result (8) + value_len(16) +
     * value
     *
     * Replication request:
     * key_len (16) + key + value_len (16) + value
     *
     * Replication ack:
     * empty
     */
    typedef uint16_t identifier_t;
    typedef uint8_t op_type_t;
    typedef uint32_t keyhash_t;
    typedef uint8_t node_t;
    typedef uint16_t load_t;
    typedef uint32_t ver_t;
    typedef uint32_t bitmap_t;
    typedef uint8_t hdr_req_id_t;
    typedef uint32_t req_id_t;
    typedef uint32_t req_time_t;
    typedef uint16_t key_len_t;
    typedef uint8_t result_t;
    typedef uint16_t value_len_t;
    typedef uint16_t sa_family_t;

    static constexpr identifier_t PEGASUS = 0x4750;
    static constexpr identifier_t STATIC = 0x1573;
    static constexpr op_type_t OP_GET       = 0x0;
    static constexpr op_type_t OP_PUT       = 0x1;
    static constexpr op_type_t OP_DEL       = 0x2;
    static constexpr op_type_t OP_REP_R     = 0x3;
    static constexpr op_type_t OP_REP_W     = 0x4;
    static constexpr op_type_t OP_RC_REQ    = 0x5;
    static constexpr op_type_t OP_RC_ACK    = 0x6;
    static constexpr op_type_t OP_PUT_FWD   = 0x7;

    static constexpr size_t PACKET_BASE_SIZE = sizeof(identifier_t) + sizeof(op_type_t) + sizeof(keyhash_t) + sizeof(node_t) + sizeof(node_t) + sizeof(load_t) + sizeof(ver_t) + sizeof(bitmap_t) + sizeof(hdr_req_id_t);
    static constexpr size_t REQUEST_BASE_SIZE = PACKET_BASE_SIZE + sizeof(req_id_t) + sizeof(req_time_t) + sizeof(op_type_t) + sizeof(key_len_t);
    static constexpr size_t REPLY_BASE_SIZE = PACKET_BASE_SIZE + sizeof(req_id_t) + sizeof(req_time_t) + sizeof(op_type_t) + sizeof(result_t) + sizeof(value_len_t);
    static constexpr size_t RC_REQ_BASE_SIZE = PACKET_BASE_SIZE + sizeof(key_len_t) + sizeof(value_len_t);
    static constexpr size_t RC_ACK_BASE_SIZE = PACKET_BASE_SIZE;
};

/*
 * Netcache codec
 */
class NetcacheCodec : public MSGCodec {
public:
    NetcacheCodec() {};
    ~NetcacheCodec() {};

    virtual bool decode(const MSG &in, MemcacheKVMSG &out) override final;
    virtual bool encode(MSG &out, const MemcacheKVMSG &in) override final;

private:
    /* Wire format:
     * Header:
     * identifier (16) + op_type (8) + key (48) + value (32) + MSG payload
     *
     * MSG payload:
     * Request:
     * client_id (32) + req_id (32) + req_time (32) + op_type (8) + key_len (16)
     * + key (+ value_len(16) + value)
     *
     * Reply:
     * server_id (8) + client_id (32) + req_id (32) + req_time (32) + op_type
     * (8) + result (8) + value_len(16) + value
     */
    typedef uint16_t identifier_t;
    typedef uint8_t op_type_t;
    typedef uint8_t server_id_t;
    typedef uint32_t client_id_t;
    typedef uint32_t req_id_t;
    typedef uint32_t req_time_t;
    typedef uint16_t key_len_t;
    typedef uint8_t result_t;
    typedef uint16_t value_len_t;
    static constexpr size_t KEY_SIZE        = 6;
    static constexpr size_t VALUE_SIZE      = 4;
    static constexpr server_id_t SWITCH_ID  = 0xFF;

    static constexpr identifier_t NETCACHE  = 0x5039;
    static constexpr op_type_t OP_READ      = 0x1;
    static constexpr op_type_t OP_WRITE     = 0x2;
    static constexpr op_type_t OP_REP_R     = 0x3;
    static constexpr op_type_t OP_REP_W     = 0x4;
    static constexpr op_type_t OP_CACHE_HIT = 0x5;

    static constexpr size_t PACKET_BASE_SIZE = sizeof(identifier_t) + sizeof(op_type_t) + KEY_SIZE + VALUE_SIZE;
    static constexpr size_t REQUEST_BASE_SIZE = PACKET_BASE_SIZE + sizeof(client_id_t) + sizeof(req_id_t) + sizeof(req_time_t) + sizeof(op_type_t) + sizeof(key_len_t);
    static constexpr size_t REPLY_BASE_SIZE = PACKET_BASE_SIZE + sizeof(client_id_t) + sizeof(req_id_t) + sizeof(req_time_t) + sizeof(op_type_t) + sizeof(result_t) + sizeof(value_len_t);
};

constexpr size_t NetcacheCodec::KEY_SIZE;
constexpr size_t NetcacheCodec::VALUE_SIZE;

class Client : public Application 
{
public:

    Client (bool pegasus, uint32_t client_id, InetSocketAddress bind, uint32_t num_nodes, int interval, double get_ratio);
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
    uint64_t          m_total_latency;
    MemcacheKVMSG   m_kvmsg;
    std::vector<float>          zipfs;
    std::vector<std::string>    keys;
    float           alpha;
    float           m_get_ratio;
    float           m_put_ratio;
    std::map<uint32_t, uint64_t> m_lats;
    std::uniform_real_distribution<float> m_unif_real_dist;
    std::poisson_distribution<long> m_poisson_dist;
    std::default_random_engine m_generator;
    InetSocketAddress m_bind;
    MSGCodec        *codec;

};

class Server : public Application 
{
public:

    Server (bool pegasus, uint32_t server_id, InetSocketAddress bind, uint32_t num_nodes);
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

inline void setupServer(bool pegasus, Ptr<Node> node, InetSocketAddress add, int id, int num_nodes, double start, double stop, std::vector<InetSocketAddress> clients){
    Ptr<Server> appA = CreateObject<Server> (pegasus, id, add, num_nodes);
    for(int i=0;i<clients.size();i++){
        appA->Setup (clients[i], i);
    }
    node->AddApplication (appA);
    appA->SetStartTime (Seconds (start));
    appA->SetStopTime (Seconds (stop));
}

inline Ptr<Application> setupClient(bool pegasus, int interval, double get_ratio, Ptr<Node> node, InetSocketAddress add, int id, int num_nodes, double start, double stop, std::vector<InetSocketAddress> clients){
    Ptr<Client> appA = CreateObject<Client> (pegasus, id, add, num_nodes, interval, get_ratio);
    for(int i=0;i<clients.size();i++){
        appA->Setup (clients[i], i);
    }
    node->AddApplication (appA);
    appA->SetStartTime (Seconds (start));
    appA->SetStopTime (Seconds (stop));
    return appA;
}

}