#ifdef __linux__
#define _GNU_SOURCE
#endif
#include <platform.h>
#include <string.h>
#include <stdio.h>
#include <sys/time.h>

#include "../pony.h"
#include "../actor/actor.h"
#include "../sched/scheduler.h"
#include "../sched/cpu.h"
#include "../mem/pool.h"
#include "../gc/cycle.h"
#include "../gc/trace.h"
#include "ponyassert.h"

#define UNUSED(x) (void)(x)

// Save timed runtime events and information for later analysis
static FILE * analyticsFile = NULL;
static uint64_t startMilliseconds = 0;

uint64_t timeInMilliseconds() {
	struct timeval timeOfDay;
	gettimeofday(&timeOfDay, NULL);
	return timeOfDay.tv_sec * 1000 + (timeOfDay.tv_usec / 1000);
}

void confirmRuntimeAnalyticHasStarted() {
	// simple CSV file with TIME,ACTOR_TAG,ANALYTIC_EVENT,NUMBER_OF_MESSAGES,BATCH_SIZE,PRIORITY,TO_ACTOR_TAG,TO_ACTOR_NUMBER_OF_MESSAGES
	if (analyticsFile == NULL) {
		analyticsFile = fopen("/tmp/pony.ponyrt_analytics", "w");
		
		fprintf(analyticsFile, "%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n", 
			"TIME_OF_EVENT",
			"ACTOR_A_UUID",
			"ACTOR_A_TAG", 
			"EVENT_NUMBER", 
			"ACTOR_A_NUMBER_OF_MESSAGES", 
			"ACTOR_A_BATCH_SIZE", 
			"ACTOR_A_PRIORITY", 
			"ACTOR_A_HEAP_SIZE", 
			"ACTOR_B_UUID", 
			"ACTOR_B_TAG", 
			"ACTOR_B_NUMBER_OF_MESSAGES"
			);
		
		startMilliseconds = timeInMilliseconds();		
	}
}

void saveRuntimeAnalyticForActorMessage(pony_actor_t * from, pony_actor_t * to, int event) {	
	confirmRuntimeAnalyticHasStarted();
	
	if (analyticsFile != NULL && from != NULL && to != NULL && from->tag != 0 && to->tag != 0) {
		fprintf(analyticsFile, "%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu\n", 
			(unsigned long)(timeInMilliseconds() - startMilliseconds),
			(unsigned long)from->uid,
			(unsigned long)from->tag, 
			(unsigned long)event, 
			(unsigned long)from->q.num_messages,
			(unsigned long)100, //from->batch,
			(unsigned long)0, //from->priority,
			(unsigned long)from->heap.used,
			(unsigned long)to->uid,
			(unsigned long)to->tag,
			(unsigned long)to->q.num_messages
			);
	}
}

void saveRuntimeAnalyticForActor(pony_actor_t * actor, int event) {	
	confirmRuntimeAnalyticHasStarted();
	
	if (analyticsFile != NULL && actor != NULL && actor->tag != 0) {
		fprintf(analyticsFile, "%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,0,0,0\n", 
			// nanoseconds -> milliseconds
			(unsigned long)(timeInMilliseconds() - startMilliseconds),
			(unsigned long)actor->uid,
			(unsigned long)actor->tag, 
			(unsigned long)event, 
			(unsigned long)actor->q.num_messages,
			(unsigned long)100, //actor->batch,
			(unsigned long)0, //actor->priority,
			(unsigned long)actor->heap.used
			);
	}
}

void endRuntimeAnalyticForActor() {
	if (analyticsFile != NULL) {
		fclose(analyticsFile);
	}
}