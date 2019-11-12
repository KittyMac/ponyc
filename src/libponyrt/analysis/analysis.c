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

void saveRuntimeAnalyticForActor(pony_actor_t * actor, int event) {	
	// simple CSV file with TIME,ACTOR_TAG,ANALYTIC_EVENT,NUMBER_OF_MESSAGES,BATCH_SIZE,PRIORITY
	if (analyticsFile == NULL) {
		analyticsFile = fopen("/tmp/pony.analytics", "w");
		startMilliseconds = timeInMilliseconds();		
	}
	if (analyticsFile != NULL && actor->tag != 0) {
		fprintf(analyticsFile, "%lu,%lu,%lu,%lu,%lu,%lu\n", 
			// nanoseconds -> milliseconds
			(unsigned long)(timeInMilliseconds() - startMilliseconds),
			(unsigned long)actor->tag, 
			(unsigned long)event, 
			(unsigned long)actor->q.numMessages,
			(unsigned long)actor->batch,
			(unsigned long)actor->priority);
	}
}

void endRuntimeAnalyticForActor() {
	if (analyticsFile != NULL) {
		fclose(analyticsFile);
	}
}