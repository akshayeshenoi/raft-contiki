#include <stdio.h> /* For printf() */
#include <string.h>
#include <stdarg.h>
#include <assert.h>

#include "contiki.h"
#include "lib/random.h"
#include "net/rime/rime.h"
#include "sys/node-id.h"

#include "raft.h"

#include "proto.h"

static struct netflood_conn netflood;
static int netflood_seq;
static server_t server;
static server_t *sv = &server;

static raft_server_t *raft_server;

static unsigned char stage_buffer[PACKETBUF_SIZE];

#define HEADER_SIZE 4 // only receiver and sender ID for now
#define PAYLOAD_SIZE (PACKETBUF_SIZE - HEADER_SIZE)
#define ELECTION_TIMEOUT CLOCK_SECOND * 50
#define REQUEST_TIMEOUT CLOCK_SECOND * 2

/*---------------------------------------------------------------------------*/
// logging
typedef enum {
    TRACE,
    DEBUG,
    INFO,
    ERR
} LOG_LEVEL_E;

#define LOG_LEVEL_CONF DEBUG

void __printf(LOG_LEVEL_E log_level, const char *format, ...)
{   // log_level = info;
    if (!(log_level >= LOG_LEVEL_CONF)) 
        return;

    va_list args;
    va_start (args, format);
    vprintf (format, args);
    va_end (args);
}

#define PRINT_FN_DBG() __printf(TRACE, "\n===== Entering %s() =====\n", __func__);

/*---------------------------------------------------------------------------*/
// networking api and callbacks
void send_data(void *data, size_t len, unsigned short nodeid)
{
    PRINT_FN_DBG();

    // make space for sender and receiver node ID
    memmove(((unsigned char*)data) + 2 * sizeof(unsigned short), data, len);
    memcpy(data, &nodeid, sizeof(unsigned short)); // receiver id
    memcpy(((unsigned char*)data) + sizeof(unsigned short), &sv->node_id, sizeof(unsigned short)); // sender id
    len += 2 * sizeof(unsigned short);

    packetbuf_copyfrom(data, len);
    netflood_send(&netflood, netflood_seq++);
}

static void sent(struct netflood_conn *c)
{
    PRINT_FN_DBG()
}

static void timedout(struct netflood_conn *c)
{
    PRINT_FN_DBG()
}

// forward refs
static void __handle_msg_appendentries(unsigned char* buf_offset, int buf_len, unsigned short sender_nodeid);
static void __handle_msg_appendentries_response(unsigned char* buf_offset, int buf_len, unsigned short sender_nodeid);
static void __handle_msg_requestvote(unsigned char* buf_offset, int buf_len, unsigned short sender_nodeid);
static void __handle_msg_requestvote_response(unsigned char* buf_offset, int buf_len, unsigned short sender_nodeid);
static void __handle_msg_client(unsigned char* buf_offset, int buf_len, unsigned short sender_nodeid);
static void __handle_msg_client_response(unsigned char* buf_offset, int buf_len, unsigned short sender_nodeid);

