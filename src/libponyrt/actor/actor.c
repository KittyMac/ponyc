#define PONY_WANT_ATOMIC_DEFS

#include "actor.h"
#include "../sched/scheduler.h"
#include "../sched/cpu.h"
#include "../mem/pool.h"
#include "../gc/cycle.h"
#include "../gc/trace.h"
#include "ponyassert.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <dtrace.h>

#ifdef USE_VALGRIND
#include <valgrind/helgrind.h>
#endif

// default actor batch size
#define PONY_SCHED_BATCH 100

// hack to get the padding values needed before at compile time...
#if INTPTR_MAX == INT64_MAX
//char (*__kaboom)[  (offsetof(pony_actor_t, gc) + sizeof(gc_t))  ][ (sizeof(pony_actor_pad_t)) ] = 1;
#elif INTPTR_MAX == INT32_MAX
//char (*__kaboom)[  (offsetof(pony_actor_t, gc) + sizeof(gc_t))  ][ (sizeof(pony_actor_pad_t)) ] = 1;
#endif

// Ignore padding at the end of the type.
pony_static_assert((offsetof(pony_actor_t, gc) + sizeof(gc_t)) ==
   sizeof(pony_actor_pad_t), "Wrong actor pad size!");

static bool actor_noblock = false;

// The flags of a given actor cannot be mutated from more than one actor at
// once, so these operations need not be atomic RMW.

bool has_flag(pony_actor_t* actor, uint8_t flag)
{
  uint8_t flags = atomic_load_explicit(&actor->flags, memory_order_relaxed);
  return (flags & flag) != 0;
}

static void set_flag(pony_actor_t* actor, uint8_t flag)
{
  uint8_t flags = atomic_load_explicit(&actor->flags, memory_order_relaxed);
  atomic_store_explicit(&actor->flags, flags | flag, memory_order_relaxed);
}

static void unset_flag(pony_actor_t* actor, uint8_t flag)
{
  uint8_t flags = atomic_load_explicit(&actor->flags, memory_order_relaxed);
  atomic_store_explicit(&actor->flags, flags & (uint8_t)~flag,
    memory_order_relaxed);
}

#ifndef PONY_NDEBUG
static bool well_formed_msg_chain(pony_msg_t* first, pony_msg_t* last)
{
  // A message chain is well formed if last is reachable from first and is the
  // end of the chain. first should also be the start of the chain but we can't
  // verify that.

  if((first == NULL) || (last == NULL) ||
    (atomic_load_explicit(&last->next, memory_order_relaxed) != NULL))
    return false;

  pony_msg_t* m1 = first;
  pony_msg_t* m2 = first;

  while((m1 != NULL) && (m2 != NULL))
  {
    if(m2 == last)
      return true;

    m2 = atomic_load_explicit(&m2->next, memory_order_relaxed);

    if(m2 == last)
      return true;
    if(m2 == NULL)
      return false;

    m1 = atomic_load_explicit(&m1->next, memory_order_relaxed);
    m2 = atomic_load_explicit(&m2->next, memory_order_relaxed);

    if(m1 == m2)
      return false;
  }

  return false;
}
#endif

static void send_unblock(pony_actor_t* actor)
{
  // Send unblock before continuing.
  unset_flag(actor, FLAG_BLOCKED | FLAG_BLOCKED_SENT);
  ponyint_cycle_unblock(actor);
}

