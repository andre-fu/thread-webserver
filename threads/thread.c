#include <assert.h>
#include <stdlib.h>
#include <ucontext.h>
#include "thread.h"
#include "interrupt.h"


typedef enum {
	T_FREE = 0,
	T_READY = 1,
	T_RUNNING = 2,
	T_EXITED = 3,
	T_SLEEP = 4, 
	T_KILLED = 5
} t_state;

typedef struct Node {
	Tid thread_id;
	struct Node *next;
} Node;

// typedef struct Queue {
// 	struct Node *head; //ptr to head of queue
// 	int num_threads;
// } Queue;

/* This is the wait queue structure */
typedef struct wait_queue {
	struct Node *head; //ptr to head of queue
	int num_threads;
} Queue;

/* This is the thread control block */
typedef struct Thread {
	Tid thread_id;
	t_state state; 
	ucontext_t context;
	void *stack_ptr; //keep tracks of threads stack
	Queue *wait_queue;
} Thread;


Thread t_arr[THREAD_MAX_THREADS];
Queue ready_queue;
Queue free_tid;
volatile Tid curr_run; 
int total_threads = 0; // sum ready + exited (but not freed or sleeping)

void insert(Queue *queue, Tid id) {
	int enabled = interrupts_set(0);
	Node *node = (Node *)malloc(sizeof(Node));
	node->thread_id = id;
	node->next = NULL;

	if (queue->head == NULL) {
		// if nothing in queue
		queue->head = node;
		queue->head->next = NULL;
		(queue->num_threads)++;
	} else {
		Node *tmp_node = queue->head;
		while (tmp_node->next != NULL) {
			tmp_node = tmp_node->next;
		}
		tmp_node->next = node;
		(queue->num_threads)++;
	}
	interrupts_set(enabled);
}

Tid pop(Queue *queue, Tid key){
	int enabled = interrupts_set(0);
	// error checking
	if (key == THREAD_SELF) {
		key = thread_id();
	} else if (key == THREAD_ANY && queue->num_threads > 0) {
		// if (t_arr[thread_id()].state == T_RUNNING && total_threads == 1 ) {
		// 	interrupts_set(enabled);
		// 	return THREAD_NONE;
		// }
		key = queue->head->thread_id;
	}


	Node *tmp_node = queue->head;
	if (tmp_node == NULL){
		interrupts_set(enabled);
		return THREAD_INVALID;
	}
	if (tmp_node->thread_id == key){
		// if want node is at head
		Node *two_down = tmp_node->next;
		free(queue->head);
		queue->head = two_down;
		(queue->num_threads)--;
		interrupts_set(enabled);
		return key;
	}
	else {
		while(tmp_node != NULL){
			if (tmp_node->next != NULL){ 
			// prevents seg fault when id isn't in list to break and go to thread_invalid
				if (tmp_node->next->thread_id == key){
					if (tmp_node->next->next != NULL){
						Node *two_down = tmp_node->next->next;
						free(tmp_node->next);
						tmp_node->next = two_down;
					}
					else {
						free(tmp_node->next);
						tmp_node->next = NULL;
					}
					(queue->num_threads)--;
					interrupts_set(enabled);
					return key;
				}
			}
			tmp_node = tmp_node->next;
		}
	}
	interrupts_set(enabled);
	return THREAD_INVALID;
}

void printll(Queue *queue) {
	int enabled = interrupts_set(0);
    Node *curr_node;    
    curr_node = (queue->head);
    printf("---------- PRINTING %d NODES ------------- \n", (queue->num_threads));
    while (curr_node != NULL){
        printf("\t node id %d \n", curr_node->thread_id);
        curr_node = curr_node->next;
    }
    printf("---------- Finished Printing ------------- \n");
	interrupts_set(enabled);
}


void
thread_init(void)
{
	for (int i = 0; i < THREAD_MAX_THREADS; i++) {
		t_arr[i].state = T_FREE;
	}
	// initial thread set to 0
	t_arr[0].state = T_RUNNING;
	t_arr[0].wait_queue = wait_queue_create();
	getcontext(&(t_arr[0].context));
	curr_run = 0;
	total_threads++;
}

