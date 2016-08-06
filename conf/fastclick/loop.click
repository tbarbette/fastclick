//This implements a very simple "invisible printing cable"
//This file is a good start point to learn

/*Verbosity will mainly print information about thread managment
 * experiment changing the core mask in the -c argument to see how FastClick
 * will try to use all the cores equally and find out where threads can end up.
 * Try inserting a Queue->Unqueue, staticthreadsched, and Pipeliner to see what
 * happens.
 */
define($verbose 3)

/*If a path try to push a packet to a busy ToDPDKDevice (no more space in the ring), it will block until there is if blocking is true, or drop the packet if false. Blocking is also known as back pressure.
 */
define($blocking true)

FromDPDKDevice(0, VERBOSE $verbose, PROMISC true) -> Print -> ToDPDKDevice(1,BLOCKING $blocking, VERBOSE $verbose)
FromDPDKDevice(1, VERBOSE $verbose, PROMISC true) -> Print -> ToDPDKDevice(0,BLOCKING $blocking, VERBOSE $verbose)