static bool handle_message(pony_ctx_t* ctx, pony_actor_t* actor, pony_msg_t* msg)
{
#ifdef USE_MEMTRACK_MESSAGES
  ctx->num_messages--;
#endif

  switch(msg->id)
  {
    case ACTORMSG_ACQUIRE:
    {
#ifdef USE_MEMTRACK_MESSAGES
      ctx->mem_used_messages -= sizeof(pony_msgp_t);
      ctx->mem_allocated_messages -= POOL_ALLOC_SIZE(pony_msgp_t);
#endif

      pony_assert(!ponyint_is_cycle(actor));
      pony_msgp_t* m = (pony_msgp_t*)msg;

#ifdef USE_MEMTRACK
      ctx->mem_used_actors -= (sizeof(actorref_t)
        + ponyint_objectmap_total_mem_size(&((actorref_t*)m->p)->map));
      ctx->mem_allocated_actors -= (POOL_ALLOC_SIZE(actorref_t)
        + ponyint_objectmap_total_alloc_size(&((actorref_t*)m->p)->map));
#endif

      if(ponyint_gc_acquire(&actor->gc, (actorref_t*)m->p) &&
        has_flag(actor, FLAG_BLOCKED_SENT))
      {
        // send unblock if we've sent a block
        send_unblock(actor);
      }
	  
      return false;
    }

    case ACTORMSG_RELEASE:
    {
#ifdef USE_MEMTRACK_MESSAGES
      ctx->mem_used_messages -= sizeof(pony_msgp_t);
      ctx->mem_allocated_messages -= POOL_ALLOC_SIZE(pony_msgp_t);
#endif

      pony_assert(!ponyint_is_cycle(actor));
      pony_msgp_t* m = (pony_msgp_t*)msg;

#ifdef USE_MEMTRACK
      ctx->mem_used_actors -= (sizeof(actorref_t)
        + ponyint_objectmap_total_mem_size(&((actorref_t*)m->p)->map));
      ctx->mem_allocated_actors -= (POOL_ALLOC_SIZE(actorref_t)
        + ponyint_objectmap_total_alloc_size(&((actorref_t*)m->p)->map));
#endif

      if(ponyint_gc_release(&actor->gc, (actorref_t*)m->p) &&
        has_flag(actor, FLAG_BLOCKED_SENT))
      {
        // send unblock if we've sent a block
        send_unblock(actor);
      }
	  
	    actor->heap_is_dirty = true;
	  
      return false;
    }

    case ACTORMSG_ACK:
    {
#ifdef USE_MEMTRACK_MESSAGES
      ctx->mem_used_messages -= sizeof(pony_msgi_t);
      ctx->mem_allocated_messages -= POOL_ALLOC_SIZE(pony_msgi_t);
#endif

      pony_assert(ponyint_is_cycle(actor));
      DTRACE3(ACTOR_MSG_RUN, (uintptr_t)ctx->scheduler, (uintptr_t)actor, msg->id);
      actor->type->dispatch(ctx, actor, msg);
      return false;
    }

    case ACTORMSG_CONF:
    {
#ifdef USE_MEMTRACK_MESSAGES
      ctx->mem_used_messages -= sizeof(pony_msgi_t);
      ctx->mem_allocated_messages -= POOL_ALLOC_SIZE(pony_msgi_t);
#endif

      pony_assert(!ponyint_is_cycle(actor));
      if(has_flag(actor, FLAG_BLOCKED_SENT))
      {
        // We've sent a block message, send confirm.
        pony_msgi_t* m = (pony_msgi_t*)msg;
        ponyint_cycle_ack(m->i);
      }

      return false;
    }

    case ACTORMSG_ISBLOCKED:
    {
#ifdef USE_MEMTRACK_MESSAGES
      ctx->mem_used_messages -= sizeof(pony_msg_t);
      ctx->mem_allocated_messages -= POOL_ALLOC_SIZE(pony_msg_t);
#endif

      pony_assert(!ponyint_is_cycle(actor));
      if(has_flag(actor, FLAG_BLOCKED) && !has_flag(actor, FLAG_BLOCKED_SENT))
      {
        // We're blocked, send block message.
        set_flag(actor, FLAG_BLOCKED_SENT);
        ponyint_cycle_block(actor, &actor->gc);
      }

      return false;
    }

    case ACTORMSG_BLOCK:
    {
      // memtrack messages tracked in cycle detector

      pony_assert(ponyint_is_cycle(actor));
      DTRACE3(ACTOR_MSG_RUN, (uintptr_t)ctx->scheduler, (uintptr_t)actor, msg->id);
      actor->type->dispatch(ctx, actor, msg);
      return false;
    }

    case ACTORMSG_UNBLOCK:
    {
#ifdef USE_MEMTRACK_MESSAGES
      ctx->mem_used_messages -= sizeof(pony_msgp_t);
      ctx->mem_allocated_messages -= POOL_ALLOC_SIZE(pony_msgp_t);
#endif

      pony_assert(ponyint_is_cycle(actor));
      DTRACE3(ACTOR_MSG_RUN, (uintptr_t)ctx->scheduler, (uintptr_t)actor, msg->id);
      actor->type->dispatch(ctx, actor, msg);
      return false;
    }

    case ACTORMSG_CREATED:
    {
#ifdef USE_MEMTRACK_MESSAGES
      ctx->mem_used_messages -= sizeof(pony_msgp_t);
      ctx->mem_allocated_messages -= POOL_ALLOC_SIZE(pony_msgp_t);
#endif

      pony_assert(ponyint_is_cycle(actor));
      DTRACE3(ACTOR_MSG_RUN, (uintptr_t)ctx->scheduler, (uintptr_t)actor, msg->id);
      actor->type->dispatch(ctx, actor, msg);
      return false;
    }

    case ACTORMSG_DESTROYED:
    {
#ifdef USE_MEMTRACK_MESSAGES
      ctx->mem_used_messages -= sizeof(pony_msgp_t);
      ctx->mem_allocated_messages -= POOL_ALLOC_SIZE(pony_msgp_t);
#endif

      pony_assert(ponyint_is_cycle(actor));
      DTRACE3(ACTOR_MSG_RUN, (uintptr_t)ctx->scheduler, (uintptr_t)actor, msg->id);
      actor->type->dispatch(ctx, actor, msg);
      return false;
    }

    case ACTORMSG_CHECKBLOCKED:
    {
#ifdef USE_MEMTRACK_MESSAGES
      ctx->mem_used_messages -= sizeof(pony_msg_t);
      ctx->mem_allocated_messages -= POOL_ALLOC_SIZE(pony_msg_t);
#endif

      pony_assert(ponyint_is_cycle(actor));
      DTRACE3(ACTOR_MSG_RUN, (uintptr_t)ctx->scheduler, (uintptr_t)actor, msg->id);
      actor->type->dispatch(ctx, actor, msg);
      return false;
    }

    default:
    {
#ifdef USE_MEMTRACK_MESSAGES
      ctx->mem_used_messages -= POOL_SIZE(msg->index);
      ctx->mem_allocated_messages -= POOL_SIZE(msg->index);
#endif

      pony_assert(!ponyint_is_cycle(actor));
      if(has_flag(actor, FLAG_BLOCKED_SENT))
      {
        // send unblock if we've sent a block
        send_unblock(actor);
      }

      DTRACE3(ACTOR_MSG_RUN, (uintptr_t)ctx->scheduler, (uintptr_t)actor, msg->id);
      actor->type->dispatch(ctx, actor, msg);
      return true;
    }
  }
}

