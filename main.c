#include <stdio.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>

#include "yhepoll.h"
#include "yhsocket.h"

// Author: Hong Yuan
// Data  : 2014年 02月 20日 星期四 18:00:16 CST
// COMM  : Nothing

void client_in (fd_event *ptr);
void client_err(fd_event *ptr);
void client_hup(fd_event *ptr);
void client_new(epoll_manager*em, int fd);

void showfd()
{
    system("ls /dev/fd");
}

void tcp_server_in(fd_event* fe)
{
    int fd;
    while (1) {
        fd = accept(fe->fd, 0, 0);
        if (fd < 0) { 
            if (errno != EAGAIN) perror("accept error"); 
            return; 
        }
        printf("server [%05d] accept ret: %d\n", fe->fd, fd);
        client_new(fe->em, fd);
    }
}

void udp_server_in(fd_event * fe)
{
    char buf[1024];
    int ret = recv( fe->fd, buf, sizeof(buf), 0);
    printf("udp server [%05d] recv bytes %d\n", fe->fd, ret);
}

void client_new(epoll_manager *em, int fd)
{
	fd_event *pfh = fd_event_new();
	fd_event_init(pfh, em, fd);
	fd_event_set(pfh, EPOLLIN,  client_in);
	fd_event_set(pfh, EPOLLET, 0);
	//fd_event_set(pfh, EPOLLERR, client_err); // 不会被触发
	//fd_event_set(pfh, EPOLLHUP, client_hup); // 不会被触发
    //setsock_rcvtimeo(fd, 5, 0); //非阻塞模式中接收超时事件底层不捕获,
                                    //仅阻塞模式可获知
    setfd_nonblock(fd);
	//em_fd_event_add(pfh);
	notify_em_fd_event_add(pfh); // 这两种方式都可以
}
void client_out(fd_event *fe)
{
	notify_em_fd_event_release(fe,1);
}
void client_in(fd_event *fe)
{
    int fd = fe->fd;
	char buff[1024] = {0};
	int n;
eintr:
    while ((n = read(fd, buff, sizeof(buff))) > 0) {
        // recv datas ....
        printf("client [%05d] read len %d\n", fd, n); 
    }
    if (n < 0) {
        if (errno == EAGAIN) return; 
        if (errno == EINTR)  goto eintr; 
        printf("client [%05d] read ret %d to error:%s\n", fd, n, strerror(errno));
        client_out(fe);
    }
    if (n == 0) {
        printf("client [%05d] read ret %d to close\n", fd, n);
        client_out(fe);
    }
}
void client_err(fd_event *fe)
{
    printf("client %d client_err\n", fe->fd);
}
void client_hup(fd_event *fe)
{
    printf("client %d client_hup\n", fe->fd);
}

void _stdin_(fd_event* fe);
void remove_stdin(fd_event_handle fe)
{
    notify_em_fd_event_release(fe,1);
}

void _stdin_(fd_event* fe)
{
    printf("_stdin_\n");
    int fd = fe->fd;
    char line[1024];
    int n;
    
eintr:
    while ((n = read(fd, line, sizeof(line))) > 0) {
        write(1, line, n);
        if (strncmp(line, "quit", 4) == 0) exit(0);
        else if (strncmp(line, "exit", 4) == 0) exit(0);
        else if (strncmp(line, "showfd", 6) == 0) showfd();
        else if (strncmp(line, "closeallfd", 10) == 0) close_all_fd();
    }
    if ( n == 0 ) {
        printf("stdin Ctrl+D to close.\n"); remove_stdin(fe); return;
    } else {
        if      (errno == EAGAIN) return;
        else if (errno == EINTR ) goto eintr;
        printf("stdin error:%s.\n", strerror(errno));
    }
}

int count;

void _virtual_out_(fd_event *fe)
{
    printf("_virtual_out_ %p %d\n", _virtual_out_, ++count);
}

void before(const epoll_manager * const em)
{
    //printf("before\n");
}
void event(const epoll_manager * const em)
{
    //printf("epoll_wait ret %d\n", em->nfds);
    if (em->nfds < 0) { 
        if (errno == EAGAIN || errno == EINTR) return;
        printf("epoll_wait error %d: %s\n", errno, strerror(errno)); 
        exit(1); 
    }
}
void after(const epoll_manager * const em)
{
    //printf("after\n");
}

