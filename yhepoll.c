#include <sys/socket.h>
#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <dirent.h>
#include "yhepoll.h"
#include "error.h"

fd_event * fd_event_new()
{
    fd_event * fe = (fd_event*)calloc(1, sizeof(fd_event));
    if (fe == 0) {
        err_ret("fd_event_new() calloc error[%d]", errno);
        return 0;
    }
    fe->heap = FD_EVENT_MAGIC_HEAP;  // 堆申请标志
    fe->fd = -1;
    return fe;
}
fd_event * Fd_event_new()
{
    fd_event *fe = fd_event_new();
    if (fe == 0) exit(1);
    return fe;
}
fd_event_handle fd_event_handle_new()
{
    return fd_event_new();
}
void fd_event_handle_del(fd_event_handle fe)
{
    fd_event_del(fe);
}
void fd_event_handle_release(fd_event_handle fe)
{
    fd_event_uninit(fe);
    fd_event_del(fe);
}
void fd_event_init(fd_event *fe, epoll_manager *em, int fd)
{
    fe->event.data.ptr  = fe; 
    fe->fd              = fd;
    fe->em              = em;
}
void fd_event_uninit(fd_event *fe)
{
    close(fe->fd);
    fe->fd    = -1; 
}
void fd_event_del(fd_event *fe)
{
    if (fe == 0) return;
    if (fe->heap == FD_EVENT_MAGIC_HEAP) 
        free(fe);
}
void fd_event_set(fd_event *fe, int event, fd_event_callback cb)
{
    fe->event.events |= event;
    switch (event) {
        case EPOLLIN  : fe->in  = cb; break;
        case EPOLLOUT : fe->out = cb; break;
        case EPOLLPRI : fe->pri = cb; break;
        case EPOLLERR : fe->err = cb; break;
        case EPOLLHUP : fe->hup = cb; break;
        case EPOLLET  : break;
        default:    break;
    };
}
void fd_event_unset(fd_event *fe, int event)
{
    fe->event.events &= ~event;
}
int em_fd_event_add(fd_event* fe)
{
    int ret = epoll_ctl(fe->em->epfd, EPOLL_CTL_ADD, fe->fd, &fe->event);
    if (ret < 0) err_ret("em_fd_event_add() epoll_ctl %d fd %d error[%d]"
            , fe->em->epfd, fe->fd, errno);
    return ret;
}
int em_fd_event_mod(fd_event* fe)
{
    int ret =  epoll_ctl(fe->em->epfd, EPOLL_CTL_MOD, fe->fd, &fe->event);
    if (ret < 0) err_ret("em_fd_event_mod() epoll_ctl %d fd %d error[%d]"
            , fe->em->epfd, fe->fd, errno);
    return ret;
}
int em_fd_event_del(fd_event* fe)
{
    int ret = epoll_ctl(fe->em->epfd, EPOLL_CTL_DEL, fe->fd, &fe->event);
    if (ret < 0) err_ret("em_fd_event_del() epoll_ctl %d fd %d error[%d]"
            , fe->em->epfd, fe->fd, errno);
    return ret;
}
void Em_fd_event_add(fd_event* fe)
{
    if (em_fd_event_add(fe) < 0) exit(1);
}
void Em_fd_event_mod(fd_event* fe)
{
    if (em_fd_event_mod(fe) < 0) exit(1);
}
void Em_fd_event_del(fd_event* fe)
{
    if (em_fd_event_del(fe) < 0) exit(1);
}
void fd_event_release(fd_event *fe, int flag)
{
    if (1 == flag) { fd_event_uninit(fe); fd_event_del(fe); }
}
static inline void em_event_process(fd_event *fe);
int setfd_nonblock(int fd)
{
    int status;
    if ((status = fcntl(fd, F_GETFL)) < 0) { 
        err_ret("setfd_nonblock() fcntl F_GETFL error[%d]", errno); 
        return -1; 
    }
    status |= O_NONBLOCK;
    if (fcntl(fd, F_SETFL, status) < 0) { 
        err_ret("setfd_nonblock() fcntl F_SETFL error[%d]", errno); 
        return -1; 
    }
    return 0;
}
int setsock_rcvtimeo(int fd, int second, int microsecond)
{
    struct timeval rcv_timeo = {second, microsecond}; 
    if (setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&rcv_timeo,sizeof(rcv_timeo))< 0) {
        err_ret("setsock_rcvtimeo() setsockopt SO_RCVTIMEO error[%d]", errno);
        return -1; 
    }
    return 0;
}
void Setfd_nonblock(int fd)
{
    if ( setfd_nonblock(fd) < 0 ) exit(1);
}
void Setsock_rcvtimeo(int fd, int second, int microsecond)
{
    if ( setsock_rcvtimeo(fd, second, microsecond) < 0 ) exit(1);
}
/* * create epoll manager */
epoll_manager* em_open(int maxfds, int timeout, 
        em_callback before, em_callback events, em_callback after)
{
    epoll_manager *em = 0; 
    if (maxfds <= 0) goto err_out;
    em = (epoll_manager*)calloc(1, sizeof(epoll_manager) + 
                (maxfds+1) * sizeof(struct epoll_event));
    em->timeout = timeout;
    em->maxfds  = maxfds + 1;   // +1 epoll manager cfd[0]
    em->before  = before;
    em->event   = events;
    em->after   = after;
    em->run     = 0;
    if ( (em->epfd = epoll_create(maxfds)             )  < 0 )  goto err_out;
    if ( (socketpair(PF_UNIX, SOCK_STREAM, 0, em->cfd))  < 0 )  goto err_out;
    if ( (pthread_mutex_init(&em->lock, 0)            ) != 0 )  goto err_out;
    fd_event_init(&em->fe, em, em->cfd[0]);	
    setfd_nonblock(em->fe.fd);
    fd_event_set(&em->fe, EPOLLET, 0);
    fd_event_set(&em->fe, EPOLLIN, em_event_process);
    if (em_fd_event_add(&em->fe) < 0)   goto err_out;
    return em;
err_out:
    if (em->epfd   >= 0) { close(em->epfd);   }
    if (em->cfd[0] >= 0) { close(em->cfd[0]); }
    if (em->cfd[1] >= 0) { close(em->cfd[1]); }
    if (em         != 0) { free(em);          }
    return 0;
}
epoll_manager* Em_open(int maxfds, int timeout,
        em_callback before, em_callback events, em_callback after)
{
    epoll_manager *em;
    if ((em = em_open(maxfds, timeout, before, events, after)) == 0) 
    { err_ret("Em_open() em_open error[%d]", errno); exit(1); }
    return em;
}
static void * em_thread(void *p)
{
    epoll_manager *em = (epoll_manager*)p;
    int n;
    void            *ptr;
    struct fd_event *fe = 0;
    pthread_detach(em->tid);
    while (em->run) 
    {
        if (em->before) em->before(em);
        em->nfds = epoll_wait(em->epfd, em->evlist, em->maxfds, em->timeout);
        if (em->event) em->event(em);
        for (n = 0; n < em->nfds; ++n) {
            ptr = em->evlist[n].data.ptr;
            if (ptr == 0) continue;
            fe = (fd_event*)ptr;
            if(em->evlist[n].events & EPOLLIN ) if(fe->in ) { fe->in (fe); }
            if(em->evlist[n].events & EPOLLOUT) if(fe->out) { fe->out(fe); }
            if(em->evlist[n].events & EPOLLPRI) if(fe->pri) { fe->pri(fe); }
            if(em->evlist[n].events & EPOLLHUP) if(fe->hup) { fe->hup(fe); }
            if(em->evlist[n].events & EPOLLERR) if(fe->err) { fe->err(fe); }
        }
        if (em->after) em->after(em);
    }
    return (void*)0;
}
int em_run(epoll_manager *em)
{
    int ret;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 1024*1024); //set stack size 1M
    pthread_mutex_lock(&em->lock);
    if (em->run) { pthread_mutex_unlock(&em->lock); return 0;}
    em->run = 1;
    pthread_mutex_unlock(&em->lock);
    if ((ret = pthread_create(&em->tid, &attr, em_thread, em)) != 0) {
        errno = ret;
        err_ret("em_start() pthread_create error[%d]", errno);
        return -1;
    }
    pthread_attr_destroy(&attr);
    return 0;
}
void Em_run(epoll_manager *em)
{
    if (em_run(em) < 0) exit(1);
}
    
