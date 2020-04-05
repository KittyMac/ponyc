# Rocco's Pony Fork

Welcome to my fork of Pony!  Pony is pretty amazing.

My fork has several major additions you might find intersting.  
[Check out the Divergence documentation for full list of differences.](https://github.com/KittyMac/ponyc/blob/roc_master/DIVERGENCE.md).

* **Less wasted CPU**  
The runtime in stock Pony has a large amount of overhead when schedulers are waiting for work, in theory trading off CPU usage for decrease in latency. This fork of Pony uses a different scheme for sleeping when no work is available, reducing the amount of CPU waste considerably. The difference can be staggering. In one practical example the old scheduling method would use 1700% CPU (17 full cores) whereas the new scheduling uses 125% CPU (1.25 cores).
* **Built-in code transpiler**  
Generate Pony code automatically from C headers, Json Schema, and text-based resources.  
* **Additional error handling**  
Send an int-based error code wihen you throw an error, or get the source code location of any error you throw.  
* **Pony runtime analysis tools**  
Writing Pony code which under performs due to not understanding the Pony runtime is shockingly easy. With the --ponyanalysis option turned on you will be told when your application crosses some of the hidden barriers (and provides mechanisms for how to avoid them)!
* **Actor execution on "main thread" only**  
If you need to use Pony with OpenGL or other UI frameworks which rely on code executing on the "main thread" now you can.
* **Cross-compile to iOS**  
Yes, you can run Pony on iOS.  You're welcome.


That being said, there are reasons not to use this fork:

* **Active pace of development and experimentation**  
As of this writing, development is active on this fork and breaking changes can happen daily.
* **Support hierarchy is Mac > Linux > Windows**  
My development platform of choice is Mac OS. As such, if you are building on Linux or Windows your mileage may vary.


-

# Building on Mac OS

To build my fork of Pony to include all features, use the following command:

```make -f Makefile-ios config=release all install```

