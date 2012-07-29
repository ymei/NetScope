/*
 * Copyright (c) 2012
 *
 *     Yuan Mei
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * dpo5054 host port ...
 */

/* waitpid on linux */
#include <sys/types.h>
#include <sys/wait.h>

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <netdb.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>

#ifdef __linux /* on linux */
#include <pty.h>
#include <utmp.h>
#else /* (__APPLE__ & __MACH__) */
#include <util.h> /* this is for mac or bsd */
#endif

#include <paths.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <time.h>

#include "waveform.h"
#include "hdf5io.h"

#ifdef DEBUG
  #define debug_printf(fmt, ...) do { fprintf(stderr, fmt, ##__VA_ARGS__); fflush(stderr); \
                                    } while (0)
#else
  #define debug_printf(...) ((void)0)
#endif
#define error_printf(fmt, ...) do { fprintf(stderr, fmt, ##__VA_ARGS__); fflush(stderr); \
                                  } while(0)

static unsigned int chMask;
static size_t nCh;
static size_t nEvents;

static struct hdf5io_waveform_file *waveformFile;
static struct hdf5io_waveform_event waveformEvent;
static struct waveform_attribute waveformAttr;

#define MAXSLEEP 2
static int connect_retry(int sockfd, const struct sockaddr *addr, socklen_t alen)
{
    int nsec;
    /* Try to connect with exponential backoff. */
    for (nsec = 1; nsec <= MAXSLEEP; nsec <<= 1) {
        if (connect(sockfd, addr, alen) == 0) {
            /* Connection accepted. */
            return(0);
        }
        /*Delay before trying again. */
        if (nsec <= MAXSLEEP/2)
            sleep(nsec);
    }
    return(-1);
}

static int get_socket(char *host, char *port)
{
    int status;
    struct addrinfo addrHint, *addrList, *ap;
    int sockfd, sockopt;

    memset(&addrHint, 0, sizeof(struct addrinfo));
    addrHint.ai_flags = AI_CANONNAME|AI_NUMERICSERV;
    addrHint.ai_family = AF_INET; /* we deal with IPv4 only, for now */
    addrHint.ai_socktype = SOCK_STREAM;
    addrHint.ai_protocol = 0;
    addrHint.ai_addrlen = 0;
    addrHint.ai_canonname = NULL;
    addrHint.ai_addr = NULL;
    addrHint.ai_next = NULL;

    status = getaddrinfo(host, port, &addrHint, &addrList);
    if(status < 0) {
        error_printf("getaddrinfo: %s\n", gai_strerror(status));
        return status;
    }

    for(ap=addrList; ap!=NULL; ap=ap->ai_next) {
        sockfd = socket(ap->ai_family, ap->ai_socktype, ap->ai_protocol);
        if(sockfd < 0) continue;
        sockopt = 1;
        if(setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (char*)&sockopt, sizeof(sockopt)) == -1) {
            /* setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, (char*)&sockopt, sizeof(sockopt)) */
            close(sockopt);
            warn("setsockopt");
            continue;
        }
        if(connect_retry(sockfd, ap->ai_addr, ap->ai_addrlen) < 0) {
            close(sockfd);
            warn("connect");
            continue;
        } else {
            break; /* success */
        }
    }
    if(ap == NULL) { /* No address succeeded */
        error_printf("Could not connect, tried %s:%s\n", host, port);
        return -1;
    }
    freeaddrinfo(addrList);
    return sockfd;
}

static int query_response_with_timeout(int sockfd, char *queryStr, char *respStr,
                                       struct timeval *tv)
{
    int maxfd;
    fd_set rfd;
    int nsel;
    ssize_t nr, nw;
    size_t ret;

    nw = send(sockfd, queryStr, strnlen(queryStr, BUFSIZ), 0);
    if(nw<0) {
        warn("send");
        return (int)nw;
    }

    ret = 0;
    for(;;) {
        FD_ZERO(&rfd);
        FD_SET(sockfd, &rfd);
        maxfd = sockfd;
        nsel = select(maxfd+1, &rfd, NULL, NULL, tv);
        if(nsel < 0 && errno != EINTR) { /* other errors */
            return nsel;
        }
        if(nsel == 0) { /* timed out */
            break;
        }
        if(nsel>0 && FD_ISSET(sockfd, &rfd)) {
            nr = read(sockfd, respStr+ret, BUFSIZ-ret);
            // debug_printf("nr = %zd\n", nr);
            if(nr>0) {
                ret += nr;
            } else {
                break;
            }
        }
    }
    return (int)ret;
}