int em_fd_event_notify(fd_event *fe, char ch, int flag)
{
    struct iovec put[3];
    put[0].iov_base = &fe;    put[0].iov_len = sizeof(fe);
    put[1].iov_base = &ch;    put[1].iov_len = sizeof(ch);
    put[2].iov_base = &flag;  put[2].iov_len = sizeof(flag);
    int ret = writev(fe->em->cfd[1], put, 3);
    if (ret < 0) 
        err_ret("em_fd_event_notify() writev [%d] [0x%08x:%d %c %d] error[%d]"
                , fe->em->cfd[1], fe, fe->fd,ch,flag,errno);
//    else
//        err_msg("em_fd_event_notify() writev [%d] [0x%08x:%d %c %d] success\n"
//                , fe->em->cfd[1], (unsigned)fe, fe->fd, ch, flag);
    return ret;

}
int em_notify(epoll_manager *em, char ch, int flag)
{
    struct iovec put[3];
    void *p = 0;
    put[0].iov_base = &p;     put[0].iov_len = sizeof(void*);
    put[1].iov_base = &ch;    put[1].iov_len = sizeof(char);
    put[2].iov_base = &flag;  put[2].iov_len = sizeof(int);
    int ret = writev(em->cfd[1], put, 3);
    if (ret < 0) err_ret("em_notify() writev [%d] [%c] error[%d]",
            em->cfd[1], ch, errno);
    return ret;
}
int em_stop(epoll_manager *em)
{
    int ret;
    pthread_mutex_lock(&em->lock);
    if (em->run > 0) {
        if ((ret = em_notify(em, 'S', 0)) < 0) { 
            err_ret("em_stop() em_notify error[%d]", errno);
            pthread_mutex_unlock(&em->lock); 
            return ret;
        }
    }
    pthread_mutex_unlock(&em->lock);
    return 0;
}
void Em_stop(epoll_manager *em)
{
    if (em_stop(em) < 0) exit(1);
}
int em_set_timeout(epoll_manager *em, int timeout)
{
    em->timeout = timeout;
    return em_notify(em, 'T', timeout);
}
int em_close(epoll_manager *em)
{
    int ret = em_notify(em, 'C', 0);
    if (ret < 0)
        err_ret("em_close() em_nitofy error[%d]", errno);
    return ret;
}
void Em_close(epoll_manager *em)
{
    if (em_close(em) < 0) exit(1);
}

