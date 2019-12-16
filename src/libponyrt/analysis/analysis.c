#ifdef __linux__
#define _GNU_SOURCE
#endif
#include <platform.h>
#include <string.h>
#include <stdio.h>
#include <sys/time.h>

#ifdef PLATFORM_IS_WINDOWS
// TODO: what is needed for windows sleep?
#else
#include <unistd.h>
#endif

#include "../pony.h"
#include "../actor/actor.h"
#include "../sched/scheduler.h"
#include "../sched/cpu.h"
#include "../mem/pool.h"
#include "../mem/alloc.h"
#include "../gc/cycle.h"
#include "../gc/trace.h"
#include "ponyassert.h"

#define UNUSED(x) (void)(x)



typedef struct analysis_msg_t
{
	pony_msg_t msg;
	
	unsigned long timeOfEvent;
	unsigned long fromUID;
	unsigned long fromTag;
	unsigned long eventID;
	unsigned long fromNumMessages;
	unsigned long fromBatch;
	unsigned long fromPriority;
	unsigned long fromHeapUsed;
	
	unsigned long toUID;
	unsigned long toTag;
	unsigned long toNumMessages;
	unsigned long totalMemory;	
	
} analysis_msg_t;


static uint64_t startMilliseconds = 0;
static bool analysisEnabled = false;
static pony_thread_id_t analysisThreadID;
static bool analysisThreadRunning = false;
static messageq_t analysisMessageQueue;

void ponyint_analysis_setanalysis(bool state) {
	analysisEnabled = state;
}

bool ponyint_analysis_getanalysis() {
	return analysisEnabled;
}


uint64_t ponyint_analysis_timeInMilliseconds() {
	struct timeval timeOfDay;
	gettimeofday(&timeOfDay, NULL);
	return timeOfDay.tv_sec * 1000 + (timeOfDay.tv_usec / 1000);
}

DECLARE_THREAD_FN(analysisEventStorageThread)
{
	UNUSED(arg);
	
	// 1. open a file in temp
	// 2. poll the mqm we were given for events
	// 3. write the events to the file
	// 4. end if analysisThreadRunning is false
	
	// Save timed runtime events and information for later analysis
	FILE * analyticsFile = NULL;
	
#ifdef PLATFORM_IS_IOS
	char path[1024];
	snprintf(path, 1024, "%s/pony.ponyrt_analytics", ponyapp_tempDirectory());
	analyticsFile = fopen(path, "w");
#else
	analyticsFile = fopen("/tmp/pony.ponyrt_analytics", "w");
#endif
	
	if (analyticsFile == NULL) {
		analysisEnabled = false;
		analysisThreadRunning = false;
		return NULL;
	}
	
	fprintf(analyticsFile, "%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n", 
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
		"ACTOR_B_NUMBER_OF_MESSAGES",
		"TOTAL_MEMORY"
		);
	
	analysis_msg_t * msg;
	while (analysisThreadRunning) {
		while((msg = (analysis_msg_t*)ponyint_thread_messageq_pop(&analysisMessageQueue
			#ifdef USE_DYNAMIC_TRACE
			, SPECIAL_THREADID_ANALYSIS
			#endif
		)) != NULL)
		{
			
			fprintf(analyticsFile, "%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu\n", 
					msg-> timeOfEvent,
					msg->fromUID,
					msg->fromTag,
					msg->eventID,
					msg->fromNumMessages,
					msg->fromBatch,
					msg->fromPriority,
					msg->fromHeapUsed,
					msg->toUID,
					msg->toTag,
					msg->toNumMessages,
					msg->totalMemory
				);
		}
		
#ifdef PLATFORM_IS_WINDOWS
  Sleep(5000);
#else
  usleep(5000);
#endif
	}
	
	fclose(analyticsFile);
	
	return NULL;
}

