#include "kernel.h"
#include "config.h"
#include "globals.h"
#include "errno.h"

#include "util/debug.h"
#include "util/list.h"
#include "util/string.h"
#include "util/printf.h"

#include "proc/kthread.h"
#include "proc/proc.h"
#include "proc/sched.h"
#include "proc/proc.h"

#include "mm/slab.h"
#include "mm/page.h"
#include "mm/mmobj.h"
#include "mm/mm.h"
#include "mm/mman.h"

#include "vm/vmmap.h"

#include "fs/vfs.h"
#include "fs/vfs_syscall.h"
#include "fs/vnode.h"
#include "fs/file.h"

proc_t *curproc = NULL; /* global */
static slab_allocator_t *proc_allocator = NULL;

static list_t _proc_list;
static proc_t *proc_initproc = NULL; /* Pointer to the init process (PID 1) */

void
proc_init()
{
        list_init(&_proc_list);
        proc_allocator = slab_allocator_create("proc", sizeof(proc_t));
        KASSERT(proc_allocator != NULL);
}

proc_t *
proc_lookup(int pid)
{
        proc_t *p;
        list_iterate_begin(&_proc_list, p, proc_t, p_list_link) {
                if (p->p_pid == pid) {
                        return p;
                }
        } list_iterate_end();
        return NULL;
}

list_t *
proc_list()
{
        return &_proc_list;
}

static pid_t next_pid = 0;

/**
 * Returns the next available PID.
 *
 * Note: Where n is the number of running processes, this algorithm is
 * worst case O(n^2). As long as PIDs never wrap around it is O(n).
 *
 * @return the next available PID
 */
static int
_proc_getid()
{
        proc_t *p;
        pid_t pid = next_pid;
        while (1) {
failed:
                list_iterate_begin(&_proc_list, p, proc_t, p_list_link) {
                        if (p->p_pid == pid) {
                                if ((pid = (pid + 1) % PROC_MAX_COUNT) == next_pid) {
                                        return -1;
                                } else {
                                        goto failed;
                                }
                        }
                } list_iterate_end();
                next_pid = (pid + 1) % PROC_MAX_COUNT;
                return pid;
        }
}

/*
 * The new process, although it isn't really running since it has no
 * threads, should be in the PROC_RUNNING state.
 *
 * Don't forget to set proc_initproc when you create the init
 * process. You will need to be able to reference the init process
 * when reparenting processes to the init process.
 */
proc_t *
proc_create(char *name)
{
        /*NOT_YET_IMPLEMENTED("PROCS: proc_create");*/
	proc_t * process;
	/* Allocating memory to process */
	process = (proc_t*) slab_obj_alloc(proc_allocator);
	/* Allocating parent process */
	process->p_pproc = curproc;
	/* Allocating id to process */
	process->p_pid = _proc_getid();
	
	KASSERT(PID_IDLE != process->p_pid || list_empty(&_proc_list)); /* pid can only be PID_IDLE if this is the first process */
        dbg(DBG_PRINT,"\n(GRADING1A 2.a) idle process pid correct");

        KASSERT(PID_INIT != process->p_pid || PID_IDLE == curproc->p_pid); /* pid can only be PID_INIT when creating from idle process */
        dbg(DBG_PRINT,"\n(GRADING1A 2.a) init process pid correct");
	if(strlen(name) < PROC_NAME_LEN) {
		strncpy(process->p_comm, name, strlen(name));
		(process->p_comm)[strlen(name)] = '\0';
	}
	else {
		strncpy(process->p_comm, name, PROC_NAME_LEN);
		(process->p_comm)[PROC_NAME_LEN-1] = '\0';
	}
	list_init(&(process->p_threads));
	list_init(&(process->p_children));

	process->p_status = 0;
	process->p_state = PROC_RUNNING;
	/* Check */
	list_init(&(process->p_wait.tq_list));
	process->p_wait.tq_size = 0;

	process->p_pagedir = pt_create_pagedir();
	list_insert_tail(proc_list(), &(process->p_list_link));
	
	if(curproc != NULL) {
		list_insert_tail(&(curproc->p_children), &(process->p_child_link));
	}
	
	/* VFS-related: */ 
	int i = 0;       
	for (i = 0; i < NFILES; i++)
    		process->p_files[i] = NULL;	/* open files */
        process->p_cwd = NULL;          /* current working dir */
		/*Items for VFS*/
	process->p_cwd = vfs_root_vn;

	if (process->p_cwd)
	{
		vref(process->p_cwd);
	}

        /* VM */
        process->p_brk = NULL;           /* process break; see brk(2) */
        process->p_start_brk = NULL;     /* initial value of process break */
        process->p_vmmap = NULL;         /* list of areas mapped into
                                          * process' user address
                                          * space */
	
	if(PID_INIT == process->p_pid)
	{
		proc_initproc = process;
	}
        return process;
}