static void try_gc(pony_ctx_t* ctx, pony_actor_t* actor)
{
  size_t used_before = actor->heap.used;
  
  if(!ponyint_heap_startgc(&actor->heap, actor->heap_is_dirty))
    return;
  
#ifdef RUNTIME_ANALYSIS
  if (ctx->analysis_enabled) {
    saveRuntimeAnalyticForActor(ctx, actor, ANALYTIC_GC_START);
  }
#endif
  
  DTRACE1(GC_START, (uintptr_t)ctx->scheduler);

  ponyint_gc_mark(ctx);

  if(actor->type->trace != NULL)
    actor->type->trace(ctx, actor);

  ponyint_mark_done(ctx);
  ponyint_heap_endgc(&actor->heap);
  
  size_t used_after = actor->heap.used;
  
  if (used_after < used_before) {
    if(actor->type != NULL && actor->type->freed_fn != NULL){
      actor->type->freed_fn(actor, actor->heap_is_dirty);
    }
  }
  
  actor->heap_is_dirty = false;
  
#ifdef RUNTIME_ANALYSIS
  if (ctx->analysis_enabled) {
    saveRuntimeAnalyticForActor(ctx, actor, ANALYTIC_GC_END);
  }
#endif

  DTRACE1(GC_END, (uintptr_t)ctx->scheduler);
}

// return true if mute occurs
static bool maybe_mute(pony_actor_t* actor)
{
  // if we become muted as a result of handling a message, bail out now.
  // we aren't set to "muted" at this point. setting to muted during a
  // a behavior can lead to race conditions that might result in a
  // deadlock.
  // Given that actor's are not run when they are muted, then when we
  // started out batch, actor->muted would have been 0. If any of our
  // message sends would result in the actor being muted, that value will
  // have changed to greater than 0.
  //
  // We will then set the actor to "muted". Once set, any actor sending
  // a message to it will be also be muted unless said sender is marked
  // as overloaded.
  //
  // The key points here is that:
  //   1. We can't set the actor to "muted" until after its finished running
  //   a behavior.
  //   2. We should bail out from running the actor and return false so that
  //   it won't be rescheduled.
  if(atomic_load_explicit(&actor->muted, memory_order_acquire) > 0)
  {
    ponyint_mute_actor(actor);
    return true;
  }

  return false;
}

static bool batch_limit_reached(pony_actor_t* actor, bool polling)
{
  if(!has_flag(actor, FLAG_OVERLOADED) && !polling)
  {
    // If we hit our batch size, consider this actor to be overloaded
    // only if we're not polling from C code.
    // Overloaded actors are allowed to send to other overloaded actors
    // and to muted actors without being muted themselves.
    ponyint_actor_setoverloaded(actor);
  }

  return !has_flag(actor, FLAG_UNSCHEDULED);
}

bool ponyint_actor_run(pony_ctx_t* ctx, pony_actor_t* actor, bool polling)
{
  pony_assert(!ponyint_is_muted(actor));
  ctx->current = actor;
  
  if(actor->running) {
  	// we're actively running, but someone else is trying to run us at the same time! No worries,
  	// we will just flag ourself to be rescheduled.
  	return true;
  }
  actor->running = true;
  
  pony_msg_t* msg;
  int32_t app = 0;
    
  if (actor->type != NULL) {
	  if(actor->type->use_main_thread_fn != NULL){
		  if(actor->use_main_thread == false) {
		  	actor->use_main_thread = actor->type->use_main_thread_fn(actor);
		  }
		  if(actor->use_main_thread == true && ctx->scheduler != NULL && ctx->scheduler->main_thread == false) {
			  // we are a main thread only actor and we are not on the main thread scheduler.
			  // this shouldn't happen, but if it does then we yield and ask ourselves to be rescheduled
			  actor->yield = true;
			  actor->running = false;
			  return true;
		  }
	  }
	  if(actor->type->tag_fn != NULL && actor->tag == 0){
	    actor->tag = (int32_t)actor->type->tag_fn(actor);
	  }
	  if(actor->type->priority_fn != NULL && actor->priority == PONY_DEFAULT_ACTOR_PRIORITY){
      actor->priority = (int32_t)actor->type->priority_fn(actor);
	  }
	  if(actor->type->batch_fn != NULL && actor->batch == PONY_SCHED_BATCH){
	    actor->batch = (int32_t)actor->type->batch_fn(actor);
  		if (actor->batch <= 0) {
  			actor->batch = 1;
  		}
	  }
  }
  
  // we reset the yield flag here because the scheduler uses it to ignore priority
  actor->yield = false;

#ifdef RUNTIME_ANALYSIS
  if (ctx->analysis_enabled) {
  	saveRuntimeAnalyticForActor(ctx, actor, ANALYTIC_RUN_START);
  }
#endif

  // If we have been scheduled, the head will not be marked as empty.
  pony_msg_t* head = atomic_load_explicit(&actor->q.head, memory_order_relaxed);

  while((msg = ponyint_actor_messageq_pop(&actor->q
#ifdef USE_DYNAMIC_TRACE
    , ctx->scheduler, ctx->current
#endif
    )) != NULL)
  {
    if (msg->id < ACTORMSG_MINIMUM_ID) {
      actor->type->dispatch(ctx, actor, msg); 
      // If we handle an application message, try to gc.
      app++;

      // maybe mute actor; returns true if mute occurs
      if(maybe_mute(actor)){
#ifdef RUNTIME_ANALYSIS
        if (ctx->analysis_enabled) {
          saveRuntimeAnalyticForActor(ctx, actor, ANALYTIC_RUN_END);
        }
#endif
        try_gc(ctx, actor);
        actor->running = false;
        return false;
      }

      // if we've reached our batch limit
      // or if we're polling where we want to stop after one app message
      if(actor->yield || app == actor->batch || polling){
#ifdef RUNTIME_ANALYSIS
        if (ctx->analysis_enabled) {
          saveRuntimeAnalyticForActor(ctx, actor, ANALYTIC_RUN_END);
        }
#endif
        try_gc(ctx, actor);
        actor->running = false;
        return batch_limit_reached(actor, polling || (actor->yield && app < actor->batch));
      }
    }else{
      handle_message(ctx, actor, msg);
    }

    // Stop handling a batch if we reach the head we found when we were
    // scheduled.
    if(msg == head)
      break;
  }
  
#ifdef RUNTIME_ANALYSIS
  if (ctx->analysis_enabled) {
    saveRuntimeAnalyticForActor(ctx, actor, ANALYTIC_RUN_END);
  }
#endif
  
  // We didn't hit our app message batch limit. We now believe our queue to be
  // empty, but we may have received further messages.
  pony_assert(app < actor->batch);
  pony_assert(!ponyint_is_muted(actor));

  if(has_flag(actor, FLAG_OVERLOADED))
  {
    // if we were overloaded and didn't process a full batch, set ourselves as
    // no longer overloaded. Once this is done:
    // 1- sending to this actor is no longer grounds for an actor being muted
    // 2- this actor can no longer send to other actors free from muting should
    //    the receiver be overloaded or muted
    ponyint_actor_unsetoverloaded(actor);
  }
  
  // if we processed an application message, then we should attempt to gc
  if(app > 0) {
    try_gc(ctx, actor);
  }

  if(has_flag(actor, FLAG_UNSCHEDULED))
  {
    // When unscheduling, don't mark the queue as empty, since we don't want
    // to get rescheduled if we receive a message.
    actor->running = false;
    return false;
  }

  // If we have processed any application level messages, defer blocking.
  if(app > 0) {
    actor->running = false;
    return true;
  }

  // note that we're logically blocked
  if(!has_flag(actor, FLAG_BLOCKED | FLAG_SYSTEM | FLAG_BLOCKED_SENT))
  {
    set_flag(actor, FLAG_BLOCKED);
  }

  bool empty = ponyint_messageq_markempty(&actor->q);
  if (empty && actor_noblock && (actor->gc.rc == 0))
  {
    // when 'actor_noblock` is true, the cycle detector isn't running.
    // this means actors won't be garbage collected unless we take special
    // action. Here, we know that:
    // - the actor has no messages in its queue
    // - there's no references to this actor
    // therefore if `noblock` is on, we should garbage collect the actor.
	  actor->running = false;
	  
    ponyint_actor_setpendingdestroy(actor);
    ponyint_actor_final(ctx, actor);
    ponyint_actor_destroy(actor);
	  
	  return false;
  }
  
  // Return true (i.e. reschedule immediately) if our queue isn't empty.
  actor->running = false;
  return !empty;
}