Tid
thread_id()
{
	return curr_run;
}


void free_exited() {
	int enabled = interrupts_set(0);
	int i = 0;
	for(i = 0; i < THREAD_MAX_THREADS; i++) {
		if (t_arr[i].state == T_EXITED && i != thread_id()) {
			t_arr[i].state = T_FREE;
			free(t_arr[i].stack_ptr);
			wait_queue_destroy(t_arr[i].wait_queue);
			// t_arr[i].wait_queue = NULL;
		}
	}
	interrupts_set(enabled);
}

/* thread starts by calling thread_stub. The arguments to thread_stub are the
 * thread_main() function, and one argument to the thread_main() function. */
void
thread_stub(void (*thread_main)(void *), void *arg)
{
	// Tid ret;
	interrupts_on();
	thread_main(arg); // call thread_main() function with arg
	thread_exit();
	exit(0);
}


Tid
thread_create(void (*fn) (void *), void *parg)
{	
	int enabled = interrupts_set(0);
	int i = 0;
	for (i = 0; i < THREAD_MAX_THREADS; i++) {
		if ( t_arr[i].state == T_FREE) { 
			break;
		} 
		if (t_arr[i].state == T_EXITED) {
			free(t_arr[i].stack_ptr);
			break;
		}
	}

	if (i == THREAD_MAX_THREADS) {
		interrupts_set(enabled);
		return THREAD_NOMORE;
	}
	void *t_sp = malloc(sizeof(unsigned long)*THREAD_MIN_STACK);
	if (t_sp == NULL) {
		interrupts_set(enabled);
		return THREAD_NOMEMORY;
	}

	getcontext(&(t_arr[i].context));
	
	t_arr[i].state = T_READY;
	t_arr[i].wait_queue = wait_queue_create();
	t_arr[i].thread_id = i;
	t_arr[i].stack_ptr = t_sp;

	t_arr[i].context.uc_mcontext.gregs[REG_RIP] = (greg_t) thread_stub;
	t_arr[i].context.uc_mcontext.gregs[REG_RDI] = (greg_t) fn; 
	t_arr[i].context.uc_mcontext.gregs[REG_RSI] = (greg_t) parg; 
	t_arr[i].context.uc_mcontext.gregs[REG_RSP] = (greg_t)t_sp + THREAD_MIN_STACK - 8;

	insert(&ready_queue, i);
	total_threads++;
	interrupts_set(enabled);
	return i;
}

Tid
thread_yield(Tid want_tid)
{
	int enabled = interrupts_set(0);	
	// error checking if want_tid is allowed
	if (want_tid == curr_run || want_tid == THREAD_SELF ){
		interrupts_set(enabled);
		return thread_id(); 
	}
	if (want_tid < THREAD_MAX_THREADS && want_tid >= -2 ){
		if (want_tid == THREAD_ANY && ready_queue.head == NULL){
			// if we want any thread but none to give
			interrupts_set(enabled);
			return THREAD_NONE;
		}
		if (want_tid >= 0 && t_arr[want_tid].state == T_RUNNING) {
			// we're currently running that thread
			interrupts_set(enabled);
			return THREAD_INVALID;
		} 
	} 
	if (want_tid > total_threads || want_tid > THREAD_MAX_THREADS || want_tid < -2) {
		interrupts_set(enabled);
		return THREAD_INVALID;
	}

	if (t_arr[curr_run].state == T_KILLED) {
		interrupts_set(enabled);
		thread_exit();
		return THREAD_NONE;
	}

	Tid next_t = pop(&ready_queue, want_tid);
	if (next_t < 0){
		// if the next thread isn't a valid option ret thread invalid
		free_exited();
		// the test code CHECKS that the threads that are exit are freed
		interrupts_set(enabled);
		return THREAD_INVALID;
	}
	volatile int switched = 0;
	if (t_arr[curr_run].state == T_RUNNING){
		// insert onto ready queue
		t_arr[curr_run].state = T_READY;
		insert(&ready_queue, curr_run);
	}
	if (t_arr[next_t].state != T_KILLED) {
		t_arr[next_t].state = T_RUNNING;
	}
	getcontext(&(t_arr[curr_run].context));
	if (switched == 0){
		// start actually switching
		curr_run = next_t;
		switched = 1;
		setcontext(&(t_arr[curr_run].context));
	}
	interrupts_set(enabled);
	return next_t;
}