static int query_response(int sockfd, char *queryStr, char *respStr)
{
    struct timeval tv = {
        .tv_sec = 0,
        .tv_usec = 500000,
    };
    return query_response_with_timeout(sockfd, queryStr, respStr, &tv);
}

static int prepare_scope(int sockfd, struct waveform_attribute *wavAttr)
/* fills wavAttr as well */
{
    int ret, ich, isFastFrame;
    char buf[BUFSIZ], buf1[BUFSIZ], buf2[BUFSIZ];
    
    wavAttr->chMask = chMask;

    strlcpy(buf, "*IDN?\n", sizeof(buf));
    ret = query_response(sockfd, buf, buf);
    buf[ret] = '\0';
    printf("%s", buf);

    strlcpy(buf, "DATa:ENCdg fastest;:", sizeof(buf));
    strlcpy(buf1, "data:source ", sizeof(buf1));
    for(ich=0; ich<SCOPE_NCH; ich++) {
        if((chMask >> ich) & 0x01) {
            snprintf(buf2, sizeof(buf2), "select:ch%d 1;:", ich+1);
            strncat(buf, buf2, sizeof(buf)-strnlen(buf, sizeof(buf))-1);

            snprintf(buf2, sizeof(buf2), "ch%d,", ich+1);
            strncat(buf1, buf2, sizeof(buf1)-strnlen(buf1, sizeof(buf1))-1);
        }
    }
    buf[strnlen(buf, sizeof(buf))-1] = '\0';
    buf[strnlen(buf, sizeof(buf))-1] = '\n';
    buf1[strnlen(buf1, sizeof(buf1))-1] = '\n';

    /* turn on selected channels */
    ret = query_response(sockfd, buf, buf);

    strlcpy(buf, "HORizontal:ACQLENGTH?;:WFMOutpre:XINcr?;:WFMOutpre:XZEro?\n", sizeof(buf));
    ret = query_response(sockfd, buf, buf);
    sscanf(buf, "%zd;%lf;%lf", &(wavAttr->nPt), &(wavAttr->dt), &(wavAttr->t0));

    strlcpy(buf, "HORizontal:FASTframe:STATE?;:HORizontal:FASTframe:COUNt?\n", sizeof(buf));
    ret = query_response(sockfd, buf, buf);
    sscanf(buf, "%d;%zd", &isFastFrame, &(wavAttr->nFrames));
    if(isFastFrame) {
        printf("FastFrame mode, %zd frames per event.\n", wavAttr->nFrames);
        wavAttr->nPt *= wavAttr->nFrames;
    } else {
        wavAttr->nFrames = 0;
    }

    for(ich=0; ich<SCOPE_NCH; ich++) {
        snprintf(buf, sizeof(buf), "data:source ch%d;%s\n", ich+1,
                 ":data:encdg FAStest;:WFMOutpre:BYT_Nr 1;"
                 ":WFMOutpre:YMUlt?;:WFMOutpre:YOFf?;:WFMOutpre:YZEro?");
        ret = query_response(sockfd, buf, buf);
        sscanf(buf, "%lf;%lf;%lf", &(wavAttr->ymult[ich]), &(wavAttr->yoff[ich]),
               &(wavAttr->yzero[ich]));
    }

    printf("waveform_attribute:\n"
           "     chMask  = 0x%02x\n"
           "     nPt     = %zd\n"
           "     nFrames = %zd\n"
           "     dt      = %g\n"
           "     t0      = %g\n"
           "     ymult   = %g %g %g %g\n"
           "     yoff    = %g %g %g %g\n"
           "     yzero   = %g %g %g %g\n",
           wavAttr->chMask, wavAttr->nPt, wavAttr->nFrames, wavAttr->dt, wavAttr->t0,
           wavAttr->ymult[0], wavAttr->ymult[1], wavAttr->ymult[2], wavAttr->ymult[3],
           wavAttr->yoff[0], wavAttr->yoff[1], wavAttr->yoff[2], wavAttr->yoff[3],
           wavAttr->yzero[0], wavAttr->yzero[1], wavAttr->yzero[2], wavAttr->yzero[3]);

    /* data:source to selected channels */
    ret = query_response(sockfd, buf1, buf);
    /* set waveform range */
    snprintf(buf, sizeof(buf), "data:start 1;:data:stop %zd\n", wavAttr->nPt);
    ret = query_response(sockfd, buf, buf);

    return ret;
}