void ponyint_actor_destroy(pony_actor_t* actor)
{
  pony_assert(has_flag(actor, FLAG_PENDINGDESTROY));
  
#ifdef RUNTIME_ANALYSIS
  {
    pony_ctx_t* ctx = pony_ctx();
    if (ctx->analysis_enabled) {
      saveRuntimeAnalyticForActor(ctx, actor, ANALYTIC_ACTOR_DESTROYED);
    }
  }
#endif

  // Make sure the actor being destroyed has finished marking its queue
  // as empty. Otherwise, it may spuriously see that tail and head are not
  // the same and fail to mark the queue as empty, resulting in it getting
  // rescheduled.
  pony_msg_t* head = NULL;
  do
  {
    head = atomic_load_explicit(&actor->q.head, memory_order_relaxed);
  } while(((uintptr_t)head & (uintptr_t)1) != (uintptr_t)1);

  atomic_thread_fence(memory_order_acquire);
#ifdef USE_VALGRIND
  ANNOTATE_HAPPENS_AFTER(&actor->q.head);
#endif

  ponyint_messageq_destroy(&actor->q);
  ponyint_gc_destroy(&actor->gc);
  ponyint_heap_destroy(&actor->heap);

#ifdef USE_MEMTRACK
  pony_ctx_t* ctx = pony_ctx();
  ctx->mem_used_actors -= actor->type->size;
  ctx->mem_allocated_actors -= ponyint_pool_used_size(actor->type->size);
#endif

  
  // Note: While the memset to 0 is not strictly necessary, there were quite
  // a few "access the actor after it was deleted" bugs going by
  // undetected because the contents of the memory just hadn't been
  // changed yet.  Leaving this code in as a reminder to help hunt
  // down such crash bugs in the future.
  //int32_t typeSize = actor->type->size;
  //memset(actor, 0, typeSize);
  //ponyint_pool_free_size(typeSize, actor);
  
  // Free variable sized actors correctly.
  ponyint_pool_free_size(actor->type->size, actor);
}

gc_t* ponyint_actor_gc(pony_actor_t* actor)
{
  return &actor->gc;
}

heap_t* ponyint_actor_heap(pony_actor_t* actor)
{
  return &actor->heap;
}

bool ponyint_actor_pendingdestroy(pony_actor_t* actor)
{
  return has_flag(actor, FLAG_PENDINGDESTROY);
}

void ponyint_actor_setpendingdestroy(pony_actor_t* actor)
{
  // This is thread-safe, even though the flag is set from the cycle detector.
  // The function is only called after the cycle detector has detected a true
  // cycle and an actor won't change its flags if it is part of a true cycle.
  // The synchronisation is done through the ACK message sent by the actor to
  // the cycle detector.
  set_flag(actor, FLAG_PENDINGDESTROY);
}

void ponyint_actor_final(pony_ctx_t* ctx, pony_actor_t* actor)
{
  // This gets run while the cycle detector is handling a message. Set the
  // current actor before running anything.
  pony_actor_t* prev = ctx->current;
  ctx->current = actor;

  // Run the actor finaliser if it has one.
  if(actor->type->final != NULL)
    actor->type->final(actor);

  // Run all outstanding object finalisers.
  ponyint_heap_final(&actor->heap);

  // Restore the current actor.
  ctx->current = prev;
}

void ponyint_actor_sendrelease(pony_ctx_t* ctx, pony_actor_t* actor)
{
  ponyint_gc_sendrelease(ctx, &actor->gc);
}

void ponyint_actor_setsystem(pony_actor_t* actor)
{
  set_flag(actor, FLAG_SYSTEM);
}

