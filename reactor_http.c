#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <arpa/inet.h>

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sendfile.h>


#define BUFFER_LENGTH		4096
#define MAX_EPOLL_EVENTS	1024
#define SERVER_PORT			10000

#define HTTP_WEBSERVER_HTML_ROOT	"resources"

#define HTTP_METHOD_GET		0
#define HTTP_METHOD_POST	1


typedef int NCALLBACK(int ,int, void*);

struct ntyevent {
	int fd;//文件描述符
	int events;//事件
	void *arg;//回调函数的参数
	int (*callback)(int fd, int events, void *arg);
	
	int status;
	char buffer[BUFFER_LENGTH];//缓存区
	int length;//
	long last_active;

    // http param
	int method; //请求的方法
	char resource[BUFFER_LENGTH];
	int ret_code;//保存响应的状态码
};



struct ntyreactor {//结构体包含一个文件描述符和一个事件指针
	int epfd;
	struct ntyevent *events;
};


int recv_cb(int fd, int events, void *arg);
int send_cb(int fd, int events, void *arg);


void nty_event_set(struct ntyevent *ev, int fd, NCALLBACK callback, void *arg) {

	ev->fd = fd;
	ev->callback = callback;
	ev->events = 0;
	ev->arg = arg;
	ev->last_active = time(NULL);

	return ;
	
}


int nty_event_add(int epfd, int events, struct ntyevent *ev) {

	struct epoll_event ep_ev = {0, {0}};
	ep_ev.data.ptr = ev;//设置要处理的事件的指针，含有文件描述符等信息
	ep_ev.events = ev->events = events;//设置要处理的事件类型

	int op;
	if (ev->status == 1) {
		op = EPOLL_CTL_MOD;
	} else {
		op = EPOLL_CTL_ADD;
		ev->status = 1;
	}

	if (epoll_ctl(epfd, op, ev->fd, &ep_ev) < 0) {//注册epoll事件
		printf("event add failed [fd=%d], events[%d]\n", ev->fd, events);
		return -1;
	}

	return 0;
}

int nty_event_del(int epfd, struct ntyevent *ev) {

	struct epoll_event ep_ev = {0, {0}};

	if (ev->status != 1) {
		return -1;
	}

	ep_ev.data.ptr = ev;
	ev->status = 0;
	epoll_ctl(epfd, EPOLL_CTL_DEL, ev->fd, &ep_ev);//删除注册epoll事件

	return 0;
}
//readline实现请求报文中一行数据的读取，以\r\n为每行的最终位置
int readline(char *allbuf, int idx, char *linebuf) {
    int len = strlen(allbuf);
    for(;idx < len;idx ++) {
		if (allbuf[idx] == '\r' && allbuf[idx+1] == '\n') {
			return idx+2;
		} else {
			*(linebuf++) = allbuf[idx];
		}
	}
    return -1;

}
//实现GET与POST请求的区分
int http_request(struct ntyevent *ev) {

	// GET, POST
	char linebuf[1024] = {0};
	int idx = readline(ev->buffer, 0, linebuf);

	if (strstr(linebuf, "GET")) {//strstr()查找第一次出现字符串 GET 的位置，不包含终止符 '\0'。
		ev->method = HTTP_METHOD_GET;

		//uri
		int i = 0;
		while (linebuf[sizeof("GET ") + i] != ' ') i++;
		linebuf[sizeof("GET ")+i] = '\0';

		sprintf(ev->resource, "./%s/%s", HTTP_WEBSERVER_HTML_ROOT, linebuf+sizeof("GET "));
		//将要访问的资源存入ev->resource
	} else if (strstr(linebuf, "POST")) {
        //
	}

}


int recv_cb(int fd, int events, void *arg) {

	struct ntyreactor *reactor = (struct ntyreactor*)arg;
	struct ntyevent *ev = reactor->events+fd;

	int len = recv(fd, ev->buffer, BUFFER_LENGTH, 0); //BUFFER_LENGTH   //读取fd的数据到相应的buffer 中，指定buffer长度（可以理解乘读取数据最大长度）
	

	if (len > 0) {
		
		ev->length = len;
		ev->buffer[len] = '\0';

		printf("C[%d]:%s\n", fd, ev->buffer);
        http_request(ev);
        nty_event_del(reactor->epfd, ev);
		nty_event_set(ev, fd, send_cb, reactor);//设置send_cb回调函数
		nty_event_add(reactor->epfd, EPOLLOUT, ev);//注册EPOLLOUT事件
		
		
	} else if (len == 0) {//关闭连接
        nty_event_del(reactor->epfd, ev);
		close(ev->fd);
		printf("[fd=%d] pos[%ld], closed\n", fd, ev-reactor->events);
		 
	} else {
        nty_event_del(reactor->epfd, ev);
		close(ev->fd);
		printf("recv[fd=%d] error[%d]:%s\n", fd, errno, strerror(errno));
		
	}

	return len;
}

