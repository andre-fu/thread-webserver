#include <assert.h>
#include <stdlib.h>
#include <ucontext.h>
#include "thread.h"
#include "interrupt.h"


typedef enum {
	T_FREE = 0,
	T_READY = 1,
	T_RUNNING = 2,
	T_EXITED = 3
} t_state;

typedef struct Node{
    Tid thread_id; // the data held inside thread structures
	struct Node *next; //ptr to next Node block in LL 
} Node;

typedef struct Queue {
    struct Node *head; //ptr to head of queue
    int num_threads; // num threads in this queue
} Queue;


int insert(Queue *queue, Tid i_key) {   
    Node *i_node;
    i_node = (Node *)malloc(sizeof(Node));
    i_node->next = NULL;
    i_node->thread_id = i_key;
    if (queue->head == NULL) {
        // if there's nothing in the queue
        (queue->head) = i_node;
        (queue->num_threads) = 1;
        return 1;
    } else {
        Node *curr_node;
        curr_node = (queue->head); //the address of the head
        while (curr_node->next != NULL){
            curr_node = curr_node->next;
        }
        curr_node->next = i_node;
        (queue->num_threads)++;
        return 1;
    }
    return -1;
}

void printll(Queue *queue) {
    Node *curr_node;    
    curr_node = (queue->head);
    printf("---------- PRINTING %d NODES ------------- \n", (queue->num_threads));
    while (curr_node != NULL){
        printf("\t node id %d \n", curr_node->thread_id);
        curr_node = curr_node->next;
    }
    printf("---------- Finished Printing ------------- \n");
}

// Tid pop_head(Queue *queue) {
//     if (queue->head == NULL) {
//         printf("what? no head?? *smashes phone* \n");
//         return -1;
//     } else {
//         Tid *ret_tid; // ptr to returning tid
//         ret_tid = &(queue->head->thread_id); // point to the thread id
//         queue->head = queue->head->next; // set head to next
//         (queue->num_threads)--;
//         return *ret_tid; //dereference tid and return the actual Tid
//     }
// }

Tid pop_head(Queue *queue) {
	if (queue->head == NULL) {
		// nothing to remove from head
		return THREAD_NONE;
	} else {
		Tid ret_tid = (queue->head->thread_id);
        Node *twodown = (queue->head->next);
        free(queue->head);
		(queue->head) = twodown;
        (queue->num_threads)--;
		return ret_tid;
	}
}

// Tid pop_head(Queue *queue) {
//     if (queue->head == NULL) {
//         printf("what? no head?? *smashes phone* \n");
//         return THREAD_NONE;
//     } 
// 	if (t_arr[thread_id()].state == T_RUNNING && total_threads == 1) {
// 		return THREAD_NONE;
// 	} else {
//         Tid ret_tid = (queue->head->thread_id); // point to the thread id
//         queue->head = queue->head->next; // set head to next
//         (queue->num_threads)--;
//         return ret_tid; //dereference tid and return the actual Tid
//     }
// }

Tid find_remove(Queue *queue, Tid key) {
	if (queue->head == NULL){
		// nothing in queue to remove
		return THREAD_NONE;
	} else if (queue->head->thread_id == key) {
		return pop_head(queue);
	} else {
		Node *curr_node;
		curr_node = (queue->head);
		Node *prev_node;
		prev_node = (queue->head);
		while (curr_node->thread_id != key){
			prev_node = curr_node;
			curr_node = curr_node->next;
		}
		Tid ret_tid = (curr_node->thread_id);
		prev_node->next = curr_node->next;
		if (curr_node->next == NULL) {
			printf("popped everything in queue, this is the last element \n");
		}
		free(curr_node);
		(queue->num_threads)--;
		return ret_tid;
	}
}


Queue ready_queue;

void main() {

    // ready_queue = (Queue *)malloc(sizeof(Queue));

    // Queue *ready_queue;
    // ready_queue = (Queue *)malloc(sizeof(Queue));
    // ready_queue->head = NULL;
    // ready_queue->num_threads = 0;

    insert(&ready_queue, 1);
    insert(&ready_queue, 2);
    insert(&ready_queue, 3);
    insert(&ready_queue, 4);
    insert(&ready_queue, 5);
    insert(&ready_queue, 6);
    insert(&ready_queue, 7);
    insert(&ready_queue, 8);
    insert(&ready_queue, 9);
    insert(&ready_queue, 10);
    
    printll(&ready_queue);

    Tid removed_tid = find_remove(&ready_queue, 3);
    printf("We removed node %d \n", removed_tid);
    printll(&ready_queue);

    Tid rq_tid = pop_head(&ready_queue);
    printf("the head was node %d \n", rq_tid);
    printll(&ready_queue);

    pop_head(&ready_queue);
    pop_head(&ready_queue);
    pop_head(&ready_queue);
    pop_head(&ready_queue);
    pop_head(&ready_queue);
    pop_head(&ready_queue);
    pop_head(&ready_queue);
    pop_head(&ready_queue);
    pop_head(&ready_queue);
    pop_head(&ready_queue);
    pop_head(&ready_queue);
    pop_head(&ready_queue);
    printll(&ready_queue);
    insert(&ready_queue, 10);
    insert(&ready_queue, 9);
    insert(&ready_queue, 8);
    insert(&ready_queue, 7);
    printll(&ready_queue);
    printf("rq before removal \n");
    find_remove(&ready_queue, 10);
    find_remove(&ready_queue, 9);
    printll(&ready_queue);
    pop_head(&ready_queue);
    printll(&ready_queue);
}