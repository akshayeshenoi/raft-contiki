/**
 * Copyright (c) 2013, Willem-Hendrik Thiart
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * @file
 * @brief ADT for managing Raft log entries (aka entries)
 * @author Willem Thiart himself@willemthiart.com
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#include "raft.h"
#include "raft_private.h"
#include "raft_log.h"

#include "contiki.h"
#include "lib/list.h"
#include "lib/memb.h"

#define INITIAL_CAPACITY 10
#define MAX_ENTRIES 50

typedef struct
{
    /* size of array */
    int size;

    /* the amount of elements in the array */
    int count;

    /* position of the queue */
    raft_entry_t *front, *back;

    /* we compact the log, and thus need to increment the Base Log Index */
    int base;

    list_t entries_list;

    /* callbacks */
    raft_cbs_t *cb;
    void* raft;
} log_private_t;

// static allocation of log_private (since it seems we only need it here)
log_private_t log_private;
// use linked list for entries (as opposed to contiguous array)
LIST(raft_entries_list);
// managed memory to allocate/deallocate entry members in the list
MEMB(raft_entries_mem, raft_entry_t, MAX_ENTRIES);

int mod(int a, int b)
{
    int r = a % b;
    return r < 0 ? r + b : r;
}

/**** Ignore for now ****/
int log_load_from_snapshot(log_t *me_, int idx, int term)
{
    // log_private_t* me = (log_private_t*)me_;

    // log_clear(me_);

    // raft_entry_t ety;
    // ety.data.len = 0;
    // ety.id = 1;
    // ety.term = term;
    // ety.type = RAFT_LOGTYPE_SNAPSHOT;

    // int e = log_append_entry(me_, &ety);
    // if (e != 0)
    // {
    //     assert(0);
    //     return e;
    // }

    // me->base = idx - 1;

    return 0;
}

log_t* log_alloc(int initial_size)
{
    log_private_t* me = &log_private;
    memset(me, 0, sizeof(*me));
 
    me->size = initial_size;
    list_init(raft_entries_list);
    // save ref to log_private_t
    me->entries_list = raft_entries_list;

    return (log_t*)me;
}

log_t* log_new()
{
    return log_alloc(INITIAL_CAPACITY);
}

void log_set_callbacks(log_t* me_, raft_cbs_t* funcs, void* raft)
{
    log_private_t* me = (log_private_t*)me_;

    me->raft = raft;
    me->cb = funcs;
}

void log_clear(log_t* me_)
{
    log_private_t* me = (log_private_t*)me_;
    // loop through the list and delete all entries
    while(list_length(me->entries_list) != 0) {
        list_pop(me->entries_list);
    }

    me->count = 0;
    me->back = NULL;
    me->front = NULL;
    me->base = 0;
}

/** TODO: rename log_append */
int log_append_entry(log_t* me_, raft_entry_t* ety)
{
    log_private_t* me = (log_private_t*)me_;
    int idx = me->base + me->count + 1;
    ety->idx = idx;
    int e;

    // check if we have space
    if (list_length(me->entries_list) == MAX_ENTRIES) {
        // remove the youngest one to make space
        // TODO side effects?
        list_pop(me->entries_list);
    }

    // allocate mem for new entry
    raft_entry_t* new_ety = memb_alloc(&raft_entries_mem);
    if (!new_ety)
        return 1;   //TODO which error? 

    memcpy(new_ety, ety, sizeof(raft_entry_t));

    // add to our list
    list_add(me->entries_list, new_ety);

    if (me->cb && me->cb->log_offer)
    {
        void* ud = raft_get_udata(me->raft);
        // actual log entry must be made on FLASH storage
        // by the callback function below (outside the library)
        // TODO for now we don't have flash, so maybe access outside

        e = me->cb->log_offer(me->raft, ud, new_ety, idx);
        if (0 != e)
            return e;
        raft_offer_log(me->raft, new_ety, idx);
    }

    me->count++;
    me->back = new_ety;
    return 0;
}

raft_entry_t* log_get_from_idx(log_t* me_, int idx, int *n_etys)
{
    log_private_t* me = (log_private_t*)me_;

    assert(0 <= idx - 1);

    if (me->base + me->count < idx || idx < me->base)
    {
        *n_etys = 0;
        return NULL;
    }

    // we get the first entry matching the idx
    raft_entry_t *ety, *first_ety = NULL;
    for(ety = list_head(me->entries_list);
        ety != NULL;
        ety = list_item_next(ety))
    {
        if (ety->idx == idx)
            first_ety = ety;
            break;
    }

    // and count number of entries till last one
    // TODO for now this is just the difference from the tail
    // TODO verify this
    int logs_till_end_of_log = list_length(me->entries_list) - first_ety->idx;

    *n_etys = logs_till_end_of_log;
    return first_ety;
}

raft_entry_t* log_get_at_idx(log_t* me_, int idx)
{
    log_private_t* me = (log_private_t*)me_;

    if (idx == 0)
        return NULL;

    if (idx <= me->base)
        return NULL;

    if (me->base + me->count < idx)
        return NULL;

    /* idx starts at 1 */
    idx -= 1;

    raft_entry_t *ety;
    for(ety = list_head(me->entries_list);
        ety != NULL;
        ety = list_item_next(ety))
    {
        if (ety->idx == idx)
            return ety;
    }

    return NULL;
}

int log_count(log_t* me_)
{
    return ((log_private_t*)me_)->count;
}

int log_delete(log_t* me_, int idx)
{
    log_private_t* me = (log_private_t*)me_;

    if (0 == idx)
        return -1;

    if (idx < me->base)
        idx = me->base;

    raft_entry_t *ety;
    for(ety = list_head(me->entries_list);
        ety != NULL;
        ety = list_item_next(ety))
    {
        if (ety->idx == idx)
        {
            int e = me->cb->log_pop(me->raft, raft_get_udata(me->raft),
                                    ety, idx);
            if (0 != e)
                return e;

            raft_pop_log(me->raft, ety, idx);
            me->count--;
        }
    }

    return 0;
}

/********* COMPACTION related *********/
int log_poll(log_t * me_, void** etyp)
{
    // log_private_t* me = (log_private_t*)me_;
    // int idx = me->base + 1;

    // if (0 == me->count)
    //     return -1;

    // const void *elem = &me->entries[me->front];
    // if (me->cb && me->cb->log_poll)
    // {
    //     int e = me->cb->log_poll(me->raft, raft_get_udata(me->raft),
    //                              &me->entries[me->front], idx);
    //     if (0 != e)
    //         return e;
    // }
    // me->front++;
    // me->front = me->front % me->size;
    // me->count--;
    // me->base++;

    // *etyp = (void*)elem;
    return 0;
}

raft_entry_t *log_peektail(log_t * me_)
{
    log_private_t* me = (log_private_t*)me_;

    if (0 == me->count)
        return NULL;

    return list_tail(me->entries_list);
}

void log_empty(log_t * me_)
{
    log_private_t* me = (log_private_t*)me_;

    me->front = 0;
    me->back = 0;
    me->count = 0;
}

void log_free(log_t * me_)
{
    log_private_t* me = (log_private_t*)me_;

    free(me->entries_list);
    free(me);
}

int log_get_current_idx(log_t* me_)
{
    log_private_t* me = (log_private_t*)me_;
    return log_count(me_) + me->base;
}

int log_get_base(log_t* me_)
{
    return ((log_private_t*)me_)->base;
}

raft_entry_t *log_get_next_entry(log_t *me_, raft_entry_t *ety)
{
    return list_item_next(ety);
}
