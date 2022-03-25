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

static struct mesh_conn mesh;
static server_t server;
static server_t *sv = &server;

static raft_server_t *raft_server;

static unsigned char stage_buffer[PACKETBUF_SIZE];

/*---------------------------------------------------------------------------*/
// logging
typedef enum {
    TRACE,
    DEBUG,
    INFO,
    ERR
} LOG_LEVEL_E;

#define LOG_LEVEL_CONF TRACE

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
// networking callbacks
static void recv(struct mesh_conn *c, const linkaddr_t *from, uint8_t hops)
{
    printf("Data received from %d.%d: %.*s (%d)\n",
           from->u8[0], from->u8[1],
           packetbuf_datalen(), (char *)packetbuf_dataptr(), packetbuf_datalen());
}

static void sent(struct mesh_conn *c)
{
    PRINT_FN_DBG()
}

static void timedout(struct mesh_conn *c)
{
    PRINT_FN_DBG()
}

const static struct mesh_callbacks callbacks = {recv, sent, timedout};

void send_data(void *data, size_t len, linkaddr_t *addr)
{
    PRINT_FN_DBG();
    packetbuf_copyfrom(data, len);
    mesh_send(&mesh, addr); // non-blocking
}

/*---------------------------------------------------------------------------*/
// helper functions

/** Adds node membership related log entries */
static int __append_cfg_change(
    raft_server_t * raft_server,
    raft_logtype_e changetype, 
    unsigned short nodeid
    )
{
    PRINT_FN_DBG();

    msg_entry_t entry;
    entry.id = random_rand();
    entry.data.buf[0] = nodeid & 0xff; // first byte (LSB)
    entry.data.buf[1] = nodeid >> 8 & 0xff; // second byte (MSB) 
    entry.type = changetype;

    msg_entry_response_t r;
    int e = raft_recv_entry(raft_server, &entry, &r);
    return e;
}

/** Make membership related changes */
static int __offer_cfg_change(
    raft_server_t * raft_server,
    raft_logtype_e changetype, 
    unsigned short nodeid
    )
{
    PRINT_FN_DBG();
    // entry_cfg_change_t *change = (void*)data;
    // peer_connection_t* conn = __find_connection(sv, change->host, change->raft_port);

    /* Node is being removed */
    if (changetype == RAFT_LOGTYPE_REMOVE_NODE)
    {
        raft_remove_node(raft_server, raft_get_node(raft_server, node_id));
        // if (conn)
        //     conn->node = NULL;
        /* __delete_connection(sv, conn); */
        return 0;
    }

    /* Node is being added */
    // if (!conn)
    // {
    //     conn = __new_connection(sv);
    // }

    int is_self = nodeid == sv->node_id;

    switch (changetype)
    {
        case RAFT_LOGTYPE_ADD_NONVOTING_NODE:
            raft_add_non_voting_node(raft_server, node_id, is_self);
            break;
        case RAFT_LOGTYPE_ADD_NODE:
            raft_add_node(raft_server, node_id, is_self);
            break;
        default:
            assert(0);
    }

    // raft_node_set_udata(conn->node, conn);

    return 0;
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

    linkaddr_t addr;
    addr.u8[0] = this_node_id & 0xff; // first byte (LSB)
    addr.u8[1] = this_node_id >> 8 & 0xff; // second byte (MSB) 

    send_data(stage_buffer, offset - stage_buffer, &addr);

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
    // copy message entry message
    // each message might be comprised of multiple "entries"
    // 1. start copy just the message metadata first (see struct)
    memcpy(offset, m, sizeof(msg_appendentries_t));
    // ignore the last `msg_entry_t *` pointer, since we need to copy actual message there
    offset += sizeof(msg_appendentries_t) - sizeof(msg_entry_t*);

    // 2. copy entries one by one
    raft_entry_t *ety = m->entries; 
    int i = 0;
    for (i = 0; i < m->n_entries; i++)
    {
        // skip the first member of the struct (just a pointer)
        memcpy(offset, (unsigned char*)ety + sizeof(raft_entry_t*), sizeof(raft_entry_t) - sizeof(raft_entry_t*));
        offset += sizeof(raft_entry_t) - sizeof(raft_entry_t*);

        ety = raft_get_next_log_entry(raft, ety);

        if (ety == NULL)
            // reached end, break
            break;

        // ensure we don't overshoot PACKETBUF_SIZE in the next iteration
        if ((offset + sizeof(raft_entry_t)) - stage_buffer >= PACKETBUF_SIZE)
            break; // TODO hopefully no sideeffects
    }

    unsigned short this_node_id = (unsigned short)raft_node_get_id(node);

    linkaddr_t addr;
    addr.u8[0] = this_node_id & 0xff; // first byte (LSB)
    addr.u8[1] = this_node_id >> 8 & 0xff; // second byte (MSB) 

    send_data(stage_buffer, offset - stage_buffer, &addr);

    // debug 
    __printf(DEBUG, "msg_size: %d, msg: ", (size_t)(offset - stage_buffer));
    for (i=0; i<offset - stage_buffer; i++) __printf(DEBUG, "%d ", (int)stage_buffer[i]); 
    __printf(DEBUG, "\n");

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
    return -1;
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
    return -1;
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
    // just manage config change (if any)
    if (raft_entry_is_cfg_change(ety))
    {
        // derive nodeid
        unsigned short nodeid = (ety->data.buf[0] & 0xff) + (ety->data.buf[1] >> 8 & 0xff);
        __offer_cfg_change(raft, ety->type, nodeid);
    }

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
    .node_has_sufficient_logs    = __raft_node_has_sufficient_logs,
    .log                         = __raft_debug,
};