void ponyint_actor_setnoblock(bool state)
{
  actor_noblock = state;
}

bool ponyint_actor_getnoblock()
{
  return actor_noblock;
}

size_t ponyint_actor_num_messages(pony_actor_t* actor)
{
	size_t n = actor->q.num_messages;
	if (n < 0) {
		return 0;
	}
	return n;
}

void ponyint_actor_yield(pony_actor_t* actor)
{
	// setting to true will cause the actor to end its run early
	actor->yield = true;
}

void ponyint_actor_flag_gc(pony_actor_t* actor)
{
	// setting to true will cause the actor to force a garbage collect the 
  // next time it tries
	actor->heap_is_dirty = true;
}

PONY_API pony_actor_t* pony_create(pony_ctx_t* ctx, pony_type_t* type)
{
  pony_assert(type != NULL);
  
  // allocate variable sized actors correctly
  pony_actor_t* actor = (pony_actor_t*)ponyint_pool_alloc_size(type->size);
  bool local_actor_noblock = actor_noblock;
  
  memset(actor, 0, type->size);
  actor->type = type;
  actor->priority = PONY_DEFAULT_ACTOR_PRIORITY;
  actor->batch = PONY_SCHED_BATCH;
  
  if(ctx->analysis_enabled){
	static int32_t actorUID = 1;
  	actor->uid = actorUID++;
  }
    
  
#ifdef USE_MEMTRACK
  ctx->mem_used_actors += type->size;
  ctx->mem_allocated_actors += ponyint_pool_used_size(type->size);
#endif

  ponyint_messageq_init(&actor->q);
  ponyint_heap_init(&actor->heap);
  ponyint_gc_done(&actor->gc);

  if(local_actor_noblock)
    ponyint_actor_setsystem(actor);

  if(ctx->current != NULL)
  {
    // actors begin unblocked and referenced by the creating actor
    actor->gc.rc = GC_INC_MORE;
    ponyint_gc_createactor(ctx->current, actor);
  } else {
    // no creator, so the actor isn't referenced by anything
    actor->gc.rc = 0;
  }

  // tell the cycle detector we exist if block messages are enabled
  if(!local_actor_noblock)
    ponyint_cycle_actor_created(actor);

  return actor;
}

PONY_API void ponyint_destroy(pony_ctx_t* ctx, pony_actor_t* actor)
{
  ((void) ctx);
  // This destroys an actor immediately.
  // The finaliser is not called.

  // Notify cycle detector of actor being destroyed
  ponyint_cycle_actor_destroyed(actor);

  ponyint_actor_setpendingdestroy(actor);
  ponyint_actor_destroy(actor);
}

PONY_API pony_msg_t* pony_alloc_msg(uint32_t index, uint32_t id)
{
#ifdef USE_MEMTRACK_MESSAGES
  pony_ctx_t* ctx = pony_ctx();
  ctx->mem_used_messages += POOL_SIZE(index);
  ctx->mem_allocated_messages += POOL_SIZE(index);
  ctx->num_messages++;
#endif

  pony_msg_t* msg = (pony_msg_t*)ponyint_pool_alloc(index);
  msg->index = index;
  msg->id = id;
#ifndef PONY_NDEBUG
  atomic_store_explicit(&msg->next, NULL, memory_order_relaxed);
#endif

  return msg;
}

PONY_API pony_msg_t* pony_alloc_msg_size(size_t size, uint32_t id)
{
  return pony_alloc_msg((uint32_t)ponyint_pool_index(size), id);
}

PONY_API void pony_sendv(pony_ctx_t* ctx, pony_actor_t* to, pony_msg_t* first,
  pony_msg_t* last, bool has_app_msg)
{
  // The function takes a prebuilt chain instead of varargs because the latter
  // is expensive and very hard to optimise.

  pony_assert(well_formed_msg_chain(first, last));

  if(DTRACE_ENABLED(ACTOR_MSG_SEND))
  {
    pony_msg_t* m = first;

    while(m != last)
    {
      DTRACE4(ACTOR_MSG_SEND, (uintptr_t)ctx->scheduler, m->id,
        (uintptr_t)ctx->current, (uintptr_t)to);
      m = atomic_load_explicit(&m->next, memory_order_relaxed);
    }

    DTRACE4(ACTOR_MSG_SEND, (uintptr_t)ctx->scheduler, last->id,
        (uintptr_t)ctx->current, (uintptr_t)to);
  }

  if(has_app_msg) {
	  ponyint_maybe_mute(ctx, to);
  }
  
#ifdef RUNTIME_ANALYSIS
  if (ctx->analysis_enabled > 1) {
    saveRuntimeAnalyticForActorMessage(ctx, ctx->current, to, (has_app_msg ? ANALYTIC_APP_MESSAGE_SENT : ANALYTIC_MESSAGE_SENT));
  }
#endif

  if(ponyint_actor_messageq_push(&to->q, first, last
#ifdef USE_DYNAMIC_TRACE
    , ctx->scheduler, ctx->current, to
#endif
    ))
  {
    if(!has_flag(to, FLAG_UNSCHEDULED) && !ponyint_is_muted(to))
    {
      ponyint_sched_add(ctx, to);
    }
  }
}


// This is a bit convoluted, but the idea is that we want to
// catch any error thrown while handle_message is executing.
// We will throw the error again, but we need to reset the 
// context's current field first
typedef struct {
  pony_ctx_t* ctx;
  pony_actor_t* to;
  pony_msg_t* msg;
}pony_handle_message_t;

void pony_error_clear_context(void* arg)
{
  pony_handle_message_t* x = (pony_handle_message_t*)arg;
  handle_message(x->ctx, x->to, x->msg);
}