static int recv(struct netflood_conn *c, const linkaddr_t *from,
	       const linkaddr_t *originator, uint8_t seqno, uint8_t hops)
{
    unsigned char *buf_offset = packetbuf_dataptr();
    int buf_len = packetbuf_datalen();

    // ensure it's addressed to us
    unsigned short intended_recvr; 
    memcpy(&intended_recvr, buf_offset, sizeof(unsigned short));
    if (sv->node_id != intended_recvr) return 1;

    buf_offset += sizeof(unsigned short);

    // get sender id
    unsigned short sender_nodeid; 
    memcpy(&sender_nodeid, buf_offset, sizeof(unsigned short));

    buf_offset += sizeof(unsigned short);

    // get message type from the first byte
    peer_message_type_e msgtype = (peer_message_type_e)*buf_offset;

    switch (msgtype) {
        case MSG_HANDSHAKE:
            // TODO dynamic later
            break;
        case MSG_HANDSHAKE_RESPONSE:
            // TODO dynamic later
            break;
        case MSG_APPENDENTRIES:
            __printf(DEBUG, "MSG_APPENDENTRIES received from %d\n", sender_nodeid);
            __handle_msg_appendentries(buf_offset, buf_len, sender_nodeid);
            break;
        case MSG_APPENDENTRIES_RESPONSE:
            __printf(DEBUG, "MSG_APPENDENTRIES_RESPONSE received from %d\n", sender_nodeid);
            __handle_msg_appendentries_response(buf_offset, buf_len, sender_nodeid);
            break;
        case MSG_REQUESTVOTE:
            __printf(DEBUG, "MSG_REQUESTVOTE received from %d\n", sender_nodeid);
            __handle_msg_requestvote(buf_offset, buf_len, sender_nodeid);
            break;
        case MSG_REQUESTVOTE_RESPONSE:
            __printf(DEBUG, "MSG_REQUESTVOTE_RESPONSE received from %d\n", sender_nodeid);
            __handle_msg_requestvote_response(buf_offset, buf_len, sender_nodeid);
            break;
        case MSG_LEAVE:
            break;
        case MSG_LEAVE_RESPONSE:
            break;
        case MSG_CLIENT:
            __printf(DEBUG, "MSG_CLIENT received from %d\n", sender_nodeid);
            __handle_msg_client(buf_offset, buf_len, sender_nodeid);
            break;
        case MSG_CLIENT_RESPONSE:
            __printf(DEBUG, "MSG_CLIENT_RESPONSE received from %d\n", sender_nodeid);
            __handle_msg_client_response(buf_offset, buf_len, sender_nodeid);
            break;
    }
    return 1;
}

const static struct netflood_callbacks callbacks = {
    recv, 
    sent, 
    timedout};

/*---------------------------------------------------------------------------*/
// raft message handlers

static void __handle_msg_appendentries(unsigned char* buf_offset, int buf_len, unsigned short sender_nodeid)
{
    PRINT_FN_DBG();
    raft_node_t* node = raft_get_node(raft_server, sender_nodeid);
    // skip type byte first
    buf_offset += 1;

    msg_appendentries_t msg_ae = {};
    // each message might be comprised of multiple "entries"
    // 1. copy just the message metadata first (see struct)
    memcpy(&msg_ae, buf_offset, sizeof(msg_appendentries_t) - sizeof(msg_entry_t*));
    buf_offset += sizeof(msg_appendentries_t) - sizeof(msg_entry_t*);

    // 2. copy entries one by one
    // allocate a temporary buffer on the stack to save entries
    unsigned char msg_ae_entries[PAYLOAD_SIZE];
    // point it to msg.entries
    msg_ae.entries = (void*)msg_ae_entries;

    int i = 0;
    for (i = 0; i < msg_ae.n_entries; i++)
    {
        memcpy(&msg_ae.entries[i], buf_offset, sizeof(raft_entry_t));
        buf_offset += sizeof(raft_entry_t);
    }

    msg_t msg_response = {};
    msg_response.type = MSG_APPENDENTRIES_RESPONSE;

    int e = raft_recv_appendentries(raft_server, node, &msg_ae, &msg_response.aer);
    assert(e == 0);

    // send response
    // marshal appendentries response message
    memset(stage_buffer, 0, PACKETBUF_SIZE);
    unsigned char *offset = stage_buffer;
    // copy type first (need just one byte)
    *offset = 0xff & MSG_APPENDENTRIES_RESPONSE;
    offset += 1;

    memcpy(offset, &msg_response.aer, sizeof(msg_appendentries_response_t));
    offset += sizeof(msg_appendentries_response_t); 

    send_data(stage_buffer, offset - stage_buffer, sender_nodeid);
}

static void __handle_msg_appendentries_response(unsigned char* buf_offset, int buf_len, unsigned short sender_nodeid)
{
    PRINT_FN_DBG();
    raft_node_t* node = raft_get_node(raft_server, sender_nodeid);
    // skip type byte first
    buf_offset += 1;

    msg_appendentries_response_t msg_aer = {};
    memcpy(&msg_aer, buf_offset, sizeof(msg_appendentries_response_t));

    int e = raft_recv_appendentries_response(raft_server, node, &msg_aer);
    assert(e == 0);
}

