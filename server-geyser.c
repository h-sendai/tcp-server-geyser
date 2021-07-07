#include <sys/time.h>

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "my_signal.h"
#include "my_socket.h"
#include "get_num.h"
#include "logUtil.h"
#include "set_timer.h"

int debug = 0;
int enable_quick_ack = 0;
int set_so_sndbuf_size = 0;
volatile sig_atomic_t has_alrm = 0;

int print_result(struct timeval start, struct timeval stop, int so_snd_buf, unsigned long long send_bytes)
{
    struct timeval diff;
    double elapse;

    timersub(&stop, &start, &diff);
    fprintfwt(stderr, "server-geyser: SO_SNDBUF: %d (final)\n", so_snd_buf);
    elapse = diff.tv_sec + 0.000001*diff.tv_usec;
    fprintfwt(stderr, "server-geyser: %.3f MB/s ( %lld bytes %ld.%06ld sec )\n",
        (double) send_bytes / elapse  / 1024.0 / 1024.0,
        send_bytes, diff.tv_sec, diff.tv_usec);

    return 0;
}

void sig_alrm()
{
    has_alrm = 1;
    if (debug > 1) {
        fprintf(stderr, "has_alrm\n");
    }
    return;
}

int child_proc(int connfd, int bufsize, int rate, int data_send_sec, int data_rest_sec)
{
    int n;
    unsigned char *buf;
    struct timeval start, stop;
    unsigned long long send_bytes = 0;

    buf = malloc(bufsize);
    if (buf == NULL) {
        err(1, "malloc sender buf in child_proc");
    }

    pid_t pid = getpid();
    fprintfwt(stderr, "server-geyser: pid: %d\n", pid);
    int so_snd_buf = get_so_sndbuf(connfd);
    fprintfwt(stderr, "server-geyser: SO_SNDBUF: %d (init)\n", so_snd_buf);

    my_signal(SIGALRM, sig_alrm);
    gettimeofday(&start, NULL);
    set_timer(data_send_sec, 0, data_send_sec + data_rest_sec, 0);
    for ( ; ; ) {
        if (has_alrm) {
            has_alrm = 0;
            if (debug) {
                fprintfwt(stderr, "entering sleep\n");
            }
            sleep(data_rest_sec);
            if (debug) {
                fprintfwt(stderr, "exiting sleep\n");
            }
            if (rate > 0) {
                /*
                 * XXX 
                 * reset start time and send_bytes to send
                 * in rate after resume.
                 * if rate is specified and data sending suspend,
                 * the final result in bytes and transfer rate
                 * will be invalid values
                 */
                gettimeofday(&start, NULL);
                send_bytes = 0;
            }
        }

        n = write(connfd, buf, bufsize);
        if (n < 0) {
            gettimeofday(&stop, NULL);
            int so_snd_buf = get_so_sndbuf(connfd);
            print_result(start, stop, so_snd_buf, send_bytes);
            if (errno == ECONNRESET) {
                fprintfwt(stderr, "server-geyser: connection reset by client\n");
                break;
            }
            else if (errno == EPIPE) {
                fprintfwt(stderr, "server-geyser: connection reset by client\n");
                break;
            }
            else {
                err(1, "write");
            }
        }
        else if (n == 0) {
            warnx("write returns 0");
            exit(0);
        }
        else {
            send_bytes += n;
        }
        if (rate > 0) {
            struct timeval now, elapsed;
            // calculate sleep sec
            gettimeofday(&now, NULL);
            timersub(&now, &start, &elapsed);
            double delta_t_sec = (double) send_bytes / (double) rate - (double) elapsed.tv_sec - (double) 0.000001*elapsed.tv_usec ;
            if (delta_t_sec > 0.0) {
                int sleep_usec = (int) (delta_t_sec * 1000000.0);
                usleep(sleep_usec);
            }
        }
    }
    return 0;
}

void sig_chld(int signo)
{
    pid_t pid;
    int   stat;

    while ( (pid = waitpid(-1, &stat, WNOHANG)) > 0) {
        ;
    }
    return;
}

int usage(void)
{
    char *msg =
"Usage: server [-b bufsize (1460)] [-r rate]\n"
"-b bufsize:    one send size (may add k for kilo, m for mega)\n"
"-p port:       port number (1234)\n"
"-r rate:       data send rate (bytes/sec).  k for kilo, m for mega\n"
"-D data_send_sec: data send seconds.  default 10 seconds\n"
"-R data_rest_sec: data rest seconds.  default 10 seconds\n";

    fprintf(stderr, msg);

    return 0;
}

int main(int argc, char *argv[])
{
    int port = 1234;
    pid_t pid;
    struct sockaddr_in remote;
    socklen_t addr_len = sizeof(struct sockaddr_in);
    int listenfd;
    int c;
    int bufsize = 1460;
    int rate = 0;
    int data_send_sec = 10;
    int data_rest_sec = 10;

    while ( (c = getopt(argc, argv, "b:dhp:r:D:R:")) != -1) {
        switch (c) {
            case 'b':
                bufsize = get_num(optarg);
                break;
            case 'd':
                debug += 1;
                break;
            case 'h':
                usage();
                exit(0);
            case 'p':
                port = strtol(optarg, NULL, 0);
                break;
            case 'r':
                rate = get_num(optarg);
                break;
            case 'D':
                data_send_sec = strtol(optarg, NULL, 0);
                break;
            case 'R':
                data_rest_sec = strtol(optarg, NULL, 0);
                break;
            default:
                break;
        }
    }
    argc -= optind;
    argv += optind;

    my_signal(SIGCHLD, sig_chld);
    my_signal(SIGPIPE, SIG_IGN);

    listenfd = tcp_listen(port);
    if (listenfd < 0) {
        errx(1, "tcp_listen");
    }

    for ( ; ; ) {
        int connfd = accept(listenfd, (struct sockaddr *)&remote, &addr_len);
        if (connfd < 0) {
            err(1, "accept");
        }
        
        pid = fork();
        if (pid == 0) { //child
            if (close(listenfd) < 0) {
                err(1, "close listenfd");
            }
            if (child_proc(connfd, bufsize, rate, data_send_sec, data_rest_sec) < 0) {
                errx(1, "child_proc");
            }
            exit(0);
        }
        else { // parent
            if (close(connfd) < 0) {
                err(1, "close for accept socket of parent");
            }
        }
    }
        
    return 0;
}