PONY_API void pony_sendv_synchronous_constructor(pony_ctx_t* ctx, pony_actor_t* to, pony_msg_t* msg)
{
  pony_actor_t * saved = ctx->current;
  ctx->current = to;
  
  pony_handle_message_t x = { ctx, to, msg };
  bool success = pony_try(pony_error_clear_context, &x);
  ctx->current = saved;
  
  if(!success){
    pony_error_again();
  }
}

PONY_API void pony_sendv_single(pony_ctx_t* ctx, pony_actor_t* to,
  pony_msg_t* first, pony_msg_t* last, bool has_app_msg)
{
  // The function takes a prebuilt chain instead of varargs because the latter
  // is expensive and very hard to optimise.

  pony_assert(well_formed_msg_chain(first, last));

  if(DTRACE_ENABLED(ACTOR_MSG_SEND))
  {
    pony_msg_t* m = first;

    while(m != last)
    {
      DTRACE4(ACTOR_MSG_SEND, (uintptr_t)ctx->scheduler, m->id,
        (uintptr_t)ctx->current, (uintptr_t)to);
      m = atomic_load_explicit(&m->next, memory_order_relaxed);
    }

    DTRACE4(ACTOR_MSG_SEND, (uintptr_t)ctx->scheduler, last->id,
        (uintptr_t)ctx->current, (uintptr_t)to);
  }

  if(has_app_msg) {
	  ponyint_maybe_mute(ctx, to);
  }

#ifdef RUNTIME_ANALYSIS
  if (ctx->analysis_enabled > 1) {
    saveRuntimeAnalyticForActorMessage(ctx, ctx->current, to, (has_app_msg ? ANALYTIC_APP_MESSAGE_SENT : ANALYTIC_MESSAGE_SENT));
  }
#endif

  if(ponyint_actor_messageq_push_single(&to->q, first, last
#ifdef USE_DYNAMIC_TRACE
    , ctx->scheduler, ctx->current, to
#endif
    ))
  {
    if(!has_flag(to, FLAG_UNSCHEDULED) && !ponyint_is_muted(to))
    {
      // if the receiving actor is currently not unscheduled AND it's not
      // muted, schedule it.
      ponyint_sched_add(ctx, to);
    }
  }
}

void ponyint_maybe_mute(pony_ctx_t* ctx, pony_actor_t* to)
{
	pony_actor_t * from = ctx->current;
  if(from != NULL)
  {
    // only mute a sender IF:
    // 1. the receiver is overloaded/under pressure/muted
    // AND
    // 2. the sender isn't overloaded or under pressure
    // AND
    // 3. we are sending to another actor (as compared to sending to self)
    if(ponyint_triggers_muting(to) &&
       !has_flag(from, FLAG_OVERLOADED) &&
       !has_flag(from, FLAG_UNDER_PRESSURE) &&
       from != to)
    {
  	  // bad things happen if we try to mute an actor which is not scheduled...
  	  if (has_flag(from, FLAG_UNSCHEDULED)) {
  		  return;
  	  }
      ponyint_sched_mute(ctx, from, to);
    }
  }
}

PONY_API void pony_chain(pony_msg_t* prev, pony_msg_t* next)
{
  pony_assert(atomic_load_explicit(&prev->next, memory_order_relaxed) == NULL);
  atomic_store_explicit(&prev->next, next, memory_order_relaxed);
}

PONY_API void pony_send(pony_ctx_t* ctx, pony_actor_t* to, uint32_t id)
{
#ifdef USE_MEMTRACK_MESSAGES
  ctx->mem_used_messages += sizeof(pony_msg_t);
  ctx->mem_used_messages -= POOL_ALLOC_SIZE(pony_msg_t);
#endif

  pony_msg_t* m = pony_alloc_msg(POOL_INDEX(sizeof(pony_msg_t)), id);
  pony_sendv(ctx, to, m, m, id <= ACTORMSG_APPLICATION_START);
}

PONY_API void pony_sendp(pony_ctx_t* ctx, pony_actor_t* to, uint32_t id,
  void* p)
{
#ifdef USE_MEMTRACK_MESSAGES
  ctx->mem_used_messages += sizeof(pony_msgp_t);
  ctx->mem_used_messages -= POOL_ALLOC_SIZE(pony_msgp_t);
#endif

  pony_msgp_t* m = (pony_msgp_t*)pony_alloc_msg(
    POOL_INDEX(sizeof(pony_msgp_t)), id);
  m->p = p;

  pony_sendv(ctx, to, &m->msg, &m->msg, id <= ACTORMSG_APPLICATION_START);
}

PONY_API void pony_sendi(pony_ctx_t* ctx, pony_actor_t* to, uint32_t id,
  intptr_t i)
{
#ifdef USE_MEMTRACK_MESSAGES
  ctx->mem_used_messages += sizeof(pony_msgi_t);
  ctx->mem_used_messages -= POOL_ALLOC_SIZE(pony_msgi_t);
#endif

  pony_msgi_t* m = (pony_msgi_t*)pony_alloc_msg(
    POOL_INDEX(sizeof(pony_msgi_t)), id);
  m->i = i;

  pony_sendv(ctx, to, &m->msg, &m->msg, id <= ACTORMSG_APPLICATION_START);
}

PONY_API void* pony_alloc(pony_ctx_t* ctx, size_t size)
{
  pony_assert(ctx->current != NULL);
  DTRACE2(HEAP_ALLOC, (uintptr_t)ctx->scheduler, size);

  return ponyint_heap_alloc(ctx->current, &ctx->current->heap, size);
}

PONY_API void* pony_alloc_small(pony_ctx_t* ctx, uint32_t sizeclass)
{
  pony_assert(ctx->current != NULL);
  DTRACE2(HEAP_ALLOC, (uintptr_t)ctx->scheduler, HEAP_MIN << sizeclass);

  return ponyint_heap_alloc_small(ctx->current, &ctx->current->heap, sizeclass);
}