static void __handle_msg_requestvote(unsigned char* buf_offset, int buf_len, unsigned short sender_nodeid)
{
    PRINT_FN_DBG();
    raft_node_t* node = raft_get_node(raft_server, sender_nodeid);
    // skip type byte first
    buf_offset += 1;

    // deserialize
    msg_requestvote_t msg_rv = {};
    memcpy(&msg_rv, buf_offset, sizeof(msg_requestvote_t));

    msg_t msg_response = {};
    msg_response.type = MSG_REQUESTVOTE_RESPONSE;

    int e = raft_recv_requestvote(raft_server, node, &msg_rv, &msg_response.rvr);
    assert(e == 0);

    // send response
    // marshal requestvote response message
    memset(stage_buffer, 0, PACKETBUF_SIZE);
    unsigned char *offset = stage_buffer;
    // copy type first (need just one byte)
    *offset = 0xff & MSG_REQUESTVOTE_RESPONSE;
    offset += 1;

    memcpy(offset, &msg_response.rvr, sizeof(msg_requestvote_response_t));
    offset += sizeof(msg_requestvote_response_t); 

    send_data(stage_buffer, offset - stage_buffer, sender_nodeid);
}

static void __handle_msg_requestvote_response(unsigned char* buf_offset, int buf_len, unsigned short sender_nodeid)
{
    PRINT_FN_DBG();
    raft_node_t* node = raft_get_node(raft_server, sender_nodeid);
    // skip type byte first
    buf_offset += 1;

    msg_requestvote_response_t msg_rvr = {};
    memcpy(&msg_rvr, buf_offset, sizeof(msg_requestvote_response_t));

    int e = raft_recv_requestvote_response(raft_server, node, &msg_rvr);
    assert(e == 0);
}

static void __handle_msg_client(unsigned char* buf_offset, int buf_len, unsigned short sender_nodeid)
{
    PRINT_FN_DBG();
    // raft_node_t* node = raft_get_node(raft_server, sender_nodeid);
    // skip type byte first
    buf_offset += 1;

    // copy message into entry struct
    msg_entry_t entry = {};
    entry.id = (unsigned int)random_rand();
    // TODO ensure this is same size as entry.data.buf
    memcpy(entry.data.buf, buf_offset, sizeof(client_message_t)); 

    // submit to raft library
    msg_entry_response_t entry_response = {};
    raft_recv_entry(raft_server, &entry, &entry_response);

    // aynchronously respond to client
    // add to some global queue perhaps, and check for success after each
    // raft_periodic

}

static void __handle_msg_client_response(unsigned char* buf_offset, int buf_len, unsigned short recv_nodeid)
{

}

/*---------------------------------------------------------------------------*/
// raft callbacks

/** Raft callback for sending request vote message */
static int __raft_send_requestvote(
    raft_server_t* raft,
    void *user_data,
    raft_node_t *node,
    msg_requestvote_t* m
    )
{
    PRINT_FN_DBG();
    // marshall the message
    memset(stage_buffer, 0, PACKETBUF_SIZE);
    unsigned char *offset = stage_buffer;
    // copy type first (need just one byte)
    *offset = 0xff & MSG_REQUESTVOTE;
    offset += 1;
    // copy message next
    memcpy(offset, m, sizeof(msg_requestvote_t));
    offset += sizeof(msg_requestvote_t);

    unsigned short this_node_id = (unsigned short)raft_node_get_id(node);

    send_data(stage_buffer, offset - stage_buffer, this_node_id);

    return 0;
}

