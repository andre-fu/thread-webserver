#include "request.h"
#include "server_thread.h"
#include "common.h"

#define TABLE_SIZE 9000000

typedef struct node {
	int fkey;
	struct node* next;
} node;

typedef struct queue {
	node *head;
	int size; 
} queue;

typedef struct fentry {
	char *fname;
	struct file_data *fdata;
	int in_use;
} fentry;

typedef struct cache {
	int size; 
	int max_cache_size;
	int table_size;
	queue *lru_queue;
	struct fentry **ftable;
} cache;

struct server {
	int nr_threads;
	int max_requests;
	int max_cache_size;
	int exiting;
	pthread_t **worker_pool; //array of worker threads
	int *buffer; // the actual buffer of fds
	int in; 
	int out;
	int count; // buffer request counter

	cache *cache;
};

// global functions
pthread_mutex_t lock;
pthread_mutex_t cache_l;
pthread_cond_t full;
pthread_cond_t empty;


fentry *cache_lookup(struct server *sv, char *fname);
int cache_evict(struct server *sv, int reqsize );
int table_delete(struct server *sv, int reqsize);
fentry *cache_insert(struct server *sv, struct file_data *fdata);
fentry* table_insert(struct server *sv, struct file_data *fdata);

void server_initalization(struct server *sv, int nr_threads, 
    int max_requests, int max_cache_size) {
    
    sv->nr_threads = nr_threads;
    sv->buffer = NULL;
    sv->worker_pool = NULL; // to be filled in later
    sv->count = 0;
    sv->exiting = 0;
    sv->in = 0;
    sv->max_requests = max_requests;
    sv->max_cache_size = max_cache_size;
    sv->out = 0;
    if (max_cache_size > 0 ) {
        sv->cache = (cache *)malloc(sizeof(cache));
        sv->cache->table_size = TABLE_SIZE;
        sv->cache->lru_queue = (queue *)malloc(sizeof(queue));
        sv->cache->ftable = (fentry **)malloc(TABLE_SIZE*sizeof(fentry*));
        sv->cache->size = 0;
        sv->cache->max_cache_size = max_cache_size;

        for (int i = 0; i < TABLE_SIZE; i++) {
            sv->cache->ftable[i] = NULL;
        }
    } else { 
        sv->cache = NULL;
    }
}

void insert_front(queue *ll, int fkey) {
	if (ll == NULL) return;
	
	node *new_node = (node *)malloc(sizeof(node));
	new_node->fkey = fkey;
	new_node->next = NULL;

	if (ll->head == NULL) {
		ll->head = new_node;
		(ll->size)++;
		return;
	} else {
		new_node->next = ll->head;
		ll->head = new_node;
		(ll->size)++;
		return;
	}
	return;
}

void insert_end(queue *ll, int fkey) {
	if (ll == NULL) return; 

	node *new_node = (node *)malloc(sizeof(node));
	new_node->fkey = fkey;
	new_node->next = NULL;

	if (ll->head == NULL) {
		ll->head = new_node; 
		(ll->size)++;
		return;
	} else {
		node *tmp;
		tmp = ll->head;
		while (tmp->next != NULL) {
			tmp = tmp->next;
		}
		tmp->next = new_node;
		(ll->size)++;
		return;
	}
}

void pop_any(queue *ll, int fkey) {
	if (ll == NULL || ll->head == NULL) return;

	node *prev = ll->head;
	node *curr = ll->head->next;

	if (prev->fkey == fkey){
		ll->head = ll->head->next;
		free(prev);
		return;
	}
	while (curr->next != NULL) {
		if (curr->fkey == fkey ){
			prev->next = curr->next;
			free(curr);
			(ll->size)--;
			return;
		}
		prev = curr;
		curr = curr->next;
	}
}

void update (struct server *sv, int fkey){
	pop_any(sv->cache->lru_queue, fkey);
	insert_end(sv->cache->lru_queue, fkey);
}

long get_hash(struct server *sv, char *fname) {
	unsigned long hash = 5381;
	int c;
	while ((c = *fname++) != '\0') {
		hash = ((hash << 5) + hash) + c;
	}
	long hash_ret = (long)  hash % TABLE_SIZE;
	return hash_ret;
}

fentry *cache_lookup(struct server *sv, char *fname) {
	long hash = get_hash(sv, fname); // get index of the file_name

	while(sv->cache->ftable != NULL && sv->cache->ftable[hash] != NULL && strcmp(sv->cache->ftable[hash]->fname, fname) != 0) {
		hash++;
		hash = hash % TABLE_SIZE ;
	}
	if(sv->cache->ftable[hash] != NULL && strcmp(sv->cache->ftable[hash]->fname, fname) == 0) {
		return sv->cache->ftable[hash];
	}
	return NULL;
}