PONY_API void* pony_alloc_large(pony_ctx_t* ctx, size_t size)
{
  pony_assert(ctx->current != NULL);
  DTRACE2(HEAP_ALLOC, (uintptr_t)ctx->scheduler, size);

  return ponyint_heap_alloc_large(ctx->current, &ctx->current->heap, size);
}

PONY_API void* pony_realloc(pony_ctx_t* ctx, void* p, size_t size)
{
  pony_assert(ctx->current != NULL);
  DTRACE2(HEAP_ALLOC, (uintptr_t)ctx->scheduler, size);

  return ponyint_heap_realloc(ctx->current, &ctx->current->heap, p, size);
}

PONY_API void* pony_alloc_final(pony_ctx_t* ctx, size_t size)
{
  pony_assert(ctx->current != NULL);
  DTRACE2(HEAP_ALLOC, (uintptr_t)ctx->scheduler, size);

  return ponyint_heap_alloc_final(ctx->current, &ctx->current->heap, size);
}

void* pony_alloc_small_final(pony_ctx_t* ctx, uint32_t sizeclass)
{
  pony_assert(ctx->current != NULL);
  DTRACE2(HEAP_ALLOC, (uintptr_t)ctx->scheduler, HEAP_MIN << sizeclass);

  return ponyint_heap_alloc_small_final(ctx->current, &ctx->current->heap,
    sizeclass);
}

void* pony_alloc_large_final(pony_ctx_t* ctx, size_t size)
{
  pony_assert(ctx->current != NULL);
  DTRACE2(HEAP_ALLOC, (uintptr_t)ctx->scheduler, size);

  return ponyint_heap_alloc_large_final(ctx->current, &ctx->current->heap,
    size);
}

PONY_API void pony_triggergc(pony_ctx_t* ctx)
{
  pony_assert(ctx->current != NULL);
  ctx->current->heap.next_gc = 0;
}

PONY_API void pony_schedule(pony_ctx_t* ctx, pony_actor_t* actor)
{
  if(!has_flag(actor, FLAG_UNSCHEDULED) || ponyint_is_muted(actor))
    return;

  unset_flag(actor, FLAG_UNSCHEDULED);
  ponyint_sched_add(ctx, actor);
}

PONY_API void pony_unschedule(pony_ctx_t* ctx, pony_actor_t* actor)
{
  ((void) ctx);
  
  if(has_flag(actor, FLAG_BLOCKED_SENT))
  {
    // send unblock if we've sent a block
    if(!actor_noblock)
      send_unblock(actor);
  }

  set_flag(actor, FLAG_UNSCHEDULED);
}

PONY_API void pony_become(pony_ctx_t* ctx, pony_actor_t* actor)
{
  ctx->current = actor;
}

PONY_API void ponyint_poll_self()
{
  // can be used by an actor to process more of its messages without giving
  // up control to the scheduler.  Only allow this method if it is called
  // while the actor is already running.  And protect against it being called
  // recurively
  pony_ctx_t* ctx = pony_ctx();
  pony_msg_t* msg;
  pony_actor_t* actor = ctx->current;
  if(actor->running == true && actor->is_polling_self == false) {
	  actor->is_polling_self = true;
	  
	  pony_msg_t* head = atomic_load_explicit(&actor->q.head, memory_order_relaxed);
	  while((msg = ponyint_actor_messageq_pop(&actor->q)) != NULL)
	  {
	    if(handle_message(ctx, actor, msg)) { }
	    if(msg == head)
	      break;
	  }
	  
	  try_gc(ctx, actor);
	  actor->is_polling_self = false;
  }
}

PONY_API void pony_poll(pony_ctx_t* ctx)
{
  // TODO: this seems like it could allow muted actors to get `ponyint_actor_run`
  // which shouldn't be allowed. Fixing might require API changes.
  pony_assert(ctx->current != NULL);
  ponyint_actor_run(ctx, ctx->current, true);
}

PONY_API bool pony_poll_many(pony_ctx_t* ctx)
{
  // For some use cases, we actually want the actor being polled to handle as many
  // messages as they normally would.
  pony_assert(ctx->current != NULL);
  return ponyint_actor_run(ctx, ctx->current, false);
}

void ponyint_actor_setoverloaded(pony_actor_t* actor)
{
  pony_assert(!ponyint_is_cycle(actor));
  set_flag(actor, FLAG_OVERLOADED);
  DTRACE1(ACTOR_OVERLOADED, (uintptr_t)actor);
#ifdef RUNTIME_ANALYSIS
  pony_ctx_t* ctx = pony_ctx();
  if (ctx->analysis_enabled) {
    saveRuntimeAnalyticForActor(ctx, actor, ANALYTIC_OVERLOADED);
  }
#endif
}

bool ponyint_actor_overloaded(pony_actor_t* actor)
{
  return has_flag(actor, FLAG_OVERLOADED);
}

void ponyint_actor_unsetoverloaded(pony_actor_t* actor)
{
  pony_ctx_t* ctx = pony_ctx();
  unset_flag(actor, FLAG_OVERLOADED);
  DTRACE1(ACTOR_OVERLOADED_CLEARED, (uintptr_t)actor);
  if (!has_flag(actor, FLAG_UNDER_PRESSURE))
  {
    ponyint_sched_start_global_unmute(ctx->scheduler->index, actor);
  }
#ifdef RUNTIME_ANALYSIS
  if (ctx->analysis_enabled) {
    saveRuntimeAnalyticForActor(ctx, actor, ANALYTIC_NOT_OVERLOADED);
  }
#endif
}

