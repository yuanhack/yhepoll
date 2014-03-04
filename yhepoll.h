#ifndef __YH_YHEPOLL_H__
#define __YH_YHEPOLL_H__

#include <sys/epoll.h>
#include <string.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>

typedef struct fd_event fd_event;
typedef struct epoll_manager epoll_manager;

//typedef void  (*fd_event_callback)(void * ptr);
typedef void  (*fd_event_callback)(fd_event * fe);
typedef void  (*em_callback)(const epoll_manager * const em);


///////////////////////////////////////////////////////////////////////////////
#ifndef offsetof
#define offsetof(type, member) \
    (size_t)&(((type *)0)->member)
#endif

#ifndef container_of
#define container_of(ptr, type, member)  \
    ({\
     const typeof(((type *)0)->member) * __mptr = (ptr);\
     (type *)((char *)__mptr - offsetof(type, member)); \
     })
#endif

#ifndef struct_entry 
#define struct_entry(ptr, type,  member) container_of(ptr, type, member)
#endif
///////////////////////////////////////////////////////////////////////////////

#define FD_EVENT_MAGIC_HEAP   0x1234abcd             /* heap alloc flag */
#define FD_EVENT_INITIALIZER  {-1,{0},0,0,0,0,0,0,0} /* Stack initialization */
#define FD_EVENT_INIT         FD_EVENT_INITIALIZER
// 栈上获取的使用此宏初始化, 然后在调用 fd_event_init 初始化
// 堆上获取的直接调用 fd_event_init 初始化

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * 
 *  关于fd 在epoll_wait 中返回后的状态
 *	EPOLLIN ：文件描述符可以读（包括对端SOCKET正常关闭）；
 * 	EPOLLOUT：文件描述符可以写；
 * 	EPOLLPRI：文件描述符有紧急的数据可读（有带外数据）；
 * 	EPOLLERR：文件描述符发生错误；
 * 	EPOLLHUP：文件描述符被挂断；
 * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * * fd event object * * * * * * * * * * * * * * * * * */
// event.events = EPOLLxxx | ...; event.data.ptr = fd_event address;
typedef struct fd_event {
	int fd;                     // file descriptor
	struct epoll_event event;   
    epoll_manager       *em;    // epoll manager

    // heap  flag is FD_EVENT_MAGIC_HEAP
    // stack flag is 0 
    unsigned int heap;

	// event callback:
	fd_event_callback  in;
	fd_event_callback  out;
	fd_event_callback  pri;
	fd_event_callback  err;
	fd_event_callback  hup;
} fd_event, *fd_event_handle;

/* * epoll manager object */
typedef struct epoll_manager
{
	pthread_mutex_t    lock;    // safe lock
    fd_event           fe;      // epoll fd event
	pthread_t          tid;     // epoll thread id
    em_callback        before;  // epoll_wait before
    em_callback        event;   // epoll_wait after,Before the event processing
    em_callback        after;   // After the completion of the event processing
	int                run;     // epoll manager thread control
	int                cfd[2];  // event notify and control -> fe.fd
	int                epfd;    // epoll_wait par1 epfd
	int                timeout; // epoll_wait par4 timeout
	int                maxfds;  // epoll_wait par3 maxevents
    int                nfds;    // epoll_wait's return value
	struct epoll_event evlist[];// epoll_wait par2 events
} epoll_manager;


#ifdef __cplusplus
extern "C"
{
#endif

/* * 使用 epoll_manager 步骤
 * 说明: 下述描述中em_前缀已省略
 *      epoll_manager 的一个实例简称 em
 *  1 . em_open 创建em所需要的环境及数据
 *  2 . em_run    运行em, 线程方式启动
 *  3 . notify_em_fd_event_add 向em里添加需要监视事件的fd_event
 *  4 . notify_em_fd_event_mod 从em里修改正在监视事件的fd_event
 *  5 . notify_em_fd_event_del 从em里删除正在监视事件的fd_event
 *  6 . em_stop随时停止em的epoll_wait处理线程
 *      停止之后如果继续可以再次em调用run开启处理线程 */
#ifdef __cplusplus
epoll_manager* em_open(int maxfds, int timeout, 
        em_callback before=0, em_callback events=0, em_callback after=0);
epoll_manager* Em_open(int maxfds, int timeout, 
        em_callback before=0, em_callback events=0, em_callback after=0);
#else
epoll_manager* em_open(int maxfds, int timeout, 
        em_callback before, em_callback events, em_callback after);
epoll_manager* Em_open(int maxfds, int timeout,
        em_callback before, em_callback events, em_callback after);
#endif
void Em_run(epoll_manager *em);
int  em_run(epoll_manager *em);
int  em_stop(epoll_manager *em);
int  em_close(epoll_manager *em);
int  em_set_timeout(epoll_manager *em, int timeout);

// 通过管道通知epoll修改所管理的描述符事件
// 作为使用管道缓存的方式实现
int notify_em_fd_event_add(struct fd_event *fe);
int notify_em_fd_event_mod(struct fd_event *fe);
int notify_em_fd_event_del(struct fd_event *fe, int flag);
int notify_em_fd_event_release(struct fd_event *fe, int flag);

// 直接调用 epoll_ctl 执行 ADD MOD DEL
//  返回 epoll_ctl的返回值
//  出错时会打印出错消息并且返回epoll_ctl的返回码
int  em_fd_event_add(fd_event* fe);
int  em_fd_event_mod(fd_event* fe);
int  em_fd_event_del(fd_event* fe);

// 直接调用 epoll_ctl 执行 ADD MOD DEL
//   出错时打印出错消息并且退出程序
void Em_fd_event_add(fd_event* fe);
void Em_fd_event_mod(fd_event* fe);
void Em_fd_event_del(fd_event* fe);

int  setfd_nonblock(int fd);
void Setfd_nonblock(int fd);
int  setsock_rcvtimeo(int fd, int second, int microsecond);
void Setsock_rcvtimeo(int fd, int second, int microsecond);
int close_all_fd(void);

fd_event* fd_event_new();
fd_event* Fd_event_new();
void fd_event_del(fd_event *p);
void fd_event_init(fd_event *fhp, epoll_manager *em, int fd);
#ifdef __cplusplus
void fd_event_set(fd_event *fh, int event, fd_event_callback cb = 0);
#else
void fd_event_set(fd_event *fh, int event, fd_event_callback cb);
#endif
void fd_event_unset(fd_event *fe, int event);
void fd_event_uninit(fd_event *fhp);
void fd_event_handle_release(fd_event_handle fe);

fd_event_handle  fd_event_handle_new();
void             fd_event_handle_del(fd_event_handle fe);

#ifdef __cplusplus
}
#endif

#endif /* __YH_YHEPOLL_H__ */
