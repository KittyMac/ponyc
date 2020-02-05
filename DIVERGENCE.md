# Divergence from Pony

This purpose of this file is to catalogue the changes this fork has implemented which differ from stock pony.  Please note that on my fork I don't actively keep up Windows support.  Linux will likely just work (or can be made to work with small changes).  Mac OS users should have no problem as that is my development platform.


## Add support for linking to frameworks on Mac OS

On Mac OS some libraries you link to as frameworks (with the -framework argument to ld). This adds support for framework as a tag in the use statements in pony

```use "framework:GLUT"```

## Hint actor should run on its own thread

This feature is necessary when working with external APIs which require that their callers only execute from the main thread (such as OpenGL). Previously the only mechanism to do this was to create a C shell and compile your pony code as a library, and have the C code instantiate an FFI actor. Now all you need to do is include the following hint in your actor class, and instances of that class will only execute on the main thread.

``` 
actor MyOpenGLInterface
	fun _use_main_thread():Bool => true	
```

Implementation is as follows:

1. The last scheduler is designed the "main thread" scheduler
2. Actors flag themselves as "use_main_thread" on their first call into ponyint_actor_run()
2. There is a new inject_main mpmcq, who purpose is to funnel use_main_thread actors to the main thread
3. The "main thread" scheduler check the inject_main mpmcq first before checking the normal global pop
4. Actors pushed to a scheduler are check for "use_main_thread" and diverted to inject_main if needed

## testsFinished()

Added a testsFinished() callback to ponytest.  This allows for things like running all tests on program first start, and if they all succeed then allowing the program to run as normal.  If tests fail, then the application take take the apprioriate measures (for example, on a critical server application you might want it to not run at all if its build in tests fail on start up).



## Runtime option --ponyanalysis

### --ponyanalysis 0

Pony analysis disabled.

### --ponyanalysis 1

The runtime will monitor a limited number of statistics to provide you with helpful information when your pony program completes. This level of analysis impacted runtime the least, but provides a healthy check against the following issues:

