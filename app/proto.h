#ifndef PROTO_H_
#define PROTO_H_

#include "raft.h"
#include "contiki.h"
#include "net/rime/rime.h"

/** Structures borrowed from ticketd: https://github.com/willemt/ticketd */
typedef enum {
    HANDSHAKE_FAILURE,
    HANDSHAKE_SUCCESS,
} handshake_state_e; 

/** Message types used for peer to peer traffic
 * These values are used to identify message types during deserialization */
typedef enum
{
    /** Handshake is a special non-raft message type
     * We send a handshake so that we can identify ourselves to our peers */
    MSG_HANDSHAKE,
    /** Successful responses mean we can start the Raft periodic callback */
    MSG_HANDSHAKE_RESPONSE,
    /** Tell leader we want to leave the cluster */
    /* When instance is ctrl-c'd we have to gracefuly disconnect */
    MSG_LEAVE,
    /* Receiving a leave response means we can shutdown */
    MSG_LEAVE_RESPONSE,
    MSG_REQUESTVOTE,
    MSG_REQUESTVOTE_RESPONSE,
    MSG_APPENDENTRIES,
    MSG_APPENDENTRIES_RESPONSE,
    MSG_CLIENT,
    MSG_CLIENT_RESPONSE
} peer_message_type_e;

typedef struct {
    // client message 
    unsigned char buf[2];
} client_message_t;

const char * get_peer_message_type(peer_message_type_e pmte)
{
    switch (pmte)
    {
    case MSG_HANDSHAKE:
        return "MSG_HANDSHAKE";
        break;
    case MSG_HANDSHAKE_RESPONSE:
        return "MSG_HANDSHAKE_RESPONSE";
        break;
    case MSG_LEAVE:
        return "MSG_LEAVE";
        break;
    case MSG_LEAVE_RESPONSE:
        return "MSG_LEAVE_RESPONSE";
        break;
    case MSG_REQUESTVOTE:
        return "MSG_REQUESTVOTE";
        break;
    case MSG_REQUESTVOTE_RESPONSE:
        return "MSG_REQUESTVOTE_RESPONSE";
        break;
    case MSG_APPENDENTRIES:
        return "MSG_APPENDENTRIES";
        break;
    case MSG_APPENDENTRIES_RESPONSE:
        return "MSG_APPENDENTRIES_RESPONSE";
        break;
    case MSG_CLIENT:
        return "MSG_CLIENT";
        break;
    case MSG_CLIENT_RESPONSE:
        return "MSG_CLIENT_RESPONSE";
        break;
    default:
        return "UNKNOWN";
    }
}

/** Peer protocol handshake
 * Send handshake after connecting so that our peer can identify us 
 * linkaddr_t can be derived from node_id */
typedef struct
{
    unsigned short node_id;
} msg_handshake_t;

typedef struct
{
    unsigned short success;

    /* my Raft node ID.
     * Sometimes we don't know who we did the handshake with */
    unsigned short node_id;

    unsigned short leader_id;
} msg_handshake_response_t;

/** Add/remove Raft peer */
typedef struct
{
    unsigned short node_id;
} entry_cfg_change_t;

typedef struct
{
    peer_message_type_e type;
    union
    {
        msg_handshake_t hs;
        msg_handshake_response_t hsr;
        msg_requestvote_t rv;
        msg_requestvote_response_t rvr;
        msg_appendentries_t ae;
        msg_appendentries_response_t aer;
    };
    // int padding[100];
} msg_t;

typedef enum
{
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
} conn_status_e;

typedef enum {
    CFG_CHANGE
} fsm_data_e;


typedef struct
{
    /* the server's node ID */
    unsigned short node_id;

    // raft_server_t* raft;

    /* Link list of peer connections */
    // peer_connection_t* conns;
} server_t;

#endif /* PROTO_H_ */