/**
 * Cleans up as much as the process as can be done from within the
 * process. This involves:
 *    - Closing all open files (VFS)
 *    - Cleaning up VM mappings (VM)
 *    - Waking up its parent if it is waiting
 *    - Reparenting any children to the init process
 *    - Setting its status and state appropriately
 *
 * The parent will finish destroying the process within do_waitpid (make
 * sure you understand why it cannot be done here). Until the parent
 * finishes destroying it, the process is informally called a 'zombie'
 * process.
 *
 * This is also where any children of the current process should be
 * reparented to the init process (unless, of course, the current
 * process is the init process. However, the init process should not
 * have any children at the time it exits).
 *
 * Note: You do _NOT_ have to special case the idle process. It should
 * never exit this way.
 *
 * @param status the status to exit the process with
 */
void
proc_cleanup(int status)
{
	dbg(DBG_PRINT,"\n Inside proc_cleanup\n");
        /*-------------------------------------------------------------------------------------*/
	KASSERT(NULL != proc_initproc); /* should have an "init" process */
        dbg(DBG_PRINT,"\n(GRADING1A 2.b) have an \"init\" process\n");

	KASSERT(1 <= curproc->p_pid); /* this process should not be idle process */
        dbg(DBG_PRINT,"\n(GRADING1A 2.b) process not idle process\n");
 
	KASSERT(NULL != curproc->p_pproc); /* this process should have parent process */
        dbg(DBG_PRINT,"\n(GRADING1A 2.b) process has parent process\n");

	/*Assigning Status to the current process for as exit status*/
	curproc->p_status = status;
	curproc->p_state = PROC_DEAD;

	proc_t *p;
	/*Checking for child Processes*/
	if(list_empty(&(curproc->p_children)))
	{
	}
	else
	{
		/*Changing the child processes parent to init() using list iterator macro*/
		list_iterate_begin(&(curproc->p_children), p, proc_t, p_child_link)
		{
		p->p_pproc=proc_initproc; /*changing the parent*/
		list_insert_tail(&(proc_initproc->p_children), &(p->p_child_link)); /*adding the proc to the 	children list of init*/
		}list_iterate_end();
	}
	/*do_waitpid();*/

	KASSERT(NULL != curproc->p_pproc); /* this process should have parent process */
        dbg(DBG_PRINT,"\n(GRADING1A 2.b) process has parent process\n");
        if(!sched_queue_empty(&(curproc->p_pproc->p_wait)))
          {
            sched_broadcast_on(&(curproc->p_pproc->p_wait));
          }
	vput(curproc->p_cwd);

	int fd = 0;
	for(fd=0; fd<NFILES; fd++)
	{
	if(curproc->p_files[fd] !=NULL && curproc->p_files[fd]->f_refcount > 0)
		do_close(fd);
	}
	
	/*NOT_YET_IMPLEMENTED("PROCS: proc_cleanup");*/

	/*-------------------------------------------------------------------------------------*/
}

/*
 * This has nothing to do with signals and kill(1).
 *
 * Calling this on the current process is equivalent to calling
 * do_exit().
 *
 * In Weenix, this is only called from proc_kill_all.
 */
void
proc_kill(proc_t *p, int status)
{
        /*------------------------------------------------------------------------------------------------------*/      dbg(DBG_PRINT,"\n Inside proc_kill\n");
	if(p == curproc)
          {
            do_exit(status);
          } 
        else
          {
           kthread_t *kt;
           list_iterate_begin(&(p->p_threads), kt, kthread_t, kt_plink)
		{
		   kthread_cancel(kt, (void*)status);
		}list_iterate_end();  
	   }
	dbg(DBG_PRINT,"\n Done proc_kill\n");
	/*NOT_YET_IMPLEMENTED("PROCS: do_exit");*/
	/*------------------------------------------------------------------------------------------------------*/
}

/*
 * Remember, proc_kill on the current process will _NOT_ return.
 * Don't kill direct children of the idle process.
 *
 * In Weenix, this is only called by sys_halt.
 */
void
proc_kill_all()
{
        /*----------------------------------------------------------------------------------------------------*/
	proc_t *p;
	list_iterate_begin(&_proc_list, p, proc_t, p_list_link)
	{
		if(p != curproc && p->p_pid != PID_IDLE && p->p_pid != PID_INIT && p->p_pid != 2)/*Checking for daemon process with pid 2*/
		{
			/*if(p->p_pproc->p_pid != PID_IDLE) Not direct children of IDLE process
			{*/
			proc_kill(p, p->p_status);/*Kill the process passed with status 0*/
			/*}*/
		}
	}list_iterate_end();
	/*Atlast KILL itself*/
	
	if(curproc->p_pid != PID_IDLE && curproc->p_pid != PID_INIT && p->p_pid != 2)
	{
	proc_kill(curproc, curproc->p_status);
	}
	
	/*NOT_YET_IMPLEMENTED("PROCS: proc_kill_all");*/
	/*----------------------------------------------------------------------------------------------------*/
}