/** Raft callback for sending appendentries to a node */
static int __raft_send_appendentries(
    raft_server_t* raft,
    void *user_data,
    raft_node_t *node,
    msg_appendentries_t* m
    )
{
    PRINT_FN_DBG();
    // marshal appendentries message
    memset(stage_buffer, 0, PACKETBUF_SIZE);
    unsigned char *offset = stage_buffer;
    // copy type first (need just one byte)
    *offset = 0xff & MSG_APPENDENTRIES;
    offset += 1;
    // copy appendentry message
    // each message might be comprised of multiple "entries"
    // 1. copy just the message metadata first (see struct)
    memcpy(offset, m, sizeof(msg_appendentries_t));
    // ignore the last `msg_entry_t *` pointer, since we need to copy actual message there
    offset += sizeof(msg_appendentries_t) - sizeof(msg_entry_t*);

    // 2. copy entries one by one
    raft_entry_t *ety = m->entries; 
    int i = 0;
    for (i = 0; i < m->n_entries; i++)
    {
        memcpy(offset, ety, sizeof(raft_entry_t));
        // note that the first member of raft_entry_t is a pointer to the next element
        // just overwrite to 0
        memset(offset, 0, sizeof(raft_entry_t*));

        offset += sizeof(raft_entry_t);

        ety = raft_get_next_log_entry(raft, ety);

        if (ety == NULL)
            // reached end, break
            break;

        // ensure we don't overshoot PACKETBUF_SIZE in the next iteration
        if ((offset + sizeof(raft_entry_t)) - stage_buffer >= PACKETBUF_SIZE)
            break; // TODO hopefully no sideeffects
    }

    unsigned short this_node_id = (unsigned short)raft_node_get_id(node);

    send_data(stage_buffer, offset - stage_buffer, this_node_id);

    return 0;
}

/** Raft callback for applying an entry to the finite state machine */
static int __raft_applylog(
    raft_server_t* raft,
    void *user_data,
    raft_entry_t *entry,
    int entry_idx
    )
{
    PRINT_FN_DBG();
    return -1;
}

/** Raft callback for saving voted_for field to disk.
 * This only returns when change has been made to disk.
 * TODO how do we manage this? */
static int __raft_persist_vote(
    raft_server_t* raft,
    void *udata,
    const int voted_for
    )
{
    PRINT_FN_DBG();
    return 0;
}

/** Raft callback for saving term field to disk.
 * This only returns when change has been made to disk.
 * TODO how do we manage this? */
static int __raft_persist_term(
    raft_server_t* raft,
    void *user_data,
    int term,
    int vote
    )
{
    PRINT_FN_DBG();
    return 0;
}

/** Raft callback for appending an item to the log */
static int __raft_logentry_offer(
    raft_server_t* raft,
    void *udata,
    raft_entry_t *ety,
    int ety_idx
    )
{
    PRINT_FN_DBG();
    // no flash storage to add log to
    return 0;
}

/** Raft callback for removing the first entry from the log
 * @note this is provided to support log compaction in the future 
 * @note skip this */
static int __raft_logentry_poll(
    raft_server_t* raft,
    void *udata,
    raft_entry_t *entry,
    int ety_idx
    )
{ return -1; }

/** Raft callback for deleting the most recent entry from the log.
 * This happens when an invalid leader finds a valid leader and has to delete
 * superseded log entries. */
static int __raft_logentry_pop(
    raft_server_t* raft,
    void *udata,
    raft_entry_t *entry,
    int ety_idx
    )
{
    PRINT_FN_DBG();
    return -1;
}

/** Raft callback for getting node_id from config change ety */
static int __raft_log_get_node_id(
    raft_server_t* raft,
    void *user_data,
    raft_entry_t *entry,
    int entry_idx
    )
{
    // we know that ety is config change related entry for sure
    // node id is in the buffer itself
    return (entry->data.buf[0] & 0xff) + (entry->data.buf[1] >> 8 & 0xff);
}

/** Non-voting node now has enough logs to be able to vote.
 * Append a finalization cfg log entry. */
static int __raft_node_has_sufficient_logs(
    raft_server_t* raft,
    void *user_data,
    raft_node_t* node
    )
{
    PRINT_FN_DBG();
    return -1;
}

/** Raft callback for displaying debugging information */
void __raft_debug(
    raft_server_t* raft,
    raft_node_t* node,
    void *user_data,
    const char *buf
    )
{
    __printf(DEBUG, buf);
}

