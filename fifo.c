#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include "fifo.h"

#ifndef min
#define min(a, b) ((a) <= (b) ? (a) : (b))
#endif

#ifndef max
#define max(a, b) ((a) >= (b) ? (a) : (b))
#endif

/* The number of spins to do before performing an expensive
 kernel-mode context switch. This is a nice easy value to tweak for
 your application's needs.  Set it to 0 if you want the implementation
 to decide, a low number if you are copying many objects into pipes at
 once (or a few large objects), and a high number if you are coping
 small or few objects into pipes at once.*/
#define MUTEX_SPINS 8192
static void mutex_lock(pthread_mutex_t* m)
{
    uint64_t i;
    
    for(i = 0; i < MUTEX_SPINS; i++)
        if(pthread_mutex_trylock(m) == 0)
            return;

    pthread_mutex_lock(m);
}
#define mutex_unlock pthread_mutex_unlock

/* runs some code while automatically locking and unlocking the
   pipe. If `break' is used, the pipe will be unlocked before control
   returns from the macro. */
#define WHILE_LOCKED(body) do {                 \
        mutex_lock(&(fifo->lock));              \
        do { body; } while(0);                  \
        mutex_unlock(&(fifo->lock));            \
    } while(0)

#define cond_signal    pthread_cond_signal
#define cond_broadcast pthread_cond_broadcast
#define cond_wait      pthread_cond_wait

struct fifo_t *fifo_init(uint64_t n)
{
    struct fifo_t *fifo;
    fifo = (struct fifo_t*)malloc(sizeof(struct fifo_t));
    fifo->buf = (char*)malloc(sizeof(char)*n);
    fifo->n = n;
    fifo->bufend = fifo->buf + n;
    fifo->head = fifo->buf;
    fifo->tail = fifo->buf;

    pthread_mutex_init(&(fifo->lock), NULL);
    pthread_cond_init(&(fifo->push), NULL);
    pthread_cond_init(&(fifo->pop), NULL);
    
    return fifo;
}

int fifo_close(struct fifo_t *fifo)
{
    if(fifo) {
        if(fifo->buf) free(fifo->buf);

        pthread_mutex_destroy(&(fifo->lock));
        pthread_cond_destroy(&(fifo->push));
        pthread_cond_destroy(&(fifo->pop));
        
        free(fifo);
        return 0;
    }
    return -1;
}

int64_t fifo_push(struct fifo_t *fifo, char *buf, uint64_t n)
{
    uint64_t ret = 0, rem;

    if(n > (fifo->n - 1))
        return -1;

    WHILE_LOCKED(
        while(1) {
            if(fifo->tail < fifo->head) /* wrapped around */
                rem = fifo->head - fifo->tail - 1;
            else
                rem = (fifo->head - fifo->buf - 1) + (fifo->bufend - fifo->tail);
            if(rem < n)
                cond_wait(&(fifo->pop), &(fifo->lock));
            else
                break;
        }

        if(fifo->tail < fifo->head) { /* wrapped around */
            if(fifo->head - fifo->tail - 1 >= n) {
                memcpy(fifo->tail, buf, n);
                fifo->tail += n;
                ret = n;
            } else { /* not enough space */
                ret = 0;
            }
        } else {
            rem = fifo->bufend - fifo->tail;
            if(rem > n) {
                memcpy(fifo->tail, buf, n);
                fifo->tail += n;
                ret = n;
            } else if(rem + (fifo->head - fifo->buf - 1) >= n) { /* need to wrap around */
                memcpy(fifo->tail, buf, rem);
                memcpy(fifo->buf, buf+rem, n-rem);
                fifo->tail = fifo->buf + n-rem;
                ret = n;
            } else { /* not enough space */
                ret = 0;
            }
        });
    if(ret>0)
        cond_signal(&(fifo->push));
    return ret;
}

uint64_t fifo_pop(struct fifo_t *fifo, char *buf, uint64_t n)
{
    uint64_t ret = 0, toCopy, rem;
    WHILE_LOCKED(
        while(1) {
            if(fifo->tail < fifo->head) /* wrapped around */
                rem = (fifo->bufend - fifo->head) + (fifo->tail - fifo->buf);
            else
                rem = fifo->tail - fifo->head;
            if(rem < 1) /* wait while fifo is empty */
                cond_wait(&(fifo->push), &(fifo->lock));
            else
                break;
        }

        if(fifo->tail < fifo->head) { /* wrapped around */
            toCopy = min(fifo->bufend - fifo->head, n);
            memcpy(buf, fifo->head, toCopy);
            ret += toCopy;
            fifo->head += toCopy;
            if(fifo->head >= fifo->bufend)
                fifo->head = fifo->buf;
            if(ret < n) {
                toCopy = min(n - ret, fifo->tail - fifo->head);
                memcpy(buf + ret, fifo->head, toCopy);
                ret += toCopy;
                fifo->head += toCopy;
            }
        } else {
            toCopy = min(fifo->tail - fifo->head, n);
            memcpy(buf, fifo->head, toCopy);
            ret += toCopy;
            fifo->head += toCopy;
        });
    if(ret>0)
        cond_signal(&(fifo->pop));
    return ret;
}

uint64_t fifo_nelements_in(struct fifo_t *fifo)
{
    WHILE_LOCKED(
        if(fifo->tail < fifo->head) { /* wrapped around */
            return (fifo->tail - fifo->buf) + (fifo->bufend - fifo->head);
        } else {
            return fifo->tail - fifo->head;
        });
}

#ifdef FIFO_DEBUG_ENABLEMAIN
#include <unistd.h>
#define BUFSIZE 1024
static struct fifo_t *fifo;
static void *pop_and_print(void *arg)
{
    char ibuf[BUFSIZE];
    uint64_t i, nr;
    
    for(i=0;;i++) {
        nr = fifo_pop(fifo, ibuf, sizeof(ibuf));
        if(nr > 0) {
            write(STDOUT_FILENO, ibuf, nr);
            printf("Thread %p, %llu, %llu popped.\n", pthread_self(), i, nr); fflush(stdout);
        }
    }
    return (void*)NULL;
}

int main(int argc, char **argv)
{
    char ibuf[BUFSIZE];
    pthread_t rTid;
    int64_t nw;

    fifo = fifo_init(8);

    pthread_create(&rTid, NULL, pop_and_print, NULL);

    do {
        fgets(ibuf, sizeof(ibuf), stdin);
        nw = strnlen(ibuf, BUFSIZE);
//        write(STDOUT_FILENO, ibuf, nw);
        nw = fifo_push(fifo, ibuf, strnlen(ibuf, BUFSIZE));
        printf("Thread %p, %llu pushed\n", pthread_self(), nw); fflush(stdout);
    } while (nw>=0);
    
    fifo_close(fifo);
    return EXIT_SUCCESS;
}
#endif