/*---------------------------------------------------------------------------*/
// Contiki Process Declarations
PROCESS(main_process, "Main Process");
PROCESS(raft_periodic_process, "Raft Periodic Process");
AUTOSTART_PROCESSES(&main_process);

/*---------------------------------------------------------------------------*/
// Raft periodic ticker
static struct etimer et_periodic;
PROCESS_THREAD(raft_periodic_process, ev, data)
{
  PROCESS_BEGIN();

  while(1) {
    etimer_set(&et_periodic, CLOCK_SECOND);

    PROCESS_WAIT_EVENT_UNTIL(ev == PROCESS_EVENT_TIMER);

    raft_periodic(raft_server, CLOCK_SECOND);
  }

  PROCESS_END();
}

/*---------------------------------------------------------------------------*/
// Main process
PROCESS_THREAD(main_process, ev, data)
{
    PROCESS_EXITHANDLER(mesh_close(&mesh);)
    PROCESS_BEGIN();

    memset(sv, 0, sizeof(server_t));
    sv->node_id = node_id; // see node-id.h

    random_init(node_id);

    raft_server = raft_new();
    raft_set_callbacks(raft_server, &raft_funcs, NULL);

    // add self
    raft_add_node(raft_server, sv->node_id, 1);

    // node_id 1 becomes leader 
    if (sv->node_id == 1) {
        raft_become_leader(raft_server);
        /* We store membership configuration inside the Raft log.
            * This configuration change is going to be the initial membership
            * configuration (ie. original node) inside the Raft log. The
            * first configuration is for a cluster of 1 node. */
        __append_cfg_change(raft_server, RAFT_LOGTYPE_ADD_NODE, sv->node_id);
    }

    // add other nodes
    // TODO this is static for now
    unsigned short i;
    for (i = 1; i <= 5; i++)
    {
        if (i == sv->node_id)
            continue; // don't add self

        raft_add_node(raft_server, i, 0);
    }    

    mesh_open(&mesh, 132, &callbacks);

    // start periodic_raft
    process_start(&raft_periodic_process, NULL);

    PROCESS_END();
}
/*---------------------------------------------------------------------------*/