raft_cbs_t raft_funcs = {
    .send_requestvote            = __raft_send_requestvote,
    .send_appendentries          = __raft_send_appendentries,
    .applylog                    = __raft_applylog,
    .persist_vote                = __raft_persist_vote,
    .persist_term                = __raft_persist_term,
    .log_offer                   = __raft_logentry_offer,
    .log_poll                    = __raft_logentry_poll,
    .log_pop                     = __raft_logentry_pop,
    .log_get_node_id             = __raft_log_get_node_id,
    .node_has_sufficient_logs    = __raft_node_has_sufficient_logs,
    .log                         = __raft_debug,
};


/*---------------------------------------------------------------------------*/
// Contiki Process Declarations
PROCESS(main_process, "Main Process");
PROCESS(raft_periodic_process, "Raft Periodic Process");
PROCESS(client_process, "Client Process");
AUTOSTART_PROCESSES(&main_process);
/*---------------------------------------------------------------------------*/

void client_new_message(raft_server_t *raft_server)
{
    client_message_t msg = {};
    *(msg.buf) = 0x0001; // 1

    unsigned short leader_node_id = raft_get_current_leader(raft_server);
    if (leader_node_id == -1) {
        // possibly electing new leader at the moment
        return;
    }

    // begin serialize
    memset(stage_buffer, 0, PACKETBUF_SIZE);
    unsigned char *offset = stage_buffer;
    // copy type first (need just one byte)
    *offset = 0xff & MSG_CLIENT;
    offset += 1;

    memcpy(offset, &msg, sizeof(client_message_t));
    offset += sizeof(client_message_t);

    __printf(DEBUG, "Client sending msg to leader: %d\n", leader_node_id);
    send_data(stage_buffer, offset - stage_buffer, leader_node_id);
}

// client thread
static struct etimer et_client;
PROCESS_THREAD(client_process, ev, data)
{
  PROCESS_BEGIN();

  while(1) {
    etimer_set(&et_client, CLOCK_SECOND * 10);

    PROCESS_WAIT_EVENT_UNTIL(ev == PROCESS_EVENT_TIMER);

    // send message to leader
    // TODO create new client object, breakout into new file
    client_new_message(raft_server); 
  }

  PROCESS_END();
}

/*---------------------------------------------------------------------------*/
// Raft periodic ticker
static struct etimer et_periodic;
PROCESS_THREAD(raft_periodic_process, ev, data)
{
  PROCESS_BEGIN();

  while(1) {
    etimer_set(&et_periodic, CLOCK_SECOND / 5);

    PROCESS_WAIT_EVENT_UNTIL(ev == PROCESS_EVENT_TIMER);

    raft_periodic(raft_server, CLOCK_SECOND / 5); // 100ms
  }

  PROCESS_END();
}

/*---------------------------------------------------------------------------*/
// Main process
PROCESS_THREAD(main_process, ev, data)
{
    PROCESS_EXITHANDLER(netflood_close(&netflood);)
    PROCESS_BEGIN();

    memset(sv, 0, sizeof(server_t));
    sv->node_id = node_id; // see node-id.h

    random_init(node_id);

    raft_server = raft_new();
    raft_set_callbacks(raft_server, &raft_funcs, NULL);

    raft_set_election_timeout(raft_server, ELECTION_TIMEOUT);
    raft_set_request_timeout(raft_server, REQUEST_TIMEOUT);

    // add self
    raft_add_node(raft_server, sv->node_id, 1);

    // add other nodes
    // TODO this is static for now
    unsigned short i;
    for (i = 1; i <= 5; i++)
    {
        if (i == sv->node_id)
            continue; // don't add self

        raft_add_node(raft_server, i, 0);
    }

    netflood_seq = random_rand();
    netflood_open(&netflood, CLOCK_SECOND * 2, 132, &callbacks);

    // start periodic_raft
    process_start(&raft_periodic_process, NULL);

    if (sv->node_id == 1){
        // start one client on one node
        process_start(&client_process, NULL);
    }

    PROCESS_END();
}
/*---------------------------------------------------------------------------*/