int http_response(struct ntyevent *ev) {

	if (ev == NULL) return -1;
	memset(ev->buffer, 0, BUFFER_LENGTH);

	printf("resource: %s\n", ev->resource);

	int filefd = open(ev->resource, O_RDONLY);
	if (filefd == -1) { // 找不到资源return 404

		ev->ret_code = 404;
		ev->length = sprintf(ev->buffer, 
			"HTTP/1.1 404 Not Found\r\n"
		 	"Date: Thu, 11 Nov 2021 12:28:52 GMT\r\n"
		 	"Content-Type: text/html;charset=ISO-8859-1\r\n"
			"Content-Length: 85\r\n\r\n"
		 	"<html><head><title>404 Not Found</title></head><body><H1>404</H1></body></html>\r\n\r\n" );
	} else {//找到资源了
		struct stat stat_buf;
		fstat(filefd, &stat_buf);//采用fstat可以获得数据的长度
		close(filefd);
		if (S_ISDIR(stat_buf.st_mode)) {//如果是只是找到目录也返回404
			ev->ret_code = 404;
			ev->length = sprintf(ev->buffer, 
				"HTTP/1.1 404 Not Found\r\n"
				"Date: Thu, 11 Nov 2021 12:28:52 GMT\r\n"
				"Content-Type: text/html;charset=ISO-8859-1\r\n"
				"Content-Length: 85\r\n\r\n"
				"<html><head><title>404 Not Found</title></head><body><H1>404</H1></body></html>\r\n\r\n" );

		} else if (S_ISREG(stat_buf.st_mode)) {//如果是文件就说明可以返回正确的资源了
			ev->ret_code = 200;
			ev->length = sprintf(ev->buffer, 
				"HTTP/1.1 200 OK\r\n"
			 	"Date:Wed, 13 Apr 2022 02:01:06 GMT\r\n"
			 	"Content-Type: text/html;charset=ISO-8859-1\r\n"
				"Content-Length: %ld\r\n\r\n", 
			 	stat_buf.st_size );
		}
	}
	return ev->length;
}


int send_cb(int fd, int events, void *arg) {

	struct ntyreactor *reactor = (struct ntyreactor*)arg;
	struct ntyevent *ev = reactor->events+fd;

    http_response(ev);

	int len = send(fd, ev->buffer, ev->length, 0);//发送数据
	if (len > 0) {
		printf("send[fd=%d], [%d]%s\n", fd, len, ev->buffer);

        if (ev->ret_code == 200) {
			int filefd = open(ev->resource, O_RDONLY);
			struct stat stat_buf;
			fstat(filefd, &stat_buf);//采用fstat，可以比较方便地拿到文件的大小

			sendfile(fd, filefd, NULL, stat_buf.st_size);//用sendfile发送文件，属于零拷贝，节省资源
			close(filefd);
		}


		nty_event_del(reactor->epfd, ev);//删除epoll注册事件
		nty_event_set(ev, fd, recv_cb, reactor);//设置回调函数
		nty_event_add(reactor->epfd, EPOLLIN | EPOLLET, ev);//条件epoll注册事件
		
	} else {

		close(ev->fd);

		nty_event_del(reactor->epfd, ev);
		printf("send[fd=%d] error %s\n", fd, strerror(errno));

	}

	return len;
}

int accept_cb(int fd, int events, void *arg) {

	struct ntyreactor *reactor = (struct ntyreactor*)arg;
	if (reactor == NULL) return -1;

	struct sockaddr_in client_addr;
	socklen_t len = sizeof(client_addr);

	int clientfd;

	if ((clientfd = accept(fd, (struct sockaddr*)&client_addr, &len)) == -1) {//accept绑定已连接队列中的socket
		if (errno != EAGAIN && errno != EINTR) {
			
		}
		printf("accept: %s\n", strerror(errno));
		return -1;
	}

	int i = 0;
	do {
		
		for (i = 3;i < MAX_EPOLL_EVENTS;i ++) {//0，1，2是系统固定的文件描述符
			if (reactor->events[i].status == 0) {//
				break;
			}
		}
		if (i == MAX_EPOLL_EVENTS) {
			printf("%s: max connect limit[%d]\n", __func__, MAX_EPOLL_EVENTS);
			break;
		}

		int flag = 0;
		if ((flag = fcntl(clientfd, F_SETFL, O_NONBLOCK)) < 0) {//设置与客户端通信的socket为非阻塞
			printf("%s: fcntl nonblocking failed, %d\n", __func__, MAX_EPOLL_EVENTS);
			break;
		}

		nty_event_set(&reactor->events[clientfd], clientfd, recv_cb, reactor);  //设置当前fd的回调函数等信息
		nty_event_add(reactor->epfd, EPOLLIN | EPOLLET, &reactor->events[clientfd]);//添加当前fd的触发信号EPOLLIN | EPOLLET

	} while (0);

	printf("new connect [%s:%d][time:%ld], pos[%d]\n", 
		inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), reactor->events[i].last_active, i);//输出连接的地址：端口：时间：fd

	return 0;

}