void
thread_exit()
{
	int enabled = interrupts_set(0);
	// error checking if this is the last thread to be run nothing left to exit/yield
	if (total_threads == 1 && t_arr[curr_run].wait_queue->head == NULL){
		interrupts_set(enabled);
		exit(0);
	}
	t_arr[curr_run].state = T_EXITED;
	total_threads--;
	thread_wakeup(t_arr[curr_run].wait_queue, 1); // dump wq onto rq
	interrupts_set(enabled);
	thread_yield(THREAD_ANY);
}

Tid
thread_kill(Tid tid)
{
	int enabled = interrupts_set(0);
	if (tid < 0 || tid > THREAD_MAX_THREADS ||
		tid == thread_id() || tid > total_threads ){ 
		interrupts_set(enabled);
		return THREAD_INVALID;
	} else {
		t_arr[tid].state = T_KILLED;
		interrupts_set(enabled);
		return tid;
	} 
}

/*******************************************************************
 * Important: The rest of the code should be implemented in Lab 3. *
 *******************************************************************/

/* make sure to fill the wait_queue structure defined above */
struct wait_queue *
wait_queue_create()
{
	struct wait_queue *wq;
	wq = (struct wait_queue *)malloc(sizeof(struct wait_queue));
	assert(wq);
	wq->num_threads = 0;
	wq->head = NULL;
	return wq;
}

void
wait_queue_destroy(struct wait_queue *wq)
{
	int enabled = interrupts_set(0);
	int val = 1;
	while (val > 0){
		val = pop(wq, THREAD_ANY);
	}
	free(wq);
	wq = NULL;
	free_exited();
	interrupts_set(enabled);
}

Tid
thread_sleep(struct wait_queue *queue)
{	
	int enabled = interrupts_set(0);
	if (queue == NULL){
		// if the wait queue is empty
		interrupts_set(enabled);
		return THREAD_INVALID;
	}
	if (total_threads == 1){
		// if there are no more threads to put to sleep
		interrupts_set(enabled);
		return THREAD_NONE;
	} else {
		t_arr[curr_run].state = T_SLEEP;
		insert(queue, curr_run); // insert sleeping node into wait queue
		total_threads--;
		interrupts_set(enabled);
		return thread_yield(THREAD_ANY);
	}
	interrupts_set(enabled);
	return THREAD_FAILED;
}

/* when the 'all' parameter is 1, wakeup all threads waiting in the queue.
 * returns whether a thread was woken up on not. */
int
thread_wakeup(struct wait_queue *queue, int all)
{
	int enabled = interrupts_set(0);
	if (queue == NULL || queue->num_threads == 0){
		interrupts_set(enabled);
		return 0;
		// return 0 since nothing was woken up
	} 
	if (all == 1) { // want to wake everything up
		int threads_woken = 0;
		int num = 1;
		while(num == 1){
			num = thread_wakeup(queue, 0);
			threads_woken += num;
		}
		interrupts_set(enabled);
		return threads_woken--; // decrement 1 to account for the inital we incremented
	}
	else if (all == 0){
		// pop the first element from the wait_queue
		Tid wakeup_t = pop(queue, THREAD_ANY);
		if (wakeup_t == THREAD_NONE) {
			interrupts_set(enabled);
			return 0;
		}
		else {
			if (t_arr[wakeup_t].state != T_KILLED) {
				// if the thread to be woken up hasnt been killed by another thread
				t_arr[wakeup_t].state = T_READY;
			}
			total_threads++;
			insert(&ready_queue, wakeup_t);
			interrupts_set(enabled);
			return 1;
		}
	}
	return -1;
}

