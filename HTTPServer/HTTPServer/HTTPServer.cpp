// HTTPServer.cpp : Defines the entry point for the application.
//

#include "HTTPServer.h"
#include "Connection.h"
#include "MsgQueue.h"
#include "ThreadPool.h"
#include "Sql.h"
#include "Session.h"



const int PORT = 8086; // port used by server for listen
const int LISTEN_BACKLOG = 100;
const int MAX_EVENTS = 10000;
const int MAX_CONNECTIONS = 10000;
const int MAX_MSGS_IN_QUE = 10000;
const int NUM_BUCKETS = 10; // num of session buckets
const int NUM_THREADS = 4;	// num of threads and sql connections
const int TIMEOUT = 600;	// in seconds
const int REQ_BUF_SIZE = 1024;		// size of request buffer of each connection
const int RESP_HEAD_BUF_SIZE = 1024;	// initial size of the buffer for the head of a response
const int RESP_BODY_BUF_SIZE = 1024;	// initial size of the buffer for the body of a response
const int SQL_PORT = 3306;
const char *const ROOT = "/home/y/.vs/HTTPServer/root/"; // path to root, like "/home/HTTPServer/root/", the last / is needed
const char *const HOST = "localhost";
const char *const SQL_USER = "";	 // sql username
const char *const SQL_PASSWORD = ""; // sql password
const char *const DATABASE = "HTTPServer";