1. Identifying actor bottlenecks (actors who overload)
2. Identifying muted actors and how much real time they spend muted (usually as a result of #1)
3. Identifying actors who experienced back pressure and how much real time that occured


### --ponyanalysis 2

The biggest hurdle to writing performant pony code is not having an understanding of how the runtime works. To help profile my pony projects for runtime issues I added code which will export information about actors, their various states and their message passing. I consider the implementation to be fairly optimized (the I/O is on its own thread, events are passed to it using pony messageq, etc).  In practical tests having it on usually results in < 1% change in efficiency, but YYMV.

The output is saved to /tmp/pony.ponyrt\_analytics in a simple csv format. Each event saves the following:

* **TIME\_OF\_EVENT** : time in milliseconds of the event
* **ACTOR\_A\_UUID** : unique number representing the unique instance of this actor
* **ACTOR\_A\_TAG** : value retuned by the \_tag() function implemented by the pony developer (see below)
* **EVENT\_NUMBER** : type of event (see table below)
* **ACTOR\_A\_NUMBER_OF_MESSAGES** : number of messages in the actor's queue at the time of the event
* **ACTOR\_A\_BATCH_SIZE** : message batch size for this actor (see Actor Network Performance Hints section)
* **ACTOR\_A\_PRIORITY** : custom priority for this actor (see Actor Network Performance Hints section)
* **ACTOR\_A\_HEAP\_SIZE** : size of the actor's memory heap
* **ACTOR\_B\_UUID** : if message send event, this is the UUID of the actor the message is being sent to
* **ACTOR\_B\_TAG** : if message send event, this is the tag of the actor the message is being sent to
* **ACTOR\_B\_NUMBER\_OF\_MESSAGES** : if message send event, this is the number of messages in the target actor queue
* **TOTAL\_MEMORY** : total memory of the program as reported by the OS (see performance section)

I wrote a quick and dirty Mac OpenGL app wihich generates a visualization of the information contained in the pony.ponyrt\_analytics file.  It is particularly useful for identifying choke points in your actor network, allowing you to make architectural changes to eliminate said choke points. Actor muting is the ponyrt's achilles heel and its best to write your pony code to ensure it doesn't happen.

Only actors which are hinted to be part of the ponyanalysis are included in the ponyanalysis.  You hint your actor by providing the following method to your actor:

```
// This hints the runtime that you would event analytics to be recorded for this actor, and that the tag for said actor in the event log should be 9
fun _tag():USize => 9
```

Future work could include command line tools which analyze the exported events and provide analytic information in a more portable manner.

## Actor Network Performance Hints

Using the ponyanalysis option you can easily identify bottlenecks in your actor networks. Sometimes those bottlenecks cannot be removed by re-architecting your program. For example, if you have a pool of workers doing work and their output must be captured, then there is likely a collection actor to whom all of the workers need to message.  This actor will be your bottleneck and every time his message queue exceeds the compiled in default of 100 messages your pool of workers will stop working. In this scenario, it makes sense to provide a couple of hints to the ponyrt to facilitate less muting of your work pool.

* **Hinting actor batch sizes** : This change allows you to give the collecting actor a larger batch size. You, the pony developer, now has the option to trade memory consumption in favor of performance.  You can do this by adding the following hint to your actor:

```
// This hints the runtime that you would like this actor's batch size to be x5 the default (default is 100)
fun _batch():USize => 500
```

* **Hinting actor priority** : The second change that allows our collection actor to benefit from being allowed to run more often that the workers in the worker pool (or, in reverse, if you want work to get done faster and delay collection in exchange for keeping all those messages in memory).  To do this, you can hint the collection actor to have a different priority than the worker actors.  The default actor priority is "0", and you can set negative or positive priority levels.  
The implementation of priority in the runtime is very simple. When an actor finishes a run (a run is an actor processing a certain number of application messages) that actor is then moved to the end of the scheduler's waiting actor queue.  Now, if said actor has a higher priority than the next actor to be run, the high priority actor is immediately rescheduled and the lower priority actor is sent to the global actor queue (where it can get picked up by another scheduler).

```
// This hints the runtime that you would like this actorto have a certain priority level
fun _priority():ISize => 2
```

* **Explicit yields** : The third change which can help is by allowing an actor to explicitly yield execution.  This is also implemented very simply.  When an actor is in its run loop and your behaviour call is executing, you can flag the actor as needing to yield. When your behaviour finishes the actor will not exectue any further messages (regardless of its batch size) and end its current run.  The actor will then be rescheduled by the ponyrt as normal.

```
// Flag this actor such that it should yield after processing of the current behaviour has concluded.
@ponyint_actor_yield[None](this)
```



## Synchronous and Partial call support for actor constructors

[Zulip RFC Conversation](https://ponylang.zulipchat.com/#narrow/stream/189959-RFCs/topic/synchronous.20actor.20constructors/near/185278655)

There are performance gains to be made by allowing actor constructors to be call synchronously. And if we are calling actor constructors synchronously, then there are convenience benefits to be gained by supporting partial calls to actor constructors (ie allowing actor constructors to throw pony errors and being able to deal with said errors immediately in the calling code).

On my fork the default behaviour matches stock pony behaviour (actor constructors are asynchronous).  If you make an partial actor constructor, then that constructor will be called synchronously.

This behaviour can be changed with the runtime option ```--sync-actor-constructors```, which will make all actor constructors execute synchronously.  Useful if you to want check quickly to see if synchronous actor constructors would actually be provide a performance benefit for your pony code, or if you simply prefer your constructors to all run synchronously.

## Critical bug fix for a single actor processing its behaviours in parallel on two different schedulers

While performance testing the synchronous actor stuff above, I encountered a crash in stock pony when ponynoblock was turned on.  The crash was happening because the same pony actor was being executed in ponyint_actor_run() on two different scheduler threads at the same time. My understanding is that this should never be possible (as then actor behaviours cannot be gauranteed to execute synchronously).  I debugged it enough to know that the issue is somewhere in the work stealing code, likely a race condition most people might not hit. To protect agaist this, I implemented a boolean check to help catch when this situation occurs and not allow the same actor to be run on two different schedulers at the same time.


## Compile errors for unused local variables

I have been spending a lot of time bouncing between the ponyrt C code and my pony code. The ponyrt C code will error out for unusued variables, but the pony code does not.  I believe not having unusued variables lying around enforces cleaner code, so I added it to ponyc. I then fixed all of the unusued variables in the various library packages.

By default this error is enabled. You can disable it using the --allow-unused-vars compiler option.

```
Error:
/Volumes/Development/Development/pony/pony.problems/unused_variables_warning_1/main.pony:37:3: Local variable unusedLet is unused in all code paths
		let unusedLet:I64 = 8
		^
```


## @ponyint\_actor\_num\_messages()

Part of the ponyanalysis change above now means that actors store the number of messages they have queued up to process. 
```@ponyint_actor_num_messages(myActor)``` will return that number to you in your pony code. This information can be useful for many scenarios, but perhaps the most obvious one is for load balancing.  If you have manager actor who wants to distribute work to a pool of actors, the most often recommended method is either round-robin or random selection. Now your load balancer can find the actor with the least work queued and provide the work to that actor.


## @ponyint\_total\_memory()

Part of the ponyanalysis change, the ponyrt is now aware of the total amount of memory usage it has (as seen by the OS). If your pony code wants to know about it, it can retrieve it using this method call.


## Allow package source code in subdirectories which start with a "+"

I can't stand not being able to organize my code in subdirectories, and I don't want them compiled into a different package.

ponyc will now add code contained in sub directories which start with a "+" to be included in the current package being compiled.

Example: the following are all compiled into the "http" package, just as if they were all contained in the same directory.

![](https://github.com/KittyMac/ponyc/blob/roc_master/meta/directory.png?raw=true)

## Compile Json Schema

You can include [json schema](https://json-schema.org) files in your packages.  ponyc will transpile them to pony classes suitable for serializing and deserializing json.

Handling Json in the manner has a number of benefits. The most critical benefit is that you will get compile errors whenever breaking changes happen to your API calls (as opposed to hunting down runtime errors because that one json property you relied on is no longer in the message you thought it should be in).  So if tomorrow your Person object no longer has a middle name, your pony code which relies on it having a middle name will now throw an error (because it will no longer exist in the generated class).

To see what pony code is generated from your json schema files, add the ```--print-code``` flag to ponyc.

There are also some additions to allow you to affect the generated pony code, such as adding use line or traits to class declarations.

```
{
	"title": "Game",
	"type": "object",
	"pony-use": "math",
	"pony-use": "stringext",
	"pony-traits": "(UUIDable & Randable & GameExt)",
	"properties": {
		"uuid": { "type": "string", "pony-default": "default_uuid()"  },
		"seed": { "type": "integer", "pony-default": "default_seed()"  },
		"settings": { "type": "#object/GameSettings" },
		
		"pedia": { "type": "#object/SOPedia", "pony-default": "SOPedia" },
		
		"stardate": { "type": "integer", "default":"4000" },
		"galaxy": { "type": "#object/Galaxy" },
		
		"diplomacyMatrix": { "type": "#object/IMatrix" },
		
		"empires": { 
		    "type": "array",
		    "items": { "type": "#object/Empire" }
		}
	}
}
```

Note: The full range of json schema is not supported (and may never be).

## Compile "Text" Resources

This one is still in the experimental phase. It similar to the Json Schema above, where you have a file with information and you want some pony code to be generated from it. In this case the files are text resources, and the pony code generated is a primitive with the contents of the file embedded in it.  Right now it will pick up any markdown (.md), json (.json), or plain text (.txt) files in your packages and generate pony code for them

To see what pony code is generated from text files, add the ```--print-code``` flag to ponyc.


## Misc builtin package additions

* **added cpu_count to Env** : just fills the value from @ponyint_cpu_count[U32](), avoiding an FFI call
* **string.pony** : added is_empty(). _to_int() now skips leading white space (seemed more performant to do it here)
* **array.pony** : added is_empty(). added deleteOne() to delete one of the provided thing; deleteAll() to delete all of the provided thing; added valuesAfter() to create iterator starting at a specific offset.
* **OSSocket now public** : I wanted to implement http server without using TCPConnection/TCPListener, and _is_sock_connected() relies on accessing it.



## Pony on iOS

# crash fixes

* **diverge from ARM code path in mpmqc.c** : In mpmqc there is a code path specific to ARM.  When testing on iOS this code path led to crashes when under load.  Made iOS compiles use the x86 path which eliminated the crashes.  Unsure whether this was a "arm on iOS" problem or a pre-existing issue in this arm optimized path.

# feature additions

* **added ponyint\_cpu\_throttle(uint64\_t v)** : added this call to allow pony code to have a say in how much cpu throttling does or does not happen. Provides interesting opportunities from an app development standpoint (force less sleeping while the user is interacting with the screen, or force higher sleeping when we want to reduce battery drain).
* **pony\_main()** : when compiling as a library, the library now includes main() renamed as pony\_main(). I find this more convenient to allow the host program to call pony\_main() with argument list and knowing all of the pony code will execute in the exact same manner.

# misc

* **Makefile-ios** : call this to make a cross-compilable ponyc as well as library versions of the runtime in the correct architectures
* **change for mach\_absolute\_time** : mach\_absolute\_time() calls vanilla return values are indeterminate based on the platform it is running on.  They need to be converted to nanoseconds to provide consistent timings (https://kandelvijaya.com/2016/10/25/precisiontiminginios/).
* **change to ponyint\_cpu\_core\_pause()** : adjusted for mac and ios, related to the above change.
* **added "ios" as a valid platform name/target triple** (ie ```use "lib:ponyjpeg-ios"```)
* **added pony\_poll\_many()** : simpler mechanism for polling actor from library (same as pony\_poll() by polling is set to false to one whole actor run is completed)

# todos / known issues

* **FFI calls with variable args** : in gencall.c, the call to declare\_ffi\_vararg(c, f\_name, t); in gen\_ffi(); generates a call structure or something which doesn't work when run on iOS (ie crashes). I was successful in working around it by forcing those calls to use declare\_ffi(c, f\_name, t, args, true); instead.  Which means no var args support in FFI calls on iOS, but works for the other 95%.
* **Double to string conversion woes** : I have a workaround in place for double-to-string conversion.  I forget exactly how this presented itself, but it very likely related to the above.  Work around calls a @snprintf\_Double() wrapper instead of the normal @snprintf().


## My Pony packages / experiments outside of ponyc/ponyrt

Here is a list of package repositories I have made while working with Pony.

* **Pony.tmbundle** : syntax highlighting and other good stuff for Pony in TextMate.

* **pony.http** : simple and fast http server
* **pony.stringExt** : handy string methods (like ```StringExt.format("something %s this way comes", 42)``` )
* **pony.math** : contains simple vector and matrix load/unload from json stuff
* **pony.schema.json** : tests and examples around the json schema transpiler
* **pony.python** : simple pony wrapper for executing python scripts
* **pony.ttimer** : simple wrapper around pony timers
* **pony.problems** : catalogues some of the things written in this document with pratical examples
* **pony.bitmap** : code for handling bitmaps in pony
* **pony.sprite** : code for handling scripts and spritesheets in pony
* **pony.jpg** : reading and writing JPEG compresses images
* **pony.png** : reading and writing PNG compresses images
* **pony.flow** : provides mechanism for chaining actors together in a generic fashion
* **pony.csv** : read and write csv, comptible with pony.flow
* **pony.fileExt** : file access mechanism that are tight wrappers around "normal" posix file access (my experience with File in pony is slow, these are fast). comptible with pony.flow.
* **pony.lzip** : allow decompressing data using the lzip library. comptible with pony.flow.
* **pony.bzip2** : allow decompressing data using the bzip2 library. comptible with pony.flow.
* **pony.easings** : place to collect easing methods.  only easeInExpo() and easeOutExpo() implemented right now