PONY_API void pony_apply_backpressure()
{
  pony_ctx_t* ctx = pony_ctx();
  set_flag(ctx->current, FLAG_UNDER_PRESSURE);
  DTRACE1(ACTOR_UNDER_PRESSURE, (uintptr_t)ctx->current);
#ifdef RUNTIME_ANALYSIS
  if (ctx->analysis_enabled) {
    saveRuntimeAnalyticForActor(ctx, ctx->current, ANALYTIC_UNDERPRESSURE);
  }
#endif
}

PONY_API void pony_release_backpressure()
{
  pony_ctx_t* ctx = pony_ctx();
  unset_flag(ctx->current, FLAG_UNDER_PRESSURE);
  DTRACE1(ACTOR_PRESSURE_RELEASED, (uintptr_t)ctx->current);
  if (!has_flag(ctx->current, FLAG_OVERLOADED)){
    ponyint_sched_start_global_unmute(ctx->scheduler->index, ctx->current);
  }
#ifdef RUNTIME_ANALYSIS
  if (ctx->analysis_enabled) {
    saveRuntimeAnalyticForActor(ctx, ctx->current, ANALYTIC_NOT_UNDERPRESSURE);
  }
#endif
}

bool ponyint_triggers_muting(pony_actor_t* actor)
{
  return has_flag(actor, FLAG_OVERLOADED) ||
    has_flag(actor, FLAG_UNDER_PRESSURE) ||
    ponyint_is_muted(actor);
}

//
// Mute/Unmute/Check mute status functions
//
// For backpressure related muting and unmuting to work correctly, the following
// rules have to be maintained.
//
// 1. Across schedulers, an actor should never been seen as muted when it is not
// in fact muted.
// 2. It's ok for a muted actor to be seen as unmuted in a transient fashion
// across actors
//
// If rule #1 is violated, we might end up deadlocking because an actor was
// muted for sending to an actor that might never be unmuted (because it isn't
// muted). The actor muted actor would continue to remain muted and the actor
// incorrectly seen as muted became actually muted and then unmuted.
//
// If rule #2 is violated, then a muted actor will receive from 1 to a few
// additional messages and the sender won't be muted. As this is a transient
// situtation that should be shortly rectified, there's no harm done.
//
// Our handling of atomic operations in `ponyint_is_muted` and
// `ponyint_unmute_actor` are to assure that rule #1 isn't violated.
// We have far more relaxed usage of atomics in `ponyint_mute_actor` given the
// far more relaxed rule #2.
//
// An actor's `is_muted` field is effectly a `bool` value. However, by using a
// `uint8_t`, we use the same amount of space that we would for a boolean but
// can use more efficient atomic operations. Given how often these methods are
// called (at least once per message send), efficiency is of primary
// importance.

bool ponyint_is_muted(pony_actor_t* actor)
{
  return (atomic_load_explicit(&actor->is_muted, memory_order_acquire) > 0);
}

void ponyint_mute_actor(pony_actor_t* actor)
{
   uint8_t is_muted = atomic_fetch_add_explicit(&actor->is_muted, 1, memory_order_acq_rel);
   pony_assert(is_muted == 0);
   DTRACE1(ACTOR_MUTED, (uintptr_t)actor);
   (void)is_muted;
   
#ifdef RUNTIME_ANALYSIS
  pony_ctx_t* ctx = pony_ctx();
  if (ctx->analysis_enabled) {
    saveRuntimeAnalyticForActor(ctx, actor, ANALYTIC_MUTE);
  }
#endif
}

void ponyint_unmute_actor(pony_actor_t* actor)
{
  uint8_t is_muted = atomic_fetch_sub_explicit(&actor->is_muted, 1, memory_order_acq_rel);
  pony_assert(is_muted == 1);
  DTRACE1(ACTOR_UNMUTED, (uintptr_t)actor);
  (void)is_muted;
  
#ifdef RUNTIME_ANALYSIS
  pony_ctx_t* ctx = pony_ctx();
  if (ctx->analysis_enabled) {
    saveRuntimeAnalyticForActor(ctx, actor, ANALYTIC_NOT_MUTE);
  }
#endif
}

#ifdef USE_MEMTRACK
size_t ponyint_actor_mem_size(pony_actor_t* actor)
{
  return actor->type->size;
}

size_t ponyint_actor_alloc_size(pony_actor_t* actor)
{
  return ponyint_pool_used_size(actor->type->size);
}

size_t ponyint_actor_total_mem_size(pony_actor_t* actor)
{
  // memeory categories:
  //   used - memory allocated that is actively being used by the runtime
  return
      // actor struct size (maybe this shouldn't be counted to avoid double
      // counting since it is counted as part of the scheduler thread mem used?)
      actor->type->size
      // cycle detector memory used (or 0 if not cycle detector)
    + ( ponyint_is_cycle(actor) ? ponyint_cycle_mem_size(actor) : 0)
      // actor heap memory used
    + ponyint_heap_mem_size(&actor->heap)
      // actor gc total memory used
    + ponyint_gc_total_mem_size(&actor->gc)
      // size of stub message when message_q is initialized
    + sizeof(pony_msg_t);
}

size_t ponyint_actor_total_alloc_size(pony_actor_t* actor)
{
  // memeory categories:
  //   alloc - memory allocated whether it is actively being used or not
  return
      // allocation for actor struct size (maybe this shouldn't be counted to
      // avoid double counting since it is counted as part of the scheduler
      // thread mem allocated?)
      ponyint_pool_used_size(actor->type->size)
      // cycle detector memory allocated (or 0 if not cycle detector)
    + ( ponyint_is_cycle(actor) ? ponyint_cycle_alloc_size(actor) : 0)
      // actor heap memory allocated
    + ponyint_heap_alloc_size(&actor->heap)
      // actor gc total memory allocated
    + ponyint_gc_total_alloc_size(&actor->gc)
      // allocation of stub message when message_q is initialized
    + POOL_ALLOC_SIZE(pony_msg_t);
}
#endif