int cache_evict(struct server *sv, int reqsize ) {
	if (reqsize > sv->cache->max_cache_size) return 0;
	if (sv->max_cache_size - sv->cache->size > reqsize) return 1;
	if (sv->cache->lru_queue->head == NULL) return 0;
	return table_delete(sv, reqsize);
}

int table_delete(struct server *sv, int reqsize) {
	queue *ll = sv->cache->lru_queue;
	cache *cache = sv->cache; // to make < 80 characters lol
	node *curr = ll->head;

	while (curr != NULL && (sv->max_cache_size - cache->size) < reqsize) {
		fentry *item = cache->ftable[curr->fkey];
		if (item != NULL && item->in_use == 0) {
			cache->size -= item->fdata->file_size;
			free(item->fdata);	
			free(item->fname);
			free(item);
			cache->ftable[curr->fkey] = NULL;
			pop_any(ll, curr->fkey);
		}
		curr = curr->next;
	}
	if ((sv->max_cache_size - cache->size) >= reqsize) {
		return 1;
	} else { 
		return 0;
	}
}

fentry *cache_insert(struct server *sv, struct file_data *fdata) {
	fentry *entry = cache_lookup(sv, fdata->file_name);
	if (entry != NULL) return entry;
	if (cache_evict(sv, fdata->file_size) == 1) 
		return table_insert(sv, fdata);
	return entry;
}

fentry *create_entry(struct server *sv, struct file_data *fdata) {
	fentry *entry = (fentry*)malloc(sizeof(struct fentry));

	entry->fname = (char *)malloc((strlen(fdata->file_name) + 1) * sizeof(char)); // is strlen allowed to be in here
	strcpy(entry->fname , fdata->file_name); // not allowed to do direct string assignment
	entry->fname[strlen(fdata->file_name)] = '\0';

	entry->fdata = fdata;
	entry->in_use = 0;
	
	return entry;
}

fentry* table_insert(struct server *sv, struct file_data *fdata) {
	long hash = get_hash(sv, fdata->file_name);
	
	// avoiding collisions and repeated words
	while(sv->cache->ftable != NULL && sv->cache->ftable[hash] != NULL && strcmp(sv->cache->ftable[hash]->fname, fdata->file_name) != 0) {
		hash++;
		hash%=sv->cache->table_size;
	}
	
	fentry *entry = create_entry(sv, fdata);

	sv->cache->ftable[hash] = entry;
	sv->cache->size += fdata->file_size;
	insert_front(sv->cache->lru_queue, hash);
	return sv->cache->ftable[hash];
}


/* static functions */

/* initialize file data */
static struct file_data *
file_data_init(void)
{
	struct file_data *data;

	data = Malloc(sizeof(struct file_data));
	data->file_name = NULL;
	data->file_buf = NULL;
	data->file_size = 0;
	return data;
}

/* free all file data */
static void
file_data_free(struct file_data *data)
{
	free(data->file_name);
	free(data->file_buf);
	free(data);
}

static void
do_server_request(struct server *sv, int connfd)
{
	int ret;
	struct request *rq;
	struct file_data *data;

	data = file_data_init();

	/* fill data->file_name with name of the file being requested */
	rq = request_init(connfd, data);
	if (!rq) {
		file_data_free(data);
		return;
	}

	if (sv->max_cache_size > 0) {
		pthread_mutex_lock(&cache_l);
		fentry *entry = cache_lookup(sv, data->file_name);
		if (entry != NULL) {
			data = entry->fdata;
			request_set_data(rq, data);
			if (entry != NULL) entry->in_use++;
			update(sv, get_hash(sv, data->file_name));
			pthread_mutex_unlock(&cache_l);

			request_sendfile(rq);

			pthread_mutex_lock(&cache_l);
			if (entry != NULL ) entry->in_use--;
			pthread_mutex_unlock(&cache_l);

			goto out;
		} else if (entry == NULL) {
			pthread_mutex_unlock(&cache_l);

			ret = request_readfile(rq);
			if (ret == 0)	goto out; /* couldn't read file */

			pthread_mutex_lock(&cache_l);
			entry = cache_insert(sv, data); // only if it can fit but i guess the check can be done in here
			request_set_data(rq, data);
			if(entry != NULL) {
				entry->in_use++;
				update(sv, get_hash(sv, data->file_name));
			}
			pthread_mutex_unlock(&cache_l);

			request_sendfile(rq);

			pthread_mutex_lock(&cache_l);
			if(entry != NULL) entry->in_use--;
			pthread_mutex_unlock(&cache_l); // TODO: pls help me

			goto out;
		}
	} else {
		/* read file, 
		* fills data->file_buf with the file contents,
		* data->file_size with file size. */
		ret = request_readfile(rq);
		if (ret == 0) { /* couldn't read file */
			goto out;
		}
		/* send file to client */
		request_sendfile(rq);
	}

out:
	request_destroy(rq);
	// file_data_free(data);
}