int init_sock(short port) {

	int fd = socket(AF_INET, SOCK_STREAM, 0);//创建socket文件描述符
	fcntl(fd, F_SETFL, O_NONBLOCK);//设置非阻塞fd

	struct sockaddr_in server_addr;
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	server_addr.sin_port = htons(port);

	bind(fd, (struct sockaddr*)&server_addr, sizeof(server_addr));//绑定端口和IP地址

	if (listen(fd, 20) < 0) {//监听端口
		printf("listen failed : %s\n", strerror(errno));
	}

	return fd;
}


int ntyreactor_init(struct ntyreactor *reactor) {

	if (reactor == NULL) return -1;
	memset(reactor, 0, sizeof(struct ntyreactor));//memset清0

	reactor->epfd = epoll_create(1); //创建epoll，返回文件描述符根节点（epoll是以红黑树存储描述符）
	if (reactor->epfd <= 0) {
		printf("create epfd in %s err %s\n", __func__, strerror(errno));
		return -2;
	}

	reactor->events = (struct ntyevent*)malloc((MAX_EPOLL_EVENTS) * sizeof(struct ntyevent));//分配存储事件的空间
	if (reactor->events == NULL) {
		printf("create epfd in %s err %s\n", __func__, strerror(errno));
		close(reactor->epfd);
		return -3;
	}
}

int ntyreactor_destory(struct ntyreactor *reactor) {

	close(reactor->epfd);
	free(reactor->events);

}



int ntyreactor_addlistener(struct ntyreactor *reactor, int sockfd, NCALLBACK *acceptor) {

	if (reactor == NULL) return -1;
	if (reactor->events == NULL) return -1;

	nty_event_set(&reactor->events[sockfd], sockfd, acceptor, reactor);//设置回调函数
	nty_event_add(reactor->epfd, EPOLLIN, &reactor->events[sockfd]);//添加epoll注册事件

	return 0;
}



int ntyreactor_run(struct ntyreactor *reactor) {
	if (reactor == NULL) return -1;
	if (reactor->epfd < 0) return -1;
	if (reactor->events == NULL) return -1;
	
	struct epoll_event events[MAX_EPOLL_EVENTS+1];
	
	int checkpos = 0, i;

	while (1) {//此处循环是看连接是否超时，采用轮询验证

		long now = time(NULL);
		for (i = 0;i < 100;i ++, checkpos ++) {
			if (checkpos == MAX_EPOLL_EVENTS) {
				checkpos = 0;
			}

			if (reactor->events[checkpos].status != 1) {
				continue;
			}
		}


		int nready = epoll_wait(reactor->epfd, events, MAX_EPOLL_EVENTS, 1000);//events用于回传要处理的事件
		if (nready < 0) {
			printf("epoll_wait error, exit\n");
			continue;
		}

		for (i = 0;i < nready;i ++) {

			struct ntyevent *ev = (struct ntyevent*)events[i].data.ptr;

			if ((events[i].events & EPOLLIN) && (ev->events & EPOLLIN)) {//监控epoll事件EPOLLIN
				ev->callback(ev->fd, events[i].events, ev->arg);//callback
			}
			if ((events[i].events & EPOLLOUT) && (ev->events & EPOLLOUT)) {//监控epoll事件EPOLLOUT
				ev->callback(ev->fd, events[i].events, ev->arg);//callback
			}
			
		}

	}
}

int main(int argc, char *argv[]) {

	unsigned short port = SERVER_PORT;
	if (argc == 2) {
		port = atoi(argv[1]);
	}

	int sockfd = init_sock(port);//传入端口号，初始化socket，，返回得到监听的socketfd

	struct ntyreactor *reactor = (struct ntyreactor*)malloc(sizeof(struct ntyreactor));//创建ntyreactor结构体对象
	ntyreactor_init(reactor);//初始化结构体，， 包含一个文件描述符和一个ntyreact类型的事件指针,reactor中epfd是监听红黑树的根节点fd
	
	ntyreactor_addlistener(reactor, sockfd, accept_cb);
	ntyreactor_run(reactor);

	ntyreactor_destory(reactor);
	close(sockfd);
	

	return 0;
}


