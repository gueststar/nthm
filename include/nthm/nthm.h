
#ifndef NTHM_H
#define NTHM_H 1

// range of negative numbers reserved for error codes
#define NTHM_MIN_ERR 16
#define NTHM_MAX_ERR 511

// in 32-bit mode, the stack size in bytes in excess of PTHREAD_STACK_MIN allocated for threads
#define NTHM_STACK_MIN 16384

// error codes

#define NTHM_UNMANT (-16)
#define NTHM_NOTDRN (-17)
#define NTHM_NULPIP (-18)
#define NTHM_INVPIP (-19)
#define NTHM_KILLED (-20)
#define NTHM_UNDFLO (-21)
#define NTHM_XSCOPE (-22)

typedef void *(*nthm_worker)(void *,int *);   // the type of function passed to nthm_open

typedef void (*nthm_slacker)(void *);         // the type of function passed to nthm_send

typedef struct nthm_pipe_struct *nthm_pipe;   // to be treated as opaque in user application code

// translate an error code into a readable message
extern const char*
nthm_strerror (int err);

// start a new thread and return its pipe
extern nthm_pipe
nthm_open (nthm_worker operator, void *operand, int *err);

// start a new thread with no pipe, but have it automatically reclaimed and synchronized
extern int
nthm_send (nthm_slacker mutator, void *operand, int *err);

// collectively poll the finishers
extern int
nthm_blocked (int *err);

// return the pipe of the next thread to finish within the current scope, if any
extern nthm_pipe
nthm_select (int *err);

// poll a specific pipe
extern int
nthm_busy (nthm_pipe source, int *err);

// perform a blocking read from a pipe and then dispose of it
extern void*
nthm_read (nthm_pipe source, int *err);

// tell a thread to shorten its output and finish up
extern void
nthm_truncate (nthm_pipe source, int *err);

// tell all tethered threads in the current scope to shorten their output and finish up
extern void
nthm_truncate_all (int *err);

// inquire about whether truncated output has been requested of the current thread
extern unsigned
nthm_truncated (int *err);

// tell a thread to finish up and that its output will be ignored
extern void
nthm_kill (nthm_pipe source, int *err);

// tell all tethered threads in the current scope to finish up and that their output will be ignored
extern void
nthm_kill_all (int *err);

// inquire about whether the current thread has been killed
extern int
nthm_killed (int *err);

// make a thread independent of its creator
extern void
nthm_untether (nthm_pipe source, int *err);

// make an untethered thread dependent on the currently running thread
extern void
nthm_tether (nthm_pipe source, int *err);

// restrict attention to pipes opened subsequently
extern int
nthm_enter_scope (int *err);

// resume previous attention span
extern void
nthm_exit_scope (int *err);

// wait for all threads created by nthm to exit
extern void
nthm_sync (int *err);

#endif
