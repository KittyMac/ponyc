#ifdef __linux__
#define _GNU_SOURCE
#endif
#include <platform.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
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

#ifdef PLATFORM_IS_IOS
extern const char * ponyapp_tempDirectory();
#endif

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
static pony_thread_id_t analysisThreadID;
static bool analysisThreadRunning = false;
static messageq_t analysisMessageQueue;

static uint64_t findTopActorsInValues(uint64_t * values, uint64_t num_values, uint64_t * results, uint64_t num_results, uint64_t * total_count);

uint64_t ponyint_analysis_timeInMilliseconds() {
	return (ponyint_cpu_tick() / 1000 / 1000);
}

void sigintHandler(int x)
{
    // we want to print out runtime stats before exiting, so
	// call stopRuntimeAnalysis() then exit.
	stopRuntimeAnalysis();
	exit(x);
}

DECLARE_THREAD_FN(analysisEventStorageThread)
{
	uint32_t analysis_enabled = *((uint32_t*)arg);

	// 1 - just gather statistics while running
	// 2 - save all events to tmp file for other tools to use
	FILE * analyticsFile = NULL;

	if (analysis_enabled > 1) {
#ifdef PLATFORM_IS_IOS
		char path[1024];
		snprintf(path, 1024, "%s/pony.ponyrt_analytics", ponyapp_tempDirectory());
		analyticsFile = fopen(path, "w");
#else
		analyticsFile = fopen("/tmp/pony.ponyrt_analytics", "w");
#endif
		if (analyticsFile == NULL) {
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
	}
	
	
	// runtime analysis:
	// 1. keep track of up to 16,384 individual actors (UUIDs)
	// 2. report which actors get overloaded the most (causing other actors to mute)
	// 3. report the actors who use the most memory
	const uint32_t kMaxActors = 16384;
	const uint64_t kMaxCount = 0xFFFFFFFFFFFFFFFF;
	uint64_t * overload_counts = ponyint_pool_alloc_size(kMaxActors * sizeof(uint64_t));
	uint64_t * gc_counts = ponyint_pool_alloc_size(kMaxActors * sizeof(uint64_t));
	uint64_t * actor_tags = ponyint_pool_alloc_size(kMaxActors * sizeof(uint64_t));
	uint64_t * memory_max = ponyint_pool_alloc_size(kMaxActors * sizeof(uint64_t));
	
	uint64_t * pressure_total = ponyint_pool_alloc_size(kMaxActors * sizeof(uint64_t));
	uint64_t * pressure_start = ponyint_pool_alloc_size(kMaxActors * sizeof(uint64_t));
	
	uint64_t * muted_total = ponyint_pool_alloc_size(kMaxActors * sizeof(uint64_t));
	uint64_t * muted_start = ponyint_pool_alloc_size(kMaxActors * sizeof(uint64_t));
		
	bool has_overloaded = false;
	bool has_muted = false;
	bool has_pressure = false;
	bool has_gc = false;
	
	memset(overload_counts, 0, kMaxActors * sizeof(uint64_t));
	memset(gc_counts, 0, kMaxActors * sizeof(uint64_t));
	memset(actor_tags, 0, kMaxActors * sizeof(uint64_t));
	memset(memory_max, 0, kMaxActors * sizeof(uint64_t));
	
	memset(pressure_total, 0, kMaxActors * sizeof(uint64_t));
	memset(pressure_start, 0, kMaxActors * sizeof(uint64_t));
	
	memset(muted_total, 0, kMaxActors * sizeof(uint64_t));
	memset(muted_start, 0, kMaxActors * sizeof(uint64_t));
	
	// exit this thread gracefully when the program is terminated so
	// we can see the results of the analysis
	signal(SIGINT, sigintHandler);
	
	analysis_msg_t * msg;
	static messageq_t * local_analysisMessageQueuePtr = &analysisMessageQueue;
	while (true) {
		
		while((msg = (analysis_msg_t*)ponyint_thread_messageq_pop(local_analysisMessageQueuePtr
			#ifdef USE_DYNAMIC_TRACE
			, SPECIAL_THREADID_ANALYSIS
			#endif
		)) != NULL)
		{
			
			if (analysis_enabled > 1 && msg->fromTag != 0 && msg->toTag != 0) {
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
			
			if (msg->fromUID < kMaxActors) {
				actor_tags[msg->fromUID] = msg->fromTag;
				if (msg->eventID == ANALYTIC_OVERLOADED && overload_counts[msg->fromUID] < kMaxCount) { 
					overload_counts[msg->fromUID] += 1;
					has_overloaded = true;
				}
				if (msg->eventID == ANALYTIC_GC_RAN && gc_counts[msg->fromUID] < kMaxCount) { 
					gc_counts[msg->fromUID] += 1;
					has_gc = true;
				}
				if (msg->eventID == ANALYTIC_MUTE) { 
					muted_start[msg->fromUID] = ponyint_cpu_tick() / 1000000;
					has_muted = true;
				}
				if (msg->eventID == ANALYTIC_NOT_MUTE) { 
					muted_total[msg->fromUID] += (ponyint_cpu_tick() / 1000000) - muted_start[msg->fromUID];
					muted_start[msg->fromUID] = 0;
				}
				if (msg->eventID == ANALYTIC_UNDERPRESSURE) { 
					pressure_start[msg->fromUID] = ponyint_cpu_tick() / 1000000;
					has_pressure = true;
				}
				if (msg->eventID == ANALYTIC_NOT_UNDERPRESSURE) { 
					pressure_total[msg->fromUID] += (ponyint_cpu_tick() / 1000000) - pressure_start[msg->fromUID];
					pressure_start[msg->fromUID] = 0;
				}
				if (memory_max[msg->fromUID] < msg->fromHeapUsed) { 
					memory_max[msg->fromUID] = msg->fromHeapUsed;
				}
			}
			
		}
		
		// continue until we've written all of our analytics events
		if (analysisThreadRunning == false && local_analysisMessageQueuePtr->num_messages == 0) {
			break;
		}
		
#ifdef PLATFORM_IS_WINDOWS
  	  Sleep(5000);
#else
      usleep(5000);
#endif
	}
	
	if (analysis_enabled > 1) {
		fclose(analyticsFile);
	}
	
	
	
	// complete time for any waiting muted or pressure actors
	for(unsigned int i = 0; i < kMaxActors; i++) {
		if (pressure_start[i] > 0) { 
			pressure_total[i] += (ponyint_cpu_tick() / 1000000) - pressure_start[i];
		}
		if (muted_start[i] > 0) { 
			muted_total[i] += (ponyint_cpu_tick() / 1000000) - muted_start[i];
		}
	}
	
	
    char * resetColor = "\x1B[0m";
	char * redColor = "\x1B[91m";
	char * greenColor =  "\x1B[92m";
	char * lightGreyColor =  "\x1B[37m";
	char * darkGreyColor =  "\x1B[90m";
	char * orangeColor =  "\x1B[31m";
	
	const int kActorNameStart = 9000;
	const int kActorNameEnd = 9010;
	char * builtinActorNames[] = {
		"Timers",
		"Promise",
		"ProcessMonitor",
		"UDPSocket",
		"TCPListener",
		"TCPConnection",
		"FileStream",
		"Registrar",
		"Custodian",
		"Stdin",
		"StdStream"
		};
	
	// print out our analysis to the console
	// 1. overloaded actors
	if (has_overloaded || has_muted || has_pressure || has_gc) {
		const int kMaxActorsPerReport = 10;
		
		// determine if we have any untagged actors
		bool hasUntaggedActors = false;
		for(unsigned int i = 0; i < kMaxActors; i++) {
			if ( actor_tags[i] == 0 && (pressure_total[i] > 0 || muted_total[i] > 0 || overload_counts[i] > 0)) {
				hasUntaggedActors = true;
				break;
			}
		}
				
		fprintf(stderr, "\n\n%s**** Pony Runtime Analysis: ISSUES FOUND%s\n\n", redColor, resetColor);
		if (has_overloaded) {
			uint64_t max_actors[kMaxActorsPerReport] = {0};
			uint64_t total_reported = 0;
			uint64_t max_reported = findTopActorsInValues(overload_counts, kMaxActors, max_actors, kMaxActorsPerReport, &total_reported);
			for(uint64_t v = 0; v < max_reported; v++) {
				uint64_t i = max_actors[v];
				if (actor_tags[i] == 0) {
					fprintf(stderr, "%sactor [untagged] was overloaded %llu times%s\n", orangeColor, overload_counts[i], resetColor);
				} else {
					if (actor_tags[i] >= kActorNameStart && actor_tags[i] <= kActorNameEnd) {
						fprintf(stderr, "%sactor %s was overloaded %llu times%s\n", orangeColor, builtinActorNames[actor_tags[i] - kActorNameStart], overload_counts[i], resetColor);
					} else {
						fprintf(stderr, "%sactor tag %llu was overloaded %llu times%s\n", orangeColor, actor_tags[i], overload_counts[i], resetColor);
					}
				}
			}
			if (total_reported > max_reported) {
				fprintf(stderr, "%s... %llu other actors were also overloaded%s\n", darkGreyColor, total_reported-max_reported, resetColor);
			}
			fprintf(stderr, "\n");
			
			fprintf(stderr, "%sOverloaded actors are bottlenecks in your actor network. Bottlenecks will\n", darkGreyColor);
			fprintf(stderr, "cause other actors to mute (stop processing until the bottleneck is clear).\n");
			fprintf(stderr, "You should indentify and remove bottlenecks from your actor network. For the\n");
			fprintf(stderr, "best performance possible actors should never become overloaded.%s\n\n", resetColor);
			
			fprintf(stderr, "\n");
		}
	
		if (has_muted) {
			uint64_t max_actors[kMaxActorsPerReport] = {0};
			uint64_t total_reported = 0;
			uint64_t max_reported = findTopActorsInValues(muted_total, kMaxActors, max_actors, kMaxActorsPerReport, &total_reported);
			for(uint64_t v = 0; v < max_reported; v++) {
				uint64_t i = max_actors[v];
				if (actor_tags[i] == 0) {
					fprintf(stderr, "%sactor [untagged] was muted for %llu ms%s\n", orangeColor, muted_total[i], resetColor);
				} else {
					if (actor_tags[i] >= kActorNameStart && actor_tags[i] <= kActorNameEnd) {
						fprintf(stderr, "%sactor %s was muted for %llu ms%s\n", orangeColor, builtinActorNames[actor_tags[i] - kActorNameStart], muted_total[i], resetColor);
					} else {
						fprintf(stderr, "%sactor tag %llu was muted for %llu ms%s\n", orangeColor, actor_tags[i], muted_total[i], resetColor);
					}
				}
			}
			if (total_reported > max_reported) {
				fprintf(stderr, "%s... %llu other actors were also muted%s\n", lightGreyColor, total_reported-max_reported, resetColor);
			}
			fprintf(stderr, "\n");
			
			fprintf(stderr, "%sMuted actors have work to do but are restricted from processing because\n", darkGreyColor);
			fprintf(stderr, "the actor they are sending messages to is overloaded. These results are the\n");
			fprintf(stderr, "amount of time your actors spent waiting while muted. For the best performance\n");
			fprintf(stderr, "possible actors should never mute.%s\n\n", resetColor);
		}
		
		if (has_pressure) {
			uint64_t max_actors[kMaxActorsPerReport] = {0};
			uint64_t total_reported = 0;
			uint64_t max_reported = findTopActorsInValues(pressure_total, kMaxActors, max_actors, kMaxActorsPerReport, &total_reported);
			for(uint64_t v = 0; v < max_reported; v++) {
				uint64_t i = max_actors[v];
				if (actor_tags[i] == 0) {
					fprintf(stderr, "%sactor [untagged] was under pressure for %llu ms%s\n", orangeColor, pressure_total[i], resetColor);
				} else {
					if (actor_tags[i] >= kActorNameStart && actor_tags[i] <= kActorNameEnd) {
						fprintf(stderr, "%sactor %s was under pressure for %llu ms%s\n", orangeColor, builtinActorNames[actor_tags[i] - kActorNameStart], pressure_total[i], resetColor);
					} else {
						fprintf(stderr, "%sactor tag %llu was under pressure for %llu ms%s\n", orangeColor, actor_tags[i], pressure_total[i], resetColor);
					}
				}
			}
			if (total_reported > max_reported) {
				fprintf(stderr, "%s... %llu other actors were also under pressure%s\n", lightGreyColor, total_reported-max_reported, resetColor);
			}
			fprintf(stderr, "\n");
			
			fprintf(stderr, "%sActors who are under pressure are making use of the built-in backpressure system.\n", darkGreyColor);
			fprintf(stderr, "As this is a opt-in system, this is just to inform you that backpressure is being\n");
			fprintf(stderr, "applied and the amount of time your actors are stopped due to it.%s\n\n", resetColor);
		}
		
		if (has_gc) {
			uint64_t max_actors[kMaxActorsPerReport] = {0};
			uint64_t total_reported = 0;
			uint64_t max_reported = findTopActorsInValues(memory_max, kMaxActors, max_actors, kMaxActorsPerReport, &total_reported);
			float to_mb = (1024 * 1024);
			for(uint64_t v = 0; v < max_reported; v++) {
				uint64_t i = max_actors[v];
				if (actor_tags[i] == 0) {
					fprintf(stderr, "%sactor [untagged] garbage collected %llu times and had a max heap size was %0.2f MB%s\n", orangeColor, gc_counts[i], memory_max[i] / to_mb, resetColor);
				} else {
					if (actor_tags[i] >= kActorNameStart && actor_tags[i] <= kActorNameEnd) {
						fprintf(stderr, "%sactor %s garbage collected %llu times and had a max heap size was %0.2f MB%s\n", orangeColor, builtinActorNames[actor_tags[i] - kActorNameStart], gc_counts[i], memory_max[i] / to_mb, resetColor);
					} else {
						fprintf(stderr, "%sactor tag %llu garbage collected %llu times and had a max heap size was %0.2f MB%s\n", orangeColor, actor_tags[i], gc_counts[i], memory_max[i] / to_mb, resetColor);
					}
				}
			}
			if (total_reported > max_reported) {
				fprintf(stderr, "%s... %llu other actors also garbage collected%s\n", darkGreyColor, total_reported-max_reported, resetColor);
			}
			fprintf(stderr, "\n");
			
			fprintf(stderr, "%sLarge amounts of garbage collection in Pony can mean two things\n", darkGreyColor);
			fprintf(stderr, "1. Time is spent garbage collecting when it could be spent elsewhere\n");
			fprintf(stderr, "2. Garbage collection of data shared between actors requires messaging between actors to resolve\n");
			fprintf(stderr, "Reducing the amount of transitory memory allocations in critical paths can result\n");
			fprintf(stderr, "in performance gains%s\n\n", resetColor);
			
			fprintf(stderr, "\n");
		}
		
		if (hasUntaggedActors) {
			fprintf(stderr, "\n\n%sNote: You can tag actors to associate results with specific types of actors.\n", darkGreyColor);
			fprintf(stderr, "actor Foo\n");
			fprintf(stderr, "  fun _tag():USize => 2%s\n\n", resetColor);
		}
		
	}else{
		fprintf(stderr, "\n\n%s**** Pony Runtime Analysis: PASS%s\n\n", greenColor, resetColor);
	}
	
	return NULL;
}

uint64_t findTopActorsInValues(uint64_t * values, uint64_t num_values, uint64_t * results, uint64_t num_results, uint64_t * total_count) {
	// find the top n indices and store them in results
	uint64_t num_found = 0;
	for (uint64_t i = 0; i < num_results; i++) {
		uint64_t new_max = 0;
		uint64_t new_max_idx = 0;
		bool new_value_found = false;
		
		// for each value in values
		for (uint64_t v = 0; v < num_values; v++) {
			if (values[v] > 0 && values[v] > new_max) {
				// ensure this one hasn't been identified yet
				bool duplicate = false;
				for(uint64_t j = 0; j < i; j++) {
					if (results[j] == v) {
						duplicate = true;
						break;
					}
				}
				if (!duplicate) {
					new_max = values[v];
					new_max_idx = v;
					new_value_found = true;
				}
			}
		}
		
		if (new_value_found) {
			results[i] = new_max_idx;
			num_found++;
		} else {
			break;
		}
	}
	
	uint64_t local_count = 0;
	for (uint64_t v = 0; v < num_values; v++) {
		if (values[v] > 0) {
			local_count++;
		}
	}
	*total_count = local_count;
		
	return num_found;
}

void saveRuntimeAnalyticForActorMessage(pony_ctx_t * ctx, pony_actor_t * from, pony_actor_t * to, int event) {
	if (ctx->analysis_enabled <= 1) {
		return;
	}
	
	if (analysisThreadRunning && from != NULL && to != NULL && from->tag != 0 && to->tag != 0 && from->tag != to->tag) {
		
	    analysis_msg_t * msg = (analysis_msg_t*) pony_alloc_msg(POOL_INDEX(sizeof(analysis_msg_t)), 0);
		msg->timeOfEvent = (unsigned long)(ponyint_analysis_timeInMilliseconds() - startMilliseconds);
		msg->fromUID = from->uid;
		msg->fromTag = from->tag;
		msg->eventID = event;
		msg->fromNumMessages = (unsigned long)from->q.num_messages;
		msg->fromBatch = from->batch;
		msg->fromPriority = from->priority;
		msg->fromHeapUsed = from->heap.used;
		msg->toUID = to->uid;
		msg->toTag = to->tag;
		msg->toNumMessages = (unsigned long)to->q.num_messages;
		msg->totalMemory = ponyint_total_memory();
		
		// if we're overloading the save-to-file thread, slow down a little
		if (analysisMessageQueue.num_messages > 100000) {
#ifdef PLATFORM_IS_WINDOWS
			Sleep(50);
#else
			usleep(50);
#endif
		}
		
		ponyint_thread_messageq_push(&analysisMessageQueue, (pony_msg_t*)msg, (pony_msg_t*)msg
		#ifdef USE_DYNAMIC_TRACE
					, SPECIAL_THREADID_ANALYSIS, SPECIAL_THREADID_ANALYSIS
		#endif
	      );
	}
}

void saveRuntimeAnalyticForActor(pony_ctx_t * ctx, pony_actor_t * actor, int event) {
	if (ctx->analysis_enabled == 0) {
		return;
	}
	
	// level 1 analysis only cares about these events
	if ( ctx->analysis_enabled == 1 && 
		(event != ANALYTIC_OVERLOADED && event != ANALYTIC_MUTE && event != ANALYTIC_NOT_MUTE && event != ANALYTIC_UNDERPRESSURE && event != ANALYTIC_NOT_UNDERPRESSURE && event != ANALYTIC_GC_RAN)) {
		return;
	}
	
	// level 2 analysis only cares about events with a tag
	if ( ctx->analysis_enabled == 2 && 
		 (event != ANALYTIC_OVERLOADED || event != ANALYTIC_MUTE || event != ANALYTIC_NOT_MUTE || event != ANALYTIC_UNDERPRESSURE || event != ANALYTIC_NOT_UNDERPRESSURE || event != ANALYTIC_GC_RAN) &&
		 actor->tag == 0) {
		return;
	}
		
	if (analysisThreadRunning && actor != NULL) {
		
		// Note: the level 1 analytics should work with untagged actors. The level 2 anaylics should
		// only work with tagged actors. Therefor we want to filter which events we actually send 
		// over to be counted to keep down unnecessary work.
		
	    analysis_msg_t * msg = (analysis_msg_t*) pony_alloc_msg(POOL_INDEX(sizeof(analysis_msg_t)), 0);
		msg->timeOfEvent = (unsigned long)(ponyint_analysis_timeInMilliseconds() - startMilliseconds);
		msg->fromUID = actor->uid;
		msg->fromTag = actor->tag;
		msg->eventID = event;
		msg->fromNumMessages = (unsigned long)actor->q.num_messages;
		msg->fromBatch = actor->batch;
		msg->fromPriority = actor->priority;
		msg->fromHeapUsed = actor->heap.used;
		msg->toUID = 0;
		msg->toTag = 0;
		msg->toNumMessages = 0;
		msg->totalMemory = ponyint_total_memory();
		
		// if we're overloading the events thread, slow down a little
		if (analysisMessageQueue.num_messages > 100000) {
			usleep(50);
		}
		
		ponyint_thread_messageq_push(&analysisMessageQueue, (pony_msg_t*)msg, (pony_msg_t*)msg
		#ifdef USE_DYNAMIC_TRACE
					, SPECIAL_THREADID_ANALYSIS, SPECIAL_THREADID_ANALYSIS
		#endif
	      );

	}
}


void startRuntimeAnalysis(pony_ctx_t * ctx) {
    
    if (ctx->analysis_enabled == 0) {
        return;
    }
    
    // We use a thread to not slow down the rest of the pony program with storing the events to disk
    if (analysisThreadRunning == false) {
        analysisThreadRunning = true;
        
        startMilliseconds = ponyint_analysis_timeInMilliseconds();
        
        ponyint_messageq_init(&analysisMessageQueue);
        if(!ponyint_thread_create(&analysisThreadID, analysisEventStorageThread, -1, &(ctx->analysis_enabled))) {
            analysisThreadRunning = false;
            ctx->analysis_enabled = 0;
        }
    }
}

void stopRuntimeAnalysis() {
	
	if (analysisThreadRunning) {
		analysisThreadRunning = false;
		ponyint_thread_join(analysisThreadID);
		
		ponyint_messageq_destroy(&analysisMessageQueue);
	}
}
