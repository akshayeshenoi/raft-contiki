#include <stdio.h> /* For printf() */
#include <string.h>

#include "contiki.h"
#include "net/rime/rime.h"
#include "sys/node-id.h"

#include "raft.h"

#define APP_DBG

#ifdef APP_DBG
    #define PRINT_FN_DBG() printf("==== Entering %s() ====\n", __func__)
    #define PRINT_DBG(p) printf("%s: %s", __func__, p)
#else
    #define PRINT_FN_DBG()
#endif

#include "proto.h"

static struct mesh_conn mesh;
static server_t server;
static server_t *sv = &server;

static unsigned char stage_buffer[PACKETBUF_SIZE];

/*---------------------------------------------------------------------------*/
PROCESS(main_process, "Main Process");
AUTOSTART_PROCESSES(&main_process);
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
    PRINT_DBG("packet ack'd\n");
}

static void timedout(struct mesh_conn *c)
{
    PRINT_DBG("packet timedout\n");
}

const static struct mesh_callbacks callbacks = {recv, sent, timedout};
/*---------------------------------------------------------------------------*/

void send_data(void *data, size_t len, linkaddr_t *addr)
{
    PRINT_FN_DBG();
    packetbuf_copyfrom(data, len);
    mesh_send(&mesh, addr); // non-blocking
    PRINT_DBG("packet transmitted\n");
}

/*----------------------------Raft Callbacks---------------------------------*/

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

    send_data(stage_buffer, stage_buffer - offset, &addr);

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
    return -1;
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
    return -1;
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
    // TODO this is not working atm
    PRINT_DBG(strcat((char *)buf, " -- raft\n"));
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
PROCESS_THREAD(main_process, ev, data)
{
    PROCESS_EXITHANDLER(mesh_close(&mesh);)
    PROCESS_BEGIN();

    memset(sv, 0, sizeof(server_t));

    raft_server_t *raft_server = raft_new();
    // raft_set_callbacks(sv->raft, &raft_funcs, sv);
    // add self -> raft_add_node(sv->raft, NULL, sv->node_id, 1);
    // raft_become_leader(sv->raft);

    // start raft periodic
    if (raft_server)
    {
    }

    mesh_open(&mesh, 132, &callbacks);

    // if (node_id != 1)
    // {
    //     __raft_send_appendentries();
    // }

    PROCESS_END();
}
/*---------------------------------------------------------------------------*/
