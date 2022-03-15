#include <stdio.h> /* For printf() */

#include "contiki.h"
#include "net/rime/rime.h"
#include "sys/node-id.h"

#include "raft.h"

static struct mesh_conn mesh;

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
    printf("packet ack'd\n");
}

static void timedout(struct mesh_conn *c)
{
    printf("packet timedout\n");
}

const static struct mesh_callbacks callbacks = {recv, sent, timedout};
/*---------------------------------------------------------------------------*/

void send_data(void *data, size_t len, linkaddr_t *addr)
{
    packetbuf_copyfrom(data, len);
    mesh_send(&mesh, addr); // non-blocking
    printf("packet transmitted\n");
}

/*---------------------------------------------------------------------------*/

// Raft Callbacks
int __raft_send_appendentries(
    // raft_server_t* raft,
    // void *user_data,
    // raft_node_t *node,
    // msg_appendentries_t* m
)
{
    linkaddr_t addr;
    addr.u8[0] = 1;
    addr.u8[1] = 0;
    char *data = "Hello";
    size_t len = strlen(data);
    packetbuf_copyfrom(data, len);

    send_data(data, len, &addr);
}

// raft_cbs_t raft_funcs = {
//     .send_requestvote            = __raft_send_requestvote,
//     .send_appendentries          = __raft_send_appendentries,
//     .applylog                    = __raft_applylog,
//     .persist_vote                = __raft_persist_vote,
//     .persist_term                = __raft_persist_term,
//     .log_offer                   = __raft_logentry_offer,
//     .log_poll                    = __raft_logentry_poll,
//     .log_pop                     = __raft_logentry_pop,
//     .node_has_sufficient_logs    = __raft_node_has_sufficient_logs,
//     .log                         = __raft_log,
// };

/*---------------------------------------------------------------------------*/
PROCESS_THREAD(main_process, ev, data)
{
    PROCESS_EXITHANDLER(mesh_close(&mesh);)
    PROCESS_BEGIN();

    raft_server_t *raft_server = raft_new();
    // raft_set_callbacks(sv->raft, &raft_funcs, sv);
    // add self -> raft_add_node(sv->raft, NULL, sv->node_id, 1);
    // raft_become_leader(sv->raft);

    // start raft periodic
    if (raft_server)
    {
    }

    mesh_open(&mesh, 132, &callbacks);

    if (node_id != 1)
    {
        __raft_send_appendentries();
    }

    PROCESS_END();
}
/*---------------------------------------------------------------------------*/