int read_buf(struct server *sv){
	pthread_mutex_lock(&lock); 
	while (sv->count == 0 ){ // while nothing wait on empty
		pthread_cond_wait(&empty, &lock);
		if (sv->exiting){
			pthread_mutex_unlock(&lock);
			pthread_exit(NULL);
		}
	}
	int connfd = sv->buffer[sv->out]; //read fd from buf
	if (sv->count == sv->max_requests){
		pthread_cond_broadcast(&full);
	}
	sv->out = (sv->out + 1) % (sv->max_requests); // circular buffer, increment
	(sv->count)--;
	pthread_mutex_unlock(&lock); 
	return connfd;
}

void write_buf(struct server *sv, int connfd){
	pthread_mutex_lock(&lock);
	while (sv->count == sv->max_requests){ //will fix spin waiting later
		pthread_cond_wait(&full, &lock);
		if (sv->exiting){
			pthread_mutex_unlock(&lock);
			pthread_exit(NULL);
		}
	}
	sv->buffer[sv->in] = connfd;
	if (sv->count == 0){ //if buffer empty signal 
		pthread_cond_broadcast(&empty);
	}
	sv->in = (sv->in + 1) % (sv->max_requests);
	(sv->count)++;
	pthread_mutex_unlock(&lock);
}


/* entry point functions */

void thread_main(struct server *sv){
	while (1){ 
		int connfd = read_buf(sv);
		do_server_request(sv, connfd);
	}
}


struct server *
server_init(int nr_threads, int max_requests, int max_cache_size)
{	
	pthread_mutex_init(&lock, NULL);
	pthread_mutex_init(&cache_l, NULL);
	pthread_cond_init(&empty, NULL);
	pthread_cond_init(&full, NULL);

	pthread_mutex_lock(&lock);
	struct server *sv;

	sv = Malloc(sizeof(struct server));
	// sv->nr_threads = nr_threads;
	// sv->max_requests = max_requests;
	// sv->max_cache_size = max_cache_size;
	// sv->exiting = 0;
	server_initalization(sv, nr_threads, max_requests, max_cache_size);
	
	if (nr_threads > 0 || max_requests > 0 || max_cache_size > 0) {
		if (max_requests > 0){
			/* Lab 4: create queue of max_request size when max_requests > 0 */
			sv->buffer = (int *)malloc((max_requests)*sizeof(int));
		}
		if (nr_threads > 0 ){
			/* Lab 4: create worker threads when nr_threads > 0 */
			sv->worker_pool = (pthread_t **)malloc(nr_threads*sizeof(pthread_t*));
			for (int i = 0; i < nr_threads; i++){
				sv->worker_pool[i] = (pthread_t *)malloc(sizeof(pthread_t)); //alloc space
				pthread_create(sv->worker_pool[i], NULL, (void *)&thread_main, sv);
			}
		}
	}
	/* Lab 5: init server cache and limit its size to max_cache_size */
	pthread_mutex_unlock(&lock);
	return sv;
}



void
server_request(struct server *sv, int connfd)
{
	if (sv->nr_threads == 0) { /* no worker threads */
		do_server_request(sv, connfd);
	} else {
		/*  Save the relevant info in a buffer and have one of the
		 *  worker threads do the work. */
		write_buf(sv, connfd);
	}
}

void
server_exit(struct server *sv)
{
	/* when using one or more worker threads, use sv->exiting to indicate to
	 * these threads that the server is exiting. make sure to call
	 * pthread_join in this function so that the main server thread waits
	 * for all the worker threads to exit before exiting. */
	sv->exiting = 1;
	pthread_cond_broadcast(&empty);
	pthread_cond_broadcast(&full);

	for (int i = 0; i < sv->nr_threads; i++){
		pthread_join(*(sv->worker_pool[i]), NULL);
		free(sv->worker_pool[i]);
	}
	if (sv->buffer > 0) free(sv->buffer);
	if (sv->nr_threads > 0) free(sv->worker_pool);
	/* make sure to free any allocated resources */
	free(sv);
}