// em_close 在实际应用中应该用不到的
// 销毁epoll manager后那些EPOLL_CTL_ADD的fd_event如果是堆上
// 获取的需要手动去清理 否则就会内存泄漏
// 如果是在epoll循环的外部close(epfd)后,
// 如果没有事件到来,epoll_wait还会处在等待状态
//   因它正在等待内核事件,只有这次epoll_wait返回后epfd才失效
//   此时有可能会带出来一些epoll_event,
//   而此时吐出的事件列表是刚才epoll_wait收集到的有效事件列表
//   这些事件应该要被处理，如果此时另外一个线程又正在清理这些
//   资源例如堆申请的fd_event的话, 将会出现不可预知的问题
// 因此, 为解决这个问题将close(epfd)的事情放到了epoll循环内部
// 去做，在epoll_manager的fd_event的管道读事件触发这一操作，
// 将关闭和清理工作完成后，epoll_wait将不在进入下次wait循环
//
// em_close在实际应用中并不推荐使用,理由有2:
// 1. epfd 关闭后加入监听的fd如果有事件来到将无法处理,
//      对于网络上的连接而言,如果加入了epoll监视之中
//      下次事件到来将无法有效处理,除非采取其他机制。
// 2. epoll作为一种事件触发机制应该随程序退出而停止
//      它的生存期应该是服务程序启动后到关闭这段时间
int em_close_event(epoll_manager *em)
{
    em->run = 0;
    if (em->epfd   > 0) { close(em->epfd);   em->epfd   = -1; }
    if (em->cfd[0] > 0) { close(em->cfd[0]); em->cfd[0] = -1; }
    if (em->cfd[1] > 0) { close(em->cfd[1]); em->cfd[1] = -1; }
    pthread_mutex_destroy(&em->lock);
    if (em            ) { free(em);}
    return 0;
}
#define EPOLL_EVENT_READV_ERR_EXIT()  \
    do { \
        err_sys("em_event_process() readv error[%d]", errno); \
    } while (0)
