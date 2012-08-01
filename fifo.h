#ifndef __FIFO_H__
#define __FIFO_H__

struct fifo_t
{
    pthread_mutex_t lock;
    pthread_cond_t push;
    pthread_cond_t pop;

    size_t n;
    /* *head is the first element, *(tail-1) is the last element.
     * *(bufend-1) is the last element in the buf.  head == tail
     * means the buffer is empty.  When wrapped around, tail is
     * allowed only up to (head-1).  Highest value for tail is (bufend-1).  
     * One space is wasted.*/
    char *head, *tail, *bufend;
    char *buf;
};

/* create a fifo with buffer size n bytes */
struct fifo_t *fifo_init(size_t n);
int fifo_close(struct fifo_t *fifo);
/* returns number of bytes successfully pushed.  If not all n bytes
 * can be pushed, this function will block until enough space is made.
 * If n is greater than the fifo capacity (fifo->n - 1), this function
 * returns -1. */
ssize_t fifo_push(struct fifo_t *fifo, char *buf, size_t n);
/* return number of bytes successfully popped. n is the requested
 * size.  If there is nothing in the fifo, this function blocks until
 * at least one element is in the fifo. */
size_t fifo_pop(struct fifo_t *fifo, char *buf, size_t n);
/* number of elements (bytes) stored in the fifo. */
size_t fifo_nelements_in(struct fifo_t *fifo);

#endif /* __FIFO_H__ */