/* suspend current thread until Thread tid exits */
Tid
thread_wait(Tid tid)
{
	int enabled = interrupts_set(0);
	// some error checking
	if (tid < 0 || tid > THREAD_MAX_THREADS || tid == curr_run ||
		t_arr[tid].state == T_FREE || t_arr[tid].state == T_EXITED) {
		interrupts_set(enabled);
		return THREAD_INVALID;
	} 
	Tid sleep_t = thread_sleep(t_arr[tid].wait_queue); 
	if (sleep_t < 0) { 
		exit(0); 
	} 
	if (t_arr[curr_run].state == T_KILLED || t_arr[curr_run].state == T_EXITED ) {
		thread_exit();
	}
	free_exited();
	interrupts_set(enabled);
	return tid;
}

typedef enum {
	L_OPEN = 1,
	L_CLOSED = -1
} l_state;

struct lock {
	Tid locked_tid; // the tid of the currently holding lock
	Queue *lock_queue; // tids of threads waiting for wq
	l_state lock_state; // current state of the lock
};


struct lock *
lock_create()
{
	int enabled = interrupts_set(0);
	struct lock *lock;
	lock = malloc(sizeof(struct lock));
	assert(lock);

	lock->locked_tid = THREAD_NONE;
	lock->lock_state = L_OPEN;
	lock->lock_queue = wait_queue_create();

	interrupts_set(enabled);
	return lock;
}

void
lock_destroy(struct lock *lock)
{
	int enabled = interrupts_set(0);
	assert(lock != NULL);
	wait_queue_destroy(lock->lock_queue);
	free(lock);
	interrupts_set(enabled);
}

void
lock_acquire(struct lock *lock)
{
	// SUPER shitty way of doing this
	int enabled = interrupts_set(0);
	assert(lock != NULL);
	while (lock->lock_state == L_CLOSED){
		thread_sleep(lock->lock_queue);
	}
	lock->lock_state = L_CLOSED;
	lock->locked_tid = curr_run;
	interrupts_set(enabled);
}

void
lock_release(struct lock *lock)
{
	int enabled = interrupts_set(0);
	assert(lock != NULL);
	if (lock->lock_state == L_CLOSED && curr_run == lock->locked_tid){
		lock->locked_tid = THREAD_NONE;
		lock->lock_state = L_OPEN;
		thread_wakeup(lock->lock_queue, 1); // wake up all the threads in queue
	}
	interrupts_set(enabled);
}

struct cv {
	Queue *cv_queue;
};

struct cv *
cv_create()
{
	int enabled = interrupts_set(0);
	struct cv *cv;

	cv = malloc(sizeof(struct cv));
	assert(cv);
	cv->cv_queue = wait_queue_create();

	interrupts_set(enabled);
	return cv;
}

void
cv_destroy(struct cv *cv)
{
	int enabled = interrupts_set(0);
	assert(cv != NULL);
	wait_queue_destroy(cv->cv_queue);
	free(cv);
	interrupts_set(enabled);
}

void
cv_wait(struct cv *cv, struct lock *lock)
{
	int enabled = interrupts_set(0);
	assert(cv != NULL);
	assert(lock != NULL);
	if (lock->locked_tid == curr_run){
		lock_release(lock);
		thread_sleep(cv->cv_queue);
	}
	lock_acquire(lock);
	interrupts_set(enabled);
}

void
cv_signal(struct cv *cv, struct lock *lock)
{
	int enabled = interrupts_set(0);
	assert(cv != NULL);
	assert(lock != NULL);
	if (lock->locked_tid == curr_run){
		thread_wakeup(cv->cv_queue, 0);
	}
	interrupts_set(enabled);
}

void
cv_broadcast(struct cv *cv, struct lock *lock)
{
	int enabled = interrupts_set(0);
	assert(cv != NULL);
	assert(lock != NULL);
	if (lock->locked_tid == curr_run){
		thread_wakeup(cv->cv_queue, 1);
	}
	interrupts_set(enabled);
}