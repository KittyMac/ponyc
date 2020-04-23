#ifdef __linux__
#define _GNU_SOURCE
#endif
#include <platform.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <sys/time.h>
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
  
  uint64_t timeOfEvent;
  uint64_t fromUID;
  uint64_t fromTag;
  uint64_t eventID;
  uint64_t fromNumMessages;
  uint64_t fromBatch;
  uint64_t fromPriority;
  uint64_t fromHeapUsed;
  
  uint64_t toUID;
  uint64_t toTag;
  uint64_t toNumMessages;
  uint64_t totalMemory;
  
} analysis_msg_t;

static uint64_t startNanoseconds = 0;
static pony_thread_id_t analysisThreadID;
static bool analysisThreadRunning = false;
static bool analysisThreadWasKilledUnexpectedly = false;
static messageq_t analysisMessageQueue;

static uint64_t findTopActorsInValues(uint64_t * values, uint64_t num_values, uint64_t * results, uint64_t num_results, uint64_t * total_count);

#define NANO_TO_MILLI(x) ((x) / 1000000)

uint64_t ponyint_analysis_timeInNanoseconds() {
  return ponyint_cpu_tick();
}

void sigintHandler(int x)
{
  // we want to print out runtime stats before exiting, so
  // call stopRuntimeAnalysis() then exit.
  stopRuntimeAnalysis(true);
  exit(x);
}

