#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define LISTENQ 20
#define SERV_PORT 8000

void setnonblocking(int sock)
{
	int opts;
	opts = fcntl(sock, F_GETFL);

	if (opts < 0)
	{
		perror("fcntl(sock, GETFL)");
		exit(1);
	}

	opts = opts | O_NONBLOCK;

	if (fcntl(sock, F_SETFL, opts) < 0)
	{
		perror("fcntl(sock, SETFL, opts)");
		exit(1);
	}
}

int main(int argc, char *argv[])
{
	//创建socket
	int sockSrv = socket(AF_INET, SOCK_STREAM, 0);
	perror("create socket");
	setnonblocking(sockSrv);

	//创建epoll fd
	int epfd = epoll_create(256);
	//congiure需要的event
	struct epoll_event ev;
	ev.data.fd = sockSrv;
	ev.events = EPOLLIN | EPOLLET;
	epoll_ctl(epfd, EPOLL_CTL_ADD, sockSrv, &ev);

	//常规的socket 流程
	//set listening address
	struct sockaddr_in serveraddr;
	bzero(&serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	inet_aton("127.0.0.1", &(serveraddr.sin_addr));
	serveraddr.sin_port = htons(SERV_PORT);

  //bind
	bind(sockSrv, (struct sockaddr *)&serveraddr, sizeof(serveraddr));
	perror("bind");

	//listen
	listen(sockSrv, LISTENQ);
	perror("srv listen");
	printf("epoll echo server listending on port %d \n", SERV_PORT);

	struct epoll_event events[20];
	char buf[200] = "\0";
	int max = sizeof(events) / sizeof(events[0]);
	for (;;)
	{
		//blocked for epoll wait
		int nEvents = epoll_wait(epfd, events, max, 500);
		perror("epoll_wait");
		printf("%d event happen!\n", nEvents);
		
		//与select需要检查所有的fd set 不同；epoll 只需要检查变动情况(event数组)
		for (int i = 0; i < nEvents; i++)
		{
			//有client 来connect
			if (events[i].data.fd == sockSrv)
			{
				struct sockaddr_in clientaddr;
				socklen_t clilen;
				//accept
				int sockCli = accept(sockSrv, (struct sockaddr *)&clientaddr, &clilen);
				perror("accept");
				if (sockCli < 0)
				{
					perror("sockCli < 0");
					exit(1);
				}
				printf("accept client fd=%d\n", sockCli);
				setnonblocking(sockCli);

				char *str = inet_ntoa(clientaddr.sin_addr);
				printf("connect from %s\n", str);

				ev.data.fd = sockCli;
				ev.events = EPOLLIN | EPOLLET | EPOLLOUT;
				//将client connection加入epoll fd监听列表
				epoll_ctl(epfd, EPOLL_CTL_ADD, sockCli, &ev);
				perror("epoll_ctl add");
				continue;
			}
			if (events[i].events & EPOLLIN)
			{
				int fd = events[i].data.fd;
				if (fd < 0)
					continue;

				ssize_t readval;
				readval = read(fd, buf, sizeof(buf)); //实际每个fd应该有自己的inBuffer,outBuffer
				perror("read");
				printf("received data: %s\n", buf);

				if (readval < 0)
				{
					if (errno == ECONNRESET)
					{
						epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
						perror("EPOLL_CTL_DEL");
						close(fd);
						continue;
					}
				}
				else if (readval == 0)
				{
					epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
					perror("EPOLL_CTL_DEL");
					close(fd);
					printf("FIN from  fd=%d\n", fd);
					continue;
				}
			}
			if (events[i].events & EPOLLOUT && buf[0] != '\0')
			{
				int fd = events[i].data.fd;
				write(fd, buf, strlen(buf));
				buf[0] = '\0';
				perror("write");
			}
		}
	}
}