void saveRuntimeAnalyticForActorMessage(pony_actor_t * from, pony_actor_t * to, int event) {
	if (analysisEnabled == false) {
		return;
	}
		
	if (analysisThreadRunning && from != NULL && to != NULL && from->tag != 0 && to->tag != 0 && from->tag != to->tag) {
		
	    analysis_msg_t * msg = (analysis_msg_t*) pony_alloc_msg(POOL_INDEX(sizeof(analysis_msg_t)), 0);
		msg->timeOfEvent = (unsigned long)(ponyint_analysis_timeInMilliseconds() - startMilliseconds);
		msg->fromUID = from->uid;
		msg->fromTag = from->tag;
		msg->eventID = event;
		msg->fromNumMessages = (unsigned long)from->q.num_messages;
		msg->fromBatch = 0;
		msg->fromPriority = 0;
		msg->fromHeapUsed = from->heap.used;
		msg->toUID = to->uid;
		msg->toTag = to->tag;
		msg->toNumMessages = (unsigned long)to->q.num_messages;
		msg->totalMemory = ponyint_total_memory();
		
		// if we're overloading the save-to-file thread, slow down a little
		if (analysisMessageQueue.num_messages > 100000) {
			usleep(5000);
		}
			
		ponyint_thread_messageq_push(&analysisMessageQueue, (pony_msg_t*)msg, (pony_msg_t*)msg
		#ifdef USE_DYNAMIC_TRACE
					, SPECIAL_THREADID_ANALYSIS, SPECIAL_THREADID_ANALYSIS
		#endif
	      );
	}
}

void saveRuntimeAnalyticForActor(pony_actor_t * actor, int event) {
	if (analysisEnabled == false) {
		return;
	}
		
	if (analysisThreadRunning && actor != NULL && actor->tag != 0) {
		
	    analysis_msg_t * msg = (analysis_msg_t*) pony_alloc_msg(POOL_INDEX(sizeof(analysis_msg_t)), 0);
		msg->timeOfEvent = (unsigned long)(ponyint_analysis_timeInMilliseconds() - startMilliseconds);
		msg->fromUID = actor->uid;
		msg->fromTag = actor->tag;
		msg->eventID = event;
		msg->fromNumMessages = (unsigned long)actor->q.num_messages;
		msg->fromBatch = 0;
		msg->fromPriority = 0;
		msg->fromHeapUsed = actor->heap.used;
		msg->toUID = 0;
		msg->toTag = 0;
		msg->toNumMessages = 0;
		msg->totalMemory = ponyint_total_memory();
		
		// if we're overloading the save-to-file thread, slow down a little
		if (analysisMessageQueue.num_messages > 100000) {
			usleep(5000);
		}
			
		ponyint_thread_messageq_push(&analysisMessageQueue, (pony_msg_t*)msg, (pony_msg_t*)msg
		#ifdef USE_DYNAMIC_TRACE
					, SPECIAL_THREADID_ANALYSIS, SPECIAL_THREADID_ANALYSIS
		#endif
	      );

	}
}


void startRuntimeAnalyticForActor() {
    
    if (analysisEnabled == false) {
        return;
    }
    
    // We use a thread to not slow down the rest of the pony program with storing the events to disk
    if (analysisThreadRunning == false) {
        analysisThreadRunning = true;
        
        startMilliseconds = ponyint_analysis_timeInMilliseconds();
        
        ponyint_messageq_init(&analysisMessageQueue);
        fprintf(stderr, "starting analysis thread\n");
        if(!ponyint_thread_create(&analysisThreadID, analysisEventStorageThread, -1, NULL)) {
            analysisThreadRunning = false;
            analysisEnabled = false;
        }
    }
}

void endRuntimeAnalyticForActor() {
	
	if (analysisThreadRunning) {
		analysisThreadRunning = false;
		ponyint_thread_join(analysisThreadID);
		
		ponyint_messageq_destroy(&analysisMessageQueue);
	}
}