#define EPOLL_EVENT_READV_ERR()  \
    do { \
        err_ret("em_event_process() readv error[%d]", errno); \
    } while (0)
//   fd_event的事件回调调用notify_em_fd_event_release, 此处需注意
//     外部如果关闭fd后发送'R'到epoll manager清理时候调用
//     epoll_ctl EPOLL_CTL_DEL 会失败: Bad file descriptor
//     epoll_ctl 处理某个fd的时候该fd必须有效才会成功
//
//   如果在此处关闭和清理资源操作又会和外部不一致
//          比较纠结的问题
//   解决方法:如果非ET模式fd_event的回调中直接调用操作epoll_ctl函数
//             这样做有违一致性, 勉强能接受。 
//   -------------------------------------------------------
//   特别注意, 关闭时的时间触发和资源清理的操作：要避免多次操作
//      如果fd设置了ET模式此处没有问题, 仅收到一次通知
//      但如果是非ET模式, 对端socket关闭时, 如果不及时处理将
//      收到多次通知而调用到此处
//      所以在非ET模式时, 收到socket关闭消息时第一时间应清理而
//      不调用 em_fd_event_notify 发送消息到em_event_process来处理
//      在notify_em_fd_event_release 实现时正是这么做的
// 'S' 处理完本轮事件后才退出epoll循环,否则可能丢失事件,如ET模式
void em_event_process(fd_event *ptr)
{
#ifdef __cplusplus
    epoll_manager *em = (epoll_manager*)((char*)ptr-offsetof(epoll_manager,fe));
#else
    epoll_manager *em = (epoll_manager*)struct_entry(ptr, epoll_manager, fe);
#endif
    fd_event      *emfe = &em->fe;
    int fd = emfe->fd;
    char ch; 
    int flag = 0;
    fd_event *fe;
    struct iovec get[3];
    get[0].iov_base = &fe;   get[0].iov_len = sizeof(fe);
    get[1].iov_base = &ch;   get[1].iov_len = sizeof(ch);
    get[2].iov_base = &flag; get[2].iov_len = sizeof(flag);
    int ret;
    while (em->run) {
        if ( (ret = readv(fd, get , 3)) < 0 ) 
        { if (errno == EAGAIN) break; else EPOLL_EVENT_READV_ERR(); }
        switch (ch) {
            case 'A': em_fd_event_add(fe); break;
            case 'M': em_fd_event_mod(fe); break;
            case 'D': em_fd_event_del(fe); break;
            case 'R': em_fd_event_del(fe); fd_event_release(fe, flag); break;
            case 'T': em->timeout = flag ; break;
            case 'S': em->run = 0;         break;
            case 'C': em_close_event(em); pthread_exit(0); break;
            default : break;
        }
    }
}
int notify_em_fd_event_add(struct fd_event *fe)
{
    return em_fd_event_notify(fe, 'A', 0);
}
int notify_em_fd_event_mod(struct fd_event *fe)
{
    return em_fd_event_notify(fe, 'M', 0);
}
int notify_em_fd_event_del(struct fd_event *fe, int flag)
{
    return em_fd_event_notify(fe, 'D', 0);
}
// release 执行两步操作，关闭fd和释放申请的堆内存
int notify_em_fd_event_release(struct fd_event *fe, int flag)
{
    if ( (fe->event.events & EPOLLET) ) 
        return em_fd_event_notify(fe, 'R', flag);
    em_fd_event_del(fe); fd_event_release(fe, flag);
    return 0;
}

int close_all_fd(void)
{
    DIR *dir;
    struct dirent *entry, _entry;
    int retval, rewind, fd;
    dir = opendir("/dev/fd");
    if (dir == NULL) 
        return -1;
    rewind = 0;
    while (1) { 
        retval = readdir_r(dir, &_entry, &entry); 
        if (retval != 0) {
            errno = -retval;
            retval = -1;
            break;
        }
        if (entry == NULL) {
            if (!rewind)
                break;
            rewinddir(dir);
            rewind = 0;
            continue;
        }
        if (entry->d_name[0] == '.')
            continue;
        fd = atoi(entry->d_name);
        if (dirfd(dir) == fd)
            continue;
#ifdef MYPERF
        if (fd == 1)
            continue;
#endif
        retval = close(fd);
        if (retval != 0)
            break;
        rewind = 1;
    }
    closedir(dir);
    return retval;
}
