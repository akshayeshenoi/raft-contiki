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
} peer_message_type_e;

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

typedef struct peer_connection_s peer_connection_t;

// MIGHT NOT NEED THIS
struct peer_connection_s
{
    /* peer's address */
    linkaddr_t addr;

    /* tell if we need to connect or not */
    conn_status_e connection_status;

    /* peer's raft node_idx */
    raft_node_t* node;

    /* number of entries currently expected.
     * this counts down as we consume entries */
    int n_expected_entries;

    /* remember most recent append entries msg, we refer to this msg when we
     * finish reading the log entries.
     * used in tandem with n_expected_entries */
    msg_t ae;
    peer_connection_t *next;
};

typedef struct
{
    /* the server's node ID */
    unsigned short node_id;

    raft_server_t* raft;

    /* Link list of peer connections */
    peer_connection_t* conns;
} server_t;