static void atexit_flush_files(void)
{
    hdf5io_flush_file(waveformFile);
    hdf5io_close_file(waveformFile);
}

static void signal_kill_handler(int sig)
{
    printf("\nstop time  = %zd\n", time(NULL));
    fflush(stdout);
    
    error_printf("Killed, cleaning up...\n");
    atexit(atexit_flush_files);
    exit(EXIT_SUCCESS);
}

static void *receive_and_save(void *arg)
{
    struct timeval tv = {
        .tv_sec = 10,
        .tv_usec = 0,
    };
    int sockfd, maxfd, nsel;
    fd_set rfd;
    int fStartEvent, fEndEvent, fStartCh, fGetNDig, fGetRetChLen; /* flags of states */
    size_t nDig, retChLen, iCh, iEvent, iRetChLen, i, j, wavBufN;
    char ibuf[BUFSIZ], retChLenBuf[BUFSIZ];
    ssize_t nr, nw;
    char *wavBuf;

/*
    FILE *fp;
    if((fp=fopen("log.txt", "w"))==NULL) {
        perror("log.txt");
        return (void*)NULL;
    }
*/
    sockfd = *((int*)arg);

    strlcpy(ibuf, "curve?\n", sizeof(ibuf));
    nw = write(sockfd, ibuf, strnlen(ibuf, sizeof(ibuf)));

    wavBufN = waveformAttr.nPt * nCh;
    wavBuf = (char*)malloc(wavBufN * sizeof(char));

    iCh = 0; iEvent = 0; j = 0;
    fStartEvent = 1; fEndEvent = 0; fStartCh = 0; fGetNDig = 0; fGetRetChLen = 0;
    for(;;) {
        FD_ZERO(&rfd);
        FD_SET(sockfd, &rfd);
        maxfd = sockfd;
        nsel = select(maxfd+1, &rfd, NULL, NULL, &tv);
        if(nsel < 0 && errno != EINTR) { /* other errors */
            warn("select");
            break;
        }
        if(nsel == 0) {
            warn("timed out");
        }
        if(nsel>0 && FD_ISSET(sockfd, &rfd)) {
            nr = read(sockfd, ibuf, sizeof(ibuf));
            // write(fileno(fp), ibuf, nr);
            for(i=0; i<nr; i++) {
                if(fStartEvent) {
                    printf("iEvent = %zd, ", iEvent);
                    iCh = 0;
                    j = 0;
                    fStartEvent = 0;
                    fStartCh = 1;
                    i--; continue;
                } else if(fEndEvent) {
                    if(ibuf[i] == ';') { /* ';' only appears in curvestream? mode */
                        continue;
                    } else if(ibuf[i] == '\n') {
                        // debug_printf("iEvent = %zd\n", iEvent);
                        printf("\n");
                        fflush(stdout);
                        fEndEvent = 0;
                        fStartEvent = 1;

                        if(iEvent < nEvents-1) {
                            strlcpy(retChLenBuf, "curve?\n", sizeof(retChLenBuf));
                            write(sockfd, retChLenBuf, strnlen(retChLenBuf, sizeof(retChLenBuf)));
                        } /* request next event before writing the
                           * current event to file may boost data rate
                           * a bit */
                        waveformEvent.wavBuf = wavBuf;
                        waveformEvent.eventId = iEvent;
                        hdf5io_write_event(waveformFile, &waveformEvent);
                        iEvent++;
                        if(iEvent >= nEvents) {
                            printf("\n");
                            goto end;
                        }
                    }
                } else {
                    if(fStartCh) {
                        if(ibuf[i] == '#') {
                            fGetNDig = 1;
                            continue;
                        } else if(fGetNDig) {
                            retChLenBuf[0] = ibuf[i];
                            retChLenBuf[1] = '\0';
                            nDig = atol(retChLenBuf);
                            printf("nDig = %zd, ", nDig);
                            iRetChLen = 0;
                            fGetNDig = 0;
                            continue;
                        } else {
                            retChLenBuf[iRetChLen] = ibuf[i];
                            iRetChLen++;
                            if(iRetChLen >= nDig) {
                                retChLenBuf[iRetChLen] = '\0';
                                retChLen = atol(retChLenBuf);
                                printf("iRetChLen = %zd, retChLen = %zd, ",
                                       iRetChLen, retChLen);
                                fStartCh = 0;
                                continue;
                            }
                        }
                    } else {
                        wavBuf[j] = ibuf[i];
                        j++;
                        if((j % waveformAttr.nPt) == 0 && (j!=0)) {
                            printf("iCh = %zd, ", iCh);
                            iCh++;
                            fStartCh = 1;
                            if(iCh >= nCh) {
                                fEndEvent = 1;
                            }
                        }
                    }
                }
            }
        }
    }
end:
/*
    while((nr = read(sockfd, ibuf, sizeof(ibuf)-1))>0) {
        ibuf[nr] = '\0';
        if(nr < sizeof(ibuf)-1) {
            printf("\nThread: %p: %d:\n", pthread_self(), ++i);
        }
        // printf("%s", ibuf);
        write(fileno(fp), ibuf, nr);
        fflush(fp);
        printf("ret: %zd ", nr);
        fflush(stdout);
    }
    if(nr < 0) {
        warn("Thread: %p: read", pthread_self());
        fflush(stderr);
    }
*/
    free(wavBuf);
//    fclose(fp);
    return (void*)NULL;
}

