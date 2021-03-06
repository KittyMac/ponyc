#ifndef analysis_h
#define analysis_h

#include "../gc/gc.h"
#include "../mem/heap.h"
#include "../pony.h"
#include <stdint.h>
#include <stdbool.h>
#ifndef __cplusplus
#  include <stdalign.h>
#endif
#include <platform.h>

PONY_EXTERN_C_BEGIN
  
enum {
  ANALYTIC_MUTE = 1,
  ANALYTIC_NOT_MUTE = 2,
  ANALYTIC_OVERLOADED = 3,
  ANALYTIC_NOT_OVERLOADED = 4,
  ANALYTIC_UNDERPRESSURE = 5,
  ANALYTIC_NOT_UNDERPRESSURE = 6,
  ANALYTIC_RUN_START = 7,
  ANALYTIC_RUN_END = 8,
  ANALYTIC_PRIORITY_RESCHEDULE = 9,
  ANALYTIC_MESSAGE_SENT = 10,
  ANALYTIC_APP_MESSAGE_SENT = 11,
  ANALYTIC_GC_START = 12,
  ANALYTIC_GC_END = 13,
  ANALYTIC_ACTOR_DESTROYED = 14
};

#define RUNTIME_ANALYSIS 1

extern void saveRuntimeAnalyticForActor(pony_ctx_t * ctx, pony_actor_t * actor, int event);
extern void saveRuntimeAnalyticForActorMessage(pony_ctx_t * ctx, pony_actor_t * from, pony_actor_t * to, int event);
extern void startRuntimeAnalysis(pony_ctx_t * ctx);
extern void stopRuntimeAnalysis(bool killed);

PONY_EXTERN_C_END

#endif