DECLARE_THREAD_FN(analysisEventStorageThread)
{
  uint32_t analysis_enabled = *((uint32_t*)arg);
  
#ifndef PLATFORM_IS_IOS
  // 1 - just gather statistics while running
  // 2 - save all events to tmp file for other tools to use
  FILE * analyticsFile = NULL;
  
  if (analysis_enabled > 1) {
    analyticsFile = fopen("/tmp/pony.ponyrt_analytics", "w");

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
#else
  ((void)analysis_enabled);
#endif
  
  
  // runtime analysis:
  // 1. keep track of up to 16,384 individual actors (UUIDs)
  // 2. report which actors get overloaded the most (causing other actors to mute)
  // 3. report the actors who use the most memory
  const uint32_t kMaxActors = 16384;
  const uint64_t kMaxCount = 0xFFFFFFFFFFFFFFFF;
  uint64_t * overload_counts = ponyint_pool_alloc_size(kMaxActors * sizeof(uint64_t));
  uint64_t * destroyed_flags = ponyint_pool_alloc_size(kMaxActors * sizeof(uint64_t));
  uint64_t * last_msg_count = ponyint_pool_alloc_size(kMaxActors * sizeof(uint64_t));
  uint64_t * actor_tags = ponyint_pool_alloc_size(kMaxActors * sizeof(uint64_t));
  uint64_t * memory_max = ponyint_pool_alloc_size(kMaxActors * sizeof(uint64_t));
  
  uint64_t * pressure_total = ponyint_pool_alloc_size(kMaxActors * sizeof(uint64_t));
  uint64_t * pressure_start = ponyint_pool_alloc_size(kMaxActors * sizeof(uint64_t));
  
  uint64_t * muted_total = ponyint_pool_alloc_size(kMaxActors * sizeof(uint64_t));
  uint64_t * muted_start = ponyint_pool_alloc_size(kMaxActors * sizeof(uint64_t));
  
  uint64_t * gc_counts = ponyint_pool_alloc_size(kMaxActors * sizeof(uint64_t));
  uint64_t * gc_total = ponyint_pool_alloc_size(kMaxActors * sizeof(uint64_t));
  uint64_t * gc_start = ponyint_pool_alloc_size(kMaxActors * sizeof(uint64_t));
  
  uint64_t * run_counts = ponyint_pool_alloc_size(kMaxActors * sizeof(uint64_t));
  uint64_t * run_total = ponyint_pool_alloc_size(kMaxActors * sizeof(uint64_t));
  uint64_t * run_start = ponyint_pool_alloc_size(kMaxActors * sizeof(uint64_t));
  
  uint64_t total_app_messages = 0;
  uint64_t total_system_messages = 0;
    
  bool has_overloaded = false;
  bool has_muted = false;
  bool has_pressure = false;
  bool has_gc = false;
  
  memset(overload_counts, 0, kMaxActors * sizeof(uint64_t));
  memset(destroyed_flags, 0, kMaxActors * sizeof(uint64_t));
  memset(last_msg_count, 0, kMaxActors * sizeof(uint64_t));
  memset(actor_tags, 0, kMaxActors * sizeof(uint64_t));
  memset(memory_max, 0, kMaxActors * sizeof(uint64_t));
  
  memset(pressure_total, 0, kMaxActors * sizeof(uint64_t));
  memset(pressure_start, 0, kMaxActors * sizeof(uint64_t));
  
  memset(muted_total, 0, kMaxActors * sizeof(uint64_t));
  memset(muted_start, 0, kMaxActors * sizeof(uint64_t));
  
  memset(gc_counts, 0, kMaxActors * sizeof(uint64_t));
  memset(gc_total, 0, kMaxActors * sizeof(uint64_t));
  memset(gc_start, 0, kMaxActors * sizeof(uint64_t));
  
  memset(run_counts, 0, kMaxActors * sizeof(uint64_t));
  memset(run_total, 0, kMaxActors * sizeof(uint64_t));
  memset(run_start, 0, kMaxActors * sizeof(uint64_t));
  
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
      
#ifndef PLATFORM_IS_IOS
      if (analysis_enabled > 1 && msg->fromTag != 0 && msg->toTag != 0) {
        fprintf(analyticsFile, "%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu\n", 
            (unsigned long)(msg->timeOfEvent / 1000000),
            (unsigned long)msg->fromUID,
            (unsigned long)msg->fromTag,
            (unsigned long)msg->eventID,
            (unsigned long)msg->fromNumMessages,
            (unsigned long)msg->fromBatch,
            (unsigned long)msg->fromPriority,
            (unsigned long)msg->fromHeapUsed,
            (unsigned long)msg->toUID,
            (unsigned long)msg->toTag,
            (unsigned long)msg->toNumMessages,
            (unsigned long)msg->totalMemory
          );
      }
#endif
      
      if (msg->eventID == ANALYTIC_MESSAGE_SENT) {
        total_system_messages++;
      }
      if (msg->eventID == ANALYTIC_APP_MESSAGE_SENT) {
        total_app_messages++;
      }
      
      
      if (msg->fromUID < kMaxActors) {
        actor_tags[msg->fromUID] = msg->fromTag;
        destroyed_flags[msg->fromUID] = 1;
        last_msg_count[msg->fromUID] = msg->fromNumMessages;
        last_msg_count[msg->toUID] = msg->toNumMessages;
        
        if (msg->eventID == ANALYTIC_OVERLOADED && overload_counts[msg->fromUID] < kMaxCount) { 
          overload_counts[msg->fromUID] += 1;
          has_overloaded = true;
        }
        
        if (msg->eventID == ANALYTIC_RUN_START) {
          if(run_counts[msg->fromUID] < kMaxCount) {
            run_counts[msg->fromUID] += 1;
          }
          run_start[msg->fromUID] = msg->timeOfEvent;
        }
        if (msg->eventID == ANALYTIC_RUN_END && run_start[msg->fromUID] != 0) { 
          run_total[msg->fromUID] += msg->timeOfEvent - run_start[msg->fromUID];
          run_start[msg->fromUID] = 0;
        }

        if (msg->eventID == ANALYTIC_GC_START) {
          if(gc_counts[msg->fromUID] < kMaxCount) {
            gc_counts[msg->fromUID] += 1;
          }
          gc_start[msg->fromUID] = msg->timeOfEvent;
          has_gc = true;
        }
        if (msg->eventID == ANALYTIC_GC_END && gc_start[msg->fromUID] != 0) { 
          gc_total[msg->fromUID] += msg->timeOfEvent - gc_start[msg->fromUID];
          gc_start[msg->fromUID] = 0;
        }
        
        if (msg->eventID == ANALYTIC_ACTOR_DESTROYED) {
          destroyed_flags[msg->fromUID] = 2;
        }
        if (msg->eventID == ANALYTIC_MUTE) { 
          muted_start[msg->fromUID] = msg->timeOfEvent;
          has_muted = true;
        }
        if (msg->eventID == ANALYTIC_NOT_MUTE && muted_start[msg->fromUID] != 0) { 
          muted_total[msg->fromUID] += msg->timeOfEvent - muted_start[msg->fromUID];
          muted_start[msg->fromUID] = 0;
        }
        if (msg->eventID == ANALYTIC_UNDERPRESSURE) { 
          pressure_start[msg->fromUID] = msg->timeOfEvent;
          has_pressure = true;
        }
        if (msg->eventID == ANALYTIC_NOT_UNDERPRESSURE && pressure_start[msg->fromUID] != 0) { 
          pressure_total[msg->fromUID] += msg->timeOfEvent - pressure_start[msg->fromUID];
          pressure_start[msg->fromUID] = 0;
        }
        if (memory_max[msg->fromUID] < msg->fromHeapUsed) { 
          memory_max[msg->fromUID] = msg->fromHeapUsed;
        }
      }
    }
    
    // continue until we've written all of our analytics events
    if (analysisThreadRunning == false && local_analysisMessageQueuePtr->num_messages <= 0) {
      break;
    }
    
    ponyint_cpu_sleep(5000);
  }
  
#ifndef PLATFORM_IS_IOS
  if (analysis_enabled > 1) {
    fclose(analyticsFile);
  }
#endif
  
  // complete time for any waiting muted or pressure actors
  /*
  for(unsigned int i = 0; i < kMaxActors; i++) {
    if (pressure_start[i] > 0) { 
      pressure_total[i] += ponyint_analysis_timeInNanoseconds() - pressure_start[i];
    }
    if (muted_start[i] > 0) { 
      muted_total[i] += ponyint_analysis_timeInNanoseconds() - muted_start[i];
    }
    if (gc_start[i] > 0) { 
      gc_total[i] += ponyint_analysis_timeInNanoseconds() - gc_start[i];
    }
  }*/
  
  
  char * resetColor = "\x1B[0m";
  char * redColor = "\x1B[91m";
  char * greenColor =  "\x1B[92m";
  char * lightGreyColor =  "\x1B[37m";
  char * darkGreyColor =  "\x1B[90m";
  char * orangeColor =  "\x1B[31m";
  
  const int kMaxActorsPerReport = 20;
  
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
    
  // determine if we have any untagged actors
  bool hasUntaggedActors = false;
  for(unsigned int i = 0; i < kMaxActors; i++) {
    if ( actor_tags[i] == 0 && (pressure_total[i] > 0 || muted_total[i] > 0 || overload_counts[i] > 0)) {
      hasUntaggedActors = true;
      break;
    }
  }
    
    
  // 1. overloaded actors
  if (has_overloaded || has_muted || has_pressure) {    
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
          fprintf(stderr, "%sactor [untagged] was muted for %llu ms%s\n", orangeColor, NANO_TO_MILLI(muted_total[i]), resetColor);
        } else {
          if (actor_tags[i] >= kActorNameStart && actor_tags[i] <= kActorNameEnd) {
            fprintf(stderr, "%sactor %s was muted for %llu ms%s\n", orangeColor, builtinActorNames[actor_tags[i] - kActorNameStart], NANO_TO_MILLI(muted_total[i]), resetColor);
          } else {
            fprintf(stderr, "%sactor tag %llu was muted for %llu ms%s\n", orangeColor, actor_tags[i], NANO_TO_MILLI(muted_total[i]), resetColor);
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
          fprintf(stderr, "%sactor [untagged] was under pressure for %llu ms%s\n", orangeColor, NANO_TO_MILLI(pressure_total[i]), resetColor);
        } else {
          if (actor_tags[i] >= kActorNameStart && actor_tags[i] <= kActorNameEnd) {
            fprintf(stderr, "%sactor %s was under pressure for %llu ms%s\n", orangeColor, builtinActorNames[actor_tags[i] - kActorNameStart], NANO_TO_MILLI(pressure_total[i]), resetColor);
          } else {
            fprintf(stderr, "%sactor tag %llu was under pressure for %llu ms%s\n", orangeColor, actor_tags[i], NANO_TO_MILLI(pressure_total[i]), resetColor);
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
  }else{
    fprintf(stderr, "\n\n%s**** Pony Runtime Analysis: PASS%s\n\n", greenColor, resetColor);
  }
  
  if (has_gc) {
    uint64_t max_actors[kMaxActorsPerReport] = {0};
    uint64_t total_reported = 0;
    uint64_t max_reported = findTopActorsInValues(memory_max, kMaxActors, max_actors, kMaxActorsPerReport, &total_reported);
    float to_mb = (1024 * 1024);
    for(uint64_t v = 0; v < max_reported; v++) {
      uint64_t i = max_actors[v];
      
      if (gc_counts[i] == 0) {
        gc_counts[i] = 1;
      }
      if (run_counts[i] == 0) {
        run_counts[i] = 1;
      }
      
      if (actor_tags[i] == 0) {
        fprintf(stderr, "%sactor [untagged] garbage collected %llu times (total %llu ms, avg %llu ms), avg run time of %llu ms and max heap size of %0.2f MB%s\n", 
          darkGreyColor, 
          gc_counts[i], 
          NANO_TO_MILLI(gc_total[i]), 
          NANO_TO_MILLI(gc_total[i]) / gc_counts[i], 
          NANO_TO_MILLI(run_total[i]) / run_counts[i], 
          memory_max[i] / to_mb, 
          resetColor);
      } else {
        if (actor_tags[i] >= kActorNameStart && actor_tags[i] <= kActorNameEnd) {
          fprintf(stderr, "%sactor %s garbage collected %llu times (total %llu ms, avg %llu ms), avg run time of %llu ms and max heap size of %0.2f MB%s\n", 
            darkGreyColor, 
            builtinActorNames[actor_tags[i] - kActorNameStart], 
            gc_counts[i], 
            NANO_TO_MILLI(gc_total[i]), 
            NANO_TO_MILLI(gc_total[i]) / gc_counts[i], 
            NANO_TO_MILLI(run_total[i]) / run_counts[i], 
            memory_max[i] / to_mb, 
            resetColor);
        } else {
          fprintf(stderr, "%sactor tag %llu garbage collected %llu times (total %llu ms, avg %llu ms), avg run time of %llu ms and max heap size of %0.2f MB%s\n", 
            darkGreyColor, 
            actor_tags[i], 
            gc_counts[i], 
            NANO_TO_MILLI(gc_total[i]), 
            NANO_TO_MILLI(gc_total[i]) / gc_counts[i], 
            NANO_TO_MILLI(run_total[i]) / run_counts[i], 
            memory_max[i] / to_mb, 
            resetColor);
        }
      }
    }
    if (total_reported > max_reported) {
      fprintf(stderr, "%s... %llu other actors also garbage collected%s\n", darkGreyColor, total_reported-max_reported, resetColor);
    }    
    fprintf(stderr, "\n");
    
    fprintf(stderr, "%sLarge amounts of garbage collection in Pony can mean two things\n", darkGreyColor);
    fprintf(stderr, "1. Time spent garbage collecting when it could be spent elsewhere\n");
    fprintf(stderr, "2. Garbage collection of data shared between actors requires messaging between actors to resolve\n");
    fprintf(stderr, "Reducing the amount of transitory memory allocations in critical paths can result\n");
    fprintf(stderr, "in performance gains%s\n\n", resetColor);
    
    fprintf(stderr, "\n");
    
    if (total_app_messages > 0 || total_system_messages > 0) {
      fprintf(stderr, "\n\n\n");
      fprintf(stderr, "%sTotal app messages sent: %llu\n", darkGreyColor, total_app_messages);
      fprintf(stderr, "Total system messages sent: %llu%s\n", total_system_messages, resetColor);
      fprintf(stderr, "\n\n\n");
    }
    
  }
  
  
  
  // Note: it appears the runtime doesn't bother calling actor destroy when the program finished normally
  // (i don't blame it), so this report should only be useful for someone who SIGINT'd the program
  if (analysisThreadWasKilledUnexpectedly) {
    // In the case of us force quitting the app (because its not ending correctly?) it would be nice
    // to see a list of actors still running (haven't been destroyed)
    for(uint64_t i = 0; i < kMaxActors; i++) {
      if (destroyed_flags[i] == 1) { // all actors still alive...
        if (actor_tags[i] == 0) {
          fprintf(stderr, "%sactor [untagged] was never destroyed [UID: %llu, NumMsgs: %llu]%s\n", orangeColor, i, last_msg_count[i], resetColor);
        } else {
          if (actor_tags[i] >= kActorNameStart && actor_tags[i] <= kActorNameEnd) {
            fprintf(stderr, "%sactor %s was never destroyed [UID: %llu, NumMsgs: %llu]%s\n", orangeColor, builtinActorNames[actor_tags[i] - kActorNameStart], i, last_msg_count[i], resetColor);
          } else {
            fprintf(stderr, "%sactor tag %llu was never destroyed [UID: %llu, NumMsgs: %llu]%s\n", orangeColor, actor_tags[i], i, last_msg_count[i], resetColor);
          }
        }
      }
    }
    
    fprintf(stderr, "\n");
    
    fprintf(stderr, "%sPony programs end when all actors have been destroyed and all schedulers have been shut down.\n", darkGreyColor);
    fprintf(stderr, "If you are reading this message then most likely you had to terminate the program manually.\n");
    fprintf(stderr, "You can use this list of actors to help narrow down why the actors didn't finish processing.%s\n", resetColor);
    
    fprintf(stderr, "\n");
    
    // Report on the active schedulers...
    uint32_t active_scheduler_count = get_active_scheduler_count();
    
    fprintf(stderr, "There are %d active schedulers\n\n", active_scheduler_count);
    for(uint32_t i = 0; i < active_scheduler_count; i++) {
      scheduler_t * sched = ponyint_sched_by_index(i);
      
                                      fprintf(stderr, "  Scheduler #%d: \n", sched->index);
      if((int32_t)sched->cpu >= 0) {  fprintf(stderr, "     cpu / node: %d / %d \n", sched->cpu, sched->node); }
      if(sched->terminate) {          fprintf(stderr, "     is terminate\n"); }
      if(sched->asio_stoppable) {     fprintf(stderr, "     is asio stoppable \n"); }
      if(sched->asio_noisy) {         fprintf(stderr, "     is asio noisy \n"); }
      if(sched->main_thread) {        fprintf(stderr, "     is main thread \n");  }
      if(sched->block_count) {        fprintf(stderr, "     block_count: %d \n", sched->block_count); }
      if(sched->ack_token) {          fprintf(stderr, "     ack_token: %d \n", sched->ack_token); }
      if(sched->ack_count) {          fprintf(stderr, "     ack_count: %d \n", sched->ack_count); }
                                      fprintf(stderr, "     waiting actors: %lld \n", sched->q.num_messages);
      if(sched->mq.num_messages) {    fprintf(stderr, "     mq num msgs: %lld\n", sched->mq.num_messages);  }
    }
    fprintf(stderr, "\n\n");
    fprintf(stderr, "There are %lld actors waiting in inject queue\n", ponyint_size_of_inject_queue());
    fprintf(stderr, "There are %lld actors waiting in inject main queue\n\n", ponyint_size_of_inject_main_queue());
  }
  
  if (hasUntaggedActors) {
    fprintf(stderr, "\n\n%sNote: You can tag actors to associate results with specific types of actors.\n", darkGreyColor);
    fprintf(stderr, "actor Foo\n");
    fprintf(stderr, "  fun _tag():USize => 2%s\n\n", resetColor);
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
    uint64_t timeOfEvent = (ponyint_analysis_timeInNanoseconds() - startNanoseconds);
    
    analysis_msg_t * msg = (analysis_msg_t*) pony_alloc_msg(POOL_INDEX(sizeof(analysis_msg_t)), 0);
    msg->timeOfEvent = timeOfEvent;
    msg->fromUID = from->uid;
    msg->fromTag = from->tag;
    msg->eventID = event;
    msg->fromNumMessages = (from->q.num_messages > 0 ? from->q.num_messages : 0);
    msg->fromBatch = from->batch;
    msg->fromPriority = from->priority;
    msg->fromHeapUsed = from->heap.used;
    msg->toUID = to->uid;
    msg->toTag = to->tag;
    msg->toNumMessages = (to->q.num_messages > 0 ? to->q.num_messages : 0);
    msg->totalMemory = ponyint_total_memory();
    
    ponyint_thread_messageq_push(&analysisMessageQueue, (pony_msg_t*)msg, (pony_msg_t*)msg
    #ifdef USE_DYNAMIC_TRACE
          , SPECIAL_THREADID_ANALYSIS, SPECIAL_THREADID_ANALYSIS
    #endif
        );
    /*
    // if we're overloading the save-to-file thread, slow down a little
    if (analysisMessageQueue.num_messages > 100000) {
      ponyint_cpu_sleep(50);
    }*/
  }
}

void saveRuntimeAnalyticForActor(pony_ctx_t * ctx, pony_actor_t * actor, int event) {
  if (ctx->analysis_enabled == 0) {
    return;
  }
  
  // level 1 analysis only cares about these events
  if ( ctx->analysis_enabled == 1 && 
    ( event != ANALYTIC_OVERLOADED && 
      event != ANALYTIC_MUTE && 
      event != ANALYTIC_NOT_MUTE && 
      event != ANALYTIC_UNDERPRESSURE && 
      event != ANALYTIC_NOT_UNDERPRESSURE && 
      event != ANALYTIC_GC_START &&
      event != ANALYTIC_GC_END &&
      event != ANALYTIC_RUN_START &&
      event != ANALYTIC_RUN_END &&
      event != ANALYTIC_ACTOR_DESTROYED)) {
    return;
  }
  
  // level 2 analysis only cares about events with a tag
  if ( ctx->analysis_enabled == 2 && 
    ( event != ANALYTIC_OVERLOADED || 
      event != ANALYTIC_MUTE || 
      event != ANALYTIC_NOT_MUTE || 
      event != ANALYTIC_UNDERPRESSURE ||
      event != ANALYTIC_NOT_UNDERPRESSURE ||
      event != ANALYTIC_GC_START ||
      event != ANALYTIC_GC_END ||
      event != ANALYTIC_RUN_START ||
      event != ANALYTIC_RUN_END ||
      event != ANALYTIC_ACTOR_DESTROYED) &&
     actor->tag == 0) {
    return;
  }
    
  if (analysisThreadRunning && actor != NULL) {
    
    // Note: the level 1 analytics should work with untagged actors. The level 2 anaylics should
    // only work with tagged actors. Therefor we want to filter which events we actually send 
    // over to be counted to keep down unnecessary work.
    uint64_t timeOfEvent = (ponyint_analysis_timeInNanoseconds() - startNanoseconds);
    
    analysis_msg_t * msg = (analysis_msg_t*) pony_alloc_msg(POOL_INDEX(sizeof(analysis_msg_t)), 0);
    msg->timeOfEvent = timeOfEvent;
    msg->fromUID = actor->uid;
    msg->fromTag = actor->tag;
    msg->eventID = event;
    msg->fromNumMessages = (actor->q.num_messages > 0 ? actor->q.num_messages : 0);
    msg->fromBatch = actor->batch;
    msg->fromPriority = actor->priority;
    msg->fromHeapUsed = actor->heap.used;
    msg->toUID = 0;
    msg->toTag = 0;
    msg->toNumMessages = 0;
    msg->totalMemory = ponyint_total_memory();
    
    ponyint_thread_messageq_push(&analysisMessageQueue, (pony_msg_t*)msg, (pony_msg_t*)msg
    #ifdef USE_DYNAMIC_TRACE
          , SPECIAL_THREADID_ANALYSIS, SPECIAL_THREADID_ANALYSIS
    #endif
        );
    /*
    // if we're overloading the events thread, slow down a little
    if (analysisMessageQueue.num_messages > 100000) {
      ponyint_cpu_sleep(50);
    }*/
  }
}


void startRuntimeAnalysis(pony_ctx_t * ctx) {
    
    if (ctx->analysis_enabled == 0) {
        return;
    }
    
    // We use a thread to not slow down the rest of the pony program with storing the events to disk
    if (analysisThreadRunning == false) {
        analysisThreadRunning = true;
        
        startNanoseconds = ponyint_analysis_timeInNanoseconds();
        
        ponyint_messageq_init(&analysisMessageQueue);
        if(!ponyint_thread_create(&analysisThreadID, analysisEventStorageThread, -1, &(ctx->analysis_enabled))) {
            analysisThreadRunning = false;
            ctx->analysis_enabled = 0;
        }
    }
}

void stopRuntimeAnalysis(bool killed) {
  
  if (analysisThreadRunning) {
    analysisThreadWasKilledUnexpectedly = killed;
    
    analysisThreadRunning = false;
    ponyint_thread_join(analysisThreadID);
    
    ponyint_messageq_destroy(&analysisMessageQueue);
  }
}