int main(int argc, char **argv)
{
    char *p, *outFileName, *scopeAddress, *scopePort;
    unsigned int v, c;
    int sockfd;
    size_t nWfmPerChunk = 100;

    if(argc<6) {
        error_printf("%s scopeAdddress scopePort outFileName chMask(0x..) nEvents nWfmPerChunk\n",
                     argv[0]);
        return EXIT_FAILURE;
    }
    scopeAddress = argv[1];
    scopePort = argv[2];
    outFileName = argv[3];
    nEvents = atol(argv[5]);

    errno = 0;
    chMask = strtol(argv[4], &p, 16);
    v = chMask;
    for(c=0; v; c++) v &= v - 1; /* Brian Kernighan's way of counting bits */
    nCh = c;
    if(errno != 0 || *p != 0 || p == argv[4] || chMask <= 0 || nCh>SCOPE_NCH) {
        error_printf("Invalid chMask input: %s\n", argv[4]);
        return EXIT_FAILURE;
    }
    if(argc>=7)
        nWfmPerChunk = atol(argv[6]);

    debug_printf("outFileName: %s, chMask: 0x%02x, nCh: %zd, nEvents: %zd, nWfmPerChunk: %zd\n",
                 outFileName, chMask, nCh, nEvents, nWfmPerChunk);

    sockfd = get_socket(scopeAddress, scopePort);
    if(sockfd < 0) {
        error_printf("Failed to establish a socket.\n");
        return EXIT_FAILURE;
    }

    prepare_scope(sockfd, &waveformAttr);
    waveformFile = hdf5io_open_file(outFileName, nWfmPerChunk, nCh);
    hdf5io_write_waveform_attribute_in_file_header(waveformFile, &waveformAttr);

    signal(SIGKILL, signal_kill_handler);
    signal(SIGINT, signal_kill_handler);

    printf("start time = %zd\n", time(NULL));

    receive_and_save(&sockfd);
/*    
    pthread_create(&rTid, NULL, receive_and_save, &sockfd);

    do {
        fgets(ibuf, sizeof(ibuf), stdin);
        nwreq = strnlen(ibuf, sizeof(ibuf));
        printf("size: %zd, %s", nwreq, ibuf);
        fflush(stdout);
        nw = write(sockfd, ibuf, nwreq);
    } while(nw >= 0);
*/
/*
    do {
        fgets(ibuf, sizeof(ibuf), stdin);
        nw = query_response(sockfd, ibuf, ibuf);
        write(STDIN_FILENO, ibuf, nw);
    } while (nw>=0);
*/
    printf("\nstop time  = %zd\n", time(NULL));

    close(sockfd);
    atexit_flush_files();
    return EXIT_SUCCESS;
}