/*
 * This function is only called from kthread_exit.
 *
 * Unless you are implementing MTP, this just means that the process
 * needs to be cleaned up and a new thread needs to be scheduled to
 * run. If you are implementing MTP, a single thread exiting does not
 * necessarily mean that the process should be exited.
 */
void
proc_thread_exited(void *retval)
{
/*===========================================================================================*/
      int active_flag=0;
      kthread_t *kt;
	dbg(DBG_PRINT,"\n Inside proc_thread_exited\n");
      list_iterate_begin(&(curproc->p_threads), kt, kthread_t, kt_plink)
		{
		   if(kt->kt_state != KT_EXITED)
                     {
                      active_flag = 1;
                      break;
                     }
		}list_iterate_end();
      if(active_flag == 0)
        {
          proc_cleanup((int)(int *)retval); /*Calling the cleanup process*/
        }
	dbg(DBG_PRINT,"\n Done proc_thread_exited\n");
/* NOT_YET_IMPLEMENTED("PROCS: proc_thread_exited");*/
/*===========================================================================================*/
     
}

/* If pid is -1 dispose of one of the exited children of the current
 * process and return its exit status in the status argument, or if
 * all children of this process are still running, then this function
 * blocks on its own p_wait queue until one exits.
 *
 * If pid is greater than 0 and the given pid is a child of the
 * current process then wait for the given pid to exit and dispose
 * of it.
 *
 * If the current process has no children, or the given pid is not
 * a child of the current process return -ECHILD.
 *
 * Pids other than -1 and positive numbers are not supported.
 * Options other than 0 are not supported.
 */

pid_t
do_waitpid(pid_t pid, int options, int *status)
{
	proc_t *p;
	kthread_t *thr;
	KASSERT(pid == -1 || pid > 0);
	dbg(DBG_PRINT,"\ndo_waitpid(): pid %d passed to do waitpid\n", pid);

	/*If no children, error case*/
	if(list_empty(&curproc->p_children))
	{
		return -ECHILD;
	}
	
	if(pid == -1)
	{
	
		while( 1 )
		{
			list_iterate_begin(&curproc->p_children, p,proc_t,p_child_link){
			
			KASSERT(NULL != p);
			dbg(DBG_PRINT,"\n(GRADING1A 2.c) process not NULL\n");

			KASSERT(-1 == pid || p->p_pid == pid);
			dbg(DBG_PRINT,"\n(GRADING1A 2.c) process with pid -1 found\n");

			if(p->p_state==PROC_DEAD)
			{
				KASSERT(NULL != p->p_pagedir);
                      		dbg(DBG_PRINT,"\n(GRADING1A 2.c) process has pagedir\n");

				*status = p->p_status;
				pid = p->p_pid;
				/*destory all process threads*/
					
				list_iterate_begin(&p->p_threads,thr,kthread_t,kt_plink){
				kthread_destroy(thr);
				/* thr points to a thread to be destroied */
				KASSERT(KT_EXITED == thr->kt_state);
				dbg(DBG_PRINT,"\n(GRADING1A 2.c) thr points to a thread to be destroied\n");
				}list_iterate_end();

				list_remove(&p->p_child_link);
				list_remove(&p->p_list_link);
				return pid;
			}
			}list_iterate_end();
	
			sched_sleep_on(&curproc->p_wait);
		}
		
	} else {
	
		/*PID > 0 specific process*/
		list_iterate_begin(&curproc->p_children, p , proc_t, p_child_link){
			KASSERT(NULL != p );
			dbg(DBG_PRINT,"\n(GRADING1A 2.c) process not NULL\n");
			if(p->p_pid == pid)
			{
				KASSERT(-1 == pid || p->p_pid == pid);
				dbg(DBG_PRINT,"\n(GRADING1A 2.c) process with pid %d found\n", p->p_pid);
                 		KASSERT(NULL != p->p_pagedir);
                 		dbg(DBG_PRINT,"\n(GRADING1A 2.c) process has pagedir\n");
				
				sched_sleep_on(&curproc->p_wait);
				*status = p->p_status;
				pid = p->p_pid;
				/*destory all process threads*/
				list_iterate_begin(&p->p_threads,thr,kthread_t,kt_plink){
					kthread_destroy(thr);
					KASSERT(KT_EXITED == thr->kt_state);
					dbg(DBG_PRINT,"\n(GRADING1A 2.c) thr points to a thread to be destroied\n");
				}list_iterate_end();
				list_remove(&p->p_child_link);
				list_remove(&p->p_list_link);
				return pid;
			}
		}list_iterate_end();
	}
	
	return -ECHILD;
}