int
main(int argc, char **argv) 
{
    if (argc != 2) {
        printf("usage: %s <service-port>\n", argv[0]);
        exit(1);
    }

    int port = atoi(argv[1]);
    signal(SIGPIPE, SIG_IGN);

    //epoll_manager *em = em_open(999, 1000, before, event, after);
    epoll_manager *em = Em_open(999, -1, 0, 0, 0);
    Em_run(em);

	// demo 1: network server tcp
	int tcpsfd = create_tcp_v4_server((char*)"0.0.0.0", port, 1000);
    if (tcpsfd < 0) {
        printf("create_tcp_v4_server error: %s\n", strerror(errno));
        exit(1);
    }
	printf("tcp server fd %d, port %d\n", tcpsfd, port);
	fd_event_handle festcp = fd_event_handle_new();
	fd_event_init(festcp, em, tcpsfd);
	fd_event_set(festcp, EPOLLIN, tcp_server_in);
	fd_event_set(festcp, EPOLLET, 0);
    setfd_nonblock(tcpsfd);
	notify_em_fd_event_add(festcp);

    //*
	// demo 2: network server udp
	int udpsfd = create_udp_v4_server((char*)"0.0.0.0", port, 1000);
    if (udpsfd < 0) {
        printf("create_udp_v4_server error: %s\n", strerror(errno));
        exit(1);
    }
	printf("udp server fd %d, port %d\n", udpsfd, port);
	fd_event* fesudp = fd_event_new();
	fd_event_init(fesudp, em, udpsfd);
	fd_event_set(fesudp, EPOLLIN, udp_server_in);
	notify_em_fd_event_add(fesudp);

	// demo 3: standard input
    fd_event in = FD_EVENT_INITIALIZER, *pin = &in;
    fd_event_init(pin, em, 0);
    setfd_nonblock(0);
    fd_event_set(pin, EPOLLET, 0);
    fd_event_set(pin, EPOLLIN, _stdin_);
    //fd_event_unset(pin, EPOLLIN); // 移除可读检测标志
    notify_em_fd_event_add(pin);
    // */

    /*
    // demo 4: timer
    //  epoll 好像不支持磁盘普通文件的写事件侦测 
    //    EPOLL_CTL_ADD普通文件失败了
    //    所以此处采用了域套接字
    int _virtual_fd_[2];
	if ( socketpair(PF_UNIX, SOCK_STREAM, 0, _virtual_fd_) < 0) {
        printf("socketpair error:%s\n", strerror(errno));
        exit(1);
    }
    close(_virtual_fd_[0]);  // 读端不使用
    fd_event wzero = FD_EVENT_INITIALIZER, *fewzero = &wzero;
    fd_event_init(fewzero, em, _virtual_fd_[1]);
    setfd_nonblock(_virtual_fd_[1]);
    fd_event_set(fewzero, EPOLLET, 0);
    fd_event_set(fewzero, EPOLLOUT, _virtual_out_);
    notify_em_fd_event_add(fewzero);

    //fd_event_uninit(fewzero); // 故意引发下面错误
    // 定时触发 EPOLLOUT 事件
    int count = 0; 
    while (++count < 5) { 
        usleep(1000*1000);
        // 如果消息未处理完就fd_event_uninit时将关闭fd
        notify_em_fd_event_mod(fewzero); 
        //em_fd_event_mod(fewzero); 
    }
    usleep(10000);
    notify_em_fd_event_release(fewzero, 1);

    sleep(5);
    printf("set em stop\n");
    em_stop(em);

    em_set_timeout(em, 10000);

    sleep(5);
    printf("set em run\n");
    em_start(em);
    sleep(5);
       
    printf("em_close\n");
    em_close(em);  // 实际应用应该不会用到
    printf("em_close done.\n");
    close(_virtual_fd_[1]);
    fd_event_handle_release(fesudp);
    fd_event_handle_release(festcp);

// */
	while (1) pause();
    exit(0);

	return 0;
}