int main()
{
	rlimit rlim{};
	rlim.rlim_cur = 65536;
	rlim.rlim_max = 65536;
	if (-1 == setrlimit(RLIMIT_NOFILE, &rlim))
	{
		perror("setrlimit");
		return 1;
	}

	struct sigaction sa {};
	sa.sa_handler = SIG_IGN;
	sa.sa_flags = 0;
	if (sigaction(SIGPIPE, &sa, NULL) == -1)
	{
		perror("sigaction");
		return 1;
	}


	std::string filePath = ROOT;
	filePath += "index.html";
	int homePageFd = open(filePath.data(), O_RDONLY);
	if (-1 == homePageFd)
	{
		perror("open home page");
		return 1;
	}
	struct stat sb {};
	fstat(homePageFd, &sb);
	int homePageSize = sb.st_size;
	char *homePageAddr = (char *)mmap(0, homePageSize, PROT_READ, MAP_PRIVATE, homePageFd, 0);

	filePath = ROOT;
	filePath += "styles.css";
	int cssFd = open(filePath.data(), O_RDONLY);
	if (-1 == cssFd)
	{
		perror("open css file");
		return 1;
	}
	fstat(cssFd, &sb);
	int cssSize = sb.st_size;
	char *cssAddr = (char *)mmap(0, cssSize, PROT_READ, MAP_PRIVATE, cssFd, 0);

	filePath = ROOT;
	filePath += "script.js";
	int jsFd = open(filePath.data(), O_RDONLY);
	if (-1 == jsFd)
	{
		perror("open js file");
		return 1;
	}
	fstat(jsFd, &sb);
	int jsSize = sb.st_size;
	char *jsAddr = (char *)mmap(0, jsSize, PROT_READ, MAP_PRIVATE, jsFd, 0);


	int listenFd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
	if (listenFd == -1)
	{
		perror("create listen socket");
		return 1;
	}
	int flag = 1;
	setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));

	sockaddr_in serverAddr{};
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(PORT);
	serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(listenFd, (sockaddr *)&serverAddr, sizeof(serverAddr)) == -1)
	{
		perror("bind");
		return 1;
	}

	if (listen(listenFd, LISTEN_BACKLOG) == -1)
	{
		perror("listen");
		return 1;
	}


	int epollFd = epoll_create1(0);
	if (epollFd == -1)
	{
		perror("epoll_create1");
		return 1;
	}


	epoll_event evt{};
	evt.events = EPOLLIN | EPOLLET;
	evt.data.fd = listenFd;
	if (epoll_ctl(epollFd, EPOLL_CTL_ADD, listenFd, &evt) == -1)
	{
		perror("epoll_ctl: add listenFd");
		return 1;
	}

	int timerFd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
	if (timerFd == -1) {
		perror("timerfd_create");
		return 1;
	}

	itimerspec timer_spec{};
	timer_spec.it_value.tv_sec = 120;
	timer_spec.it_interval.tv_sec = 120;
	timerfd_settime(timerFd, 0, &timer_spec, nullptr);

	evt.events = EPOLLIN | EPOLLET;
	evt.data.fd = timerFd;
	if (epoll_ctl(epollFd, EPOLL_CTL_ADD, timerFd, &evt) == -1)
	{
		perror("epoll_ctl: add timerFd");
		return 1;
	}


	sigset_t mask;
	sigemptyset(&mask);
	sigaddset(&mask, SIGINT);
	sigaddset(&mask, SIGQUIT);
	if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1)
	{
		perror("sigprocmask");
		return 1;
	}

	int sigFd = signalfd(-1, &mask, SFD_NONBLOCK);
	if (sigFd == -1)
	{
		perror("signalfd");
		return 1;
	}

	evt.events = EPOLLIN | EPOLLET;
	evt.data.fd = sigFd;
	if (epoll_ctl(epollFd, EPOLL_CTL_ADD, sigFd, &evt) == -1)
	{
		perror("epoll_ctl: add sigFd");
		return 1;
	}


	SqlPool sqlPool(NUM_THREADS);
	if (!sqlPool.initSqlPool(HOST, SQL_USER, SQL_PASSWORD, DATABASE, SQL_PORT))
	{
		std::cout << "init sql pool error" << std::endl;
		return 1;
	}


	Que evtQue(MAX_MSGS_IN_QUE);

	Session sessions(NUM_BUCKETS);

	std::vector<std::unique_ptr<Connection>> conns;
	for (int i = 0; i < MAX_CONNECTIONS; ++i)
	{
		conns.emplace_back(std::make_unique<Connection>(REQ_BUF_SIZE, RESP_HEAD_BUF_SIZE, RESP_BODY_BUF_SIZE, ROOT, homePageAddr,
			homePageSize, cssAddr, cssSize, jsAddr, jsSize, epollFd, i, sqlPool, sessions));
	}

	ThreadPool threadPool(NUM_THREADS, evtQue, conns, MAX_CONNECTIONS, TIMEOUT, epollFd, sessions);


	epoll_event events[MAX_EVENTS];

	bool stopRunning = false;

	std::cout << "Server Started" << std::endl;

	while (!stopRunning)
	{
		int numEvents = epoll_wait(epollFd, events, MAX_EVENTS, -1);
		if (numEvents == -1)
		{
			perror("epoll_wait");
			break;
		}

		for (int i = 0; i < numEvents; ++i)
		{
			if (events[i].data.fd == listenFd)
			{
				evtQue.pushBack({ listenFd, EventType::ACCEPT });
			}
			else if (events[i].data.fd == timerFd)
			{
				evtQue.pushBack({ (int)events[i].data.fd, EventType::ACTIVITY_TIMER });
			}
			else if (events[i].data.fd == sigFd)
			{
				signalfd_siginfo fdsi;
				if (read(sigFd, &fdsi, sizeof(struct signalfd_siginfo)) == -1)
				{
					perror("read: sigFd");
					continue;
				}
				if (fdsi.ssi_signo == SIGINT || fdsi.ssi_signo == SIGQUIT)
				{
					stopRunning = true;
					break;
				}
			}
			else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
			{
				evtQue.pushBack({ (int)events[i].data.fd, EventType::RESET });
			}
			else if (events[i].events & EPOLLIN)
			{
				evtQue.pushBack({ (int)events[i].data.fd, EventType::IN });
			}
			else if (events[i].events & EPOLLOUT)
			{
				evtQue.pushBack({ (int)events[i].data.fd, EventType::OUT });
			}
		}
	}

	munmap(homePageAddr, homePageSize);
	munmap(cssAddr, cssSize);
	munmap(jsAddr, jsSize);
	close(homePageFd);
	close(cssFd);
	close(jsFd);
	close(epollFd);
	close(listenFd);
	close(timerFd);
	close(sigFd);
	std::cout << "\nServer Stopped" << std::endl;
	return 0;
}