/*
 * Cancel all threads, join with them, and exit from the current
 * thread.
 *
 * @param status the exit status of the process
 */
void
do_exit(int status)
{
/*------------------------------------------------------------------------------------------------------*/ dbg(DBG_PRINT,"\n Inside proc.c do_exit\n");
    kthread_t *kt;
    list_iterate_begin(&(curproc->p_threads), kt, kthread_t, kt_plink)
		{
	            kthread_cancel(kt, (void *)(status));
		}list_iterate_end(); 
   dbg(DBG_PRINT,"\n Done proc.c do_exit\n");
/*NOT_YET_IMPLEMENTED("PROCS: do_exit");*/
/*------------------------------------------------------------------------------------------------------*/
}

size_t
proc_info(const void *arg, char *buf, size_t osize)
{
        const proc_t *p = (proc_t *) arg;
        size_t size = osize;
        proc_t *child;

        KASSERT(NULL != p);
        KASSERT(NULL != buf);

        iprintf(&buf, &size, "pid:          %i\n", p->p_pid);
        iprintf(&buf, &size, "name:         %s\n", p->p_comm);
        if (NULL != p->p_pproc) {
                iprintf(&buf, &size, "parent:       %i (%s)\n",
                        p->p_pproc->p_pid, p->p_pproc->p_comm);
        } else {
                iprintf(&buf, &size, "parent:       -\n");
        }

#ifdef __MTP__
        int count = 0;
        kthread_t *kthr;
        list_iterate_begin(&p->p_threads, kthr, kthread_t, kt_plink) {
                ++count;
        } list_iterate_end();
        iprintf(&buf, &size, "thread count: %i\n", count);
#endif

        if (list_empty(&p->p_children)) {
                iprintf(&buf, &size, "children:     -\n");
        } else {
                iprintf(&buf, &size, "children:\n");
        }
        list_iterate_begin(&p->p_children, child, proc_t, p_child_link) {
                iprintf(&buf, &size, "     %i (%s)\n", child->p_pid, child->p_comm);
        } list_iterate_end();

        iprintf(&buf, &size, "status:       %i\n", p->p_status);
        iprintf(&buf, &size, "state:        %i\n", p->p_state);

#ifdef __VFS__
#ifdef __GETCWD__
        if (NULL != p->p_cwd) {
                char cwd[256];
                lookup_dirpath(p->p_cwd, cwd, sizeof(cwd));
                iprintf(&buf, &size, "cwd:          %-s\n", cwd);
        } else {
                iprintf(&buf, &size, "cwd:          -\n");
        }
#endif /* __GETCWD__ */
#endif

#ifdef __VM__
        iprintf(&buf, &size, "start brk:    0x%p\n", p->p_start_brk);
        iprintf(&buf, &size, "brk:          0x%p\n", p->p_brk);
#endif

        return size;
}

size_t
proc_list_info(const void *arg, char *buf, size_t osize)
{
        size_t size = osize;
        proc_t *p;

        KASSERT(NULL == arg);
        KASSERT(NULL != buf);

#if defined(__VFS__) && defined(__GETCWD__)
        iprintf(&buf, &size, "%5s %-13s %-18s %-s\n", "PID", "NAME", "PARENT", "CWD");
#else
        iprintf(&buf, &size, "%5s %-13s %-s\n", "PID", "NAME", "PARENT");
#endif

        list_iterate_begin(&_proc_list, p, proc_t, p_list_link) {
                char parent[64];
                if (NULL != p->p_pproc) {
                        snprintf(parent, sizeof(parent),
                                 "%3i (%s)", p->p_pproc->p_pid, p->p_pproc->p_comm);
                } else {
                        snprintf(parent, sizeof(parent), "  -");
                }

#if defined(__VFS__) && defined(__GETCWD__)
                if (NULL != p->p_cwd) {
                        char cwd[256];
                        lookup_dirpath(p->p_cwd, cwd, sizeof(cwd));
                        iprintf(&buf, &size, " %3i  %-13s %-18s %-s\n",
                                p->p_pid, p->p_comm, parent, cwd);
                } else {
                        iprintf(&buf, &size, " %3i  %-13s %-18s -\n",
                                p->p_pid, p->p_comm, parent);
                }
#else
                iprintf(&buf, &size, " %3i  %-13s %-s\n",
                        p->p_pid, p->p_comm, parent);
#endif
        } list_iterate_end();
        return size;
}
