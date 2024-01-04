#pragma once

#include "HTTPServer.h"
#include "MsgQueue.h"
#include "Connection.h"
#include "Session.h"


class ThreadPool
{
public:
	ThreadPool(int num, Que &q, std::vector<std::unique_ptr<Connection>> &conns, int maxConns, int timeout, int epollFd, Session &ses):
		m_msgQue(q), m_conns(conns), m_maxConns(maxConns), m_timeout(timeout), m_epollFd(epollFd), m_sessions(ses)
	{
		for (int i = 0; i < num; ++i)
		{
			m_pool.emplace_back([this, i] { worker(i); });
		}
	}


	~ThreadPool()
	{
		int size = m_pool.size();
		for (int i = 0; i < size; ++i)
		{
			m_msgQue.pushBack({ -1, EventType::STOP });
		}
		for (int i = 0; i < size; ++i)
		{
			if (m_pool[i].joinable())
				m_pool[i].join();
		}
	}


private:
	void worker(int _threadID)
	{
		int threadID = _threadID;

		while (true)
		{
			Message cur = m_msgQue.popFront();

			switch (cur.type)
			{
			case EventType::ACCEPT:
			{
				acceptNew(cur.fd);
				break;
			}
			case EventType::IN:
			{
				if (m_conns[cur.fd]->readReq() != IOStatus::ERROR)
				{
					if (m_conns[cur.fd]->process() == IOStatus::DONE)
					{
						m_msgQue.pushBack({ cur.fd, EventType::OUT });
					}
					else
					{
						m_conns[cur.fd]->changeFd(EPOLLIN);
					}
				}
				else
				{
					m_msgQue.pushBack({ cur.fd, EventType::RESET });
				}
				break;
			}
			case EventType::OUT:
			{
				IOStatus res = m_conns[cur.fd]->writeResp();
				if (IOStatus::DONE == res)
				{
					m_conns[cur.fd]->clean();
					m_conns[cur.fd]->changeFd(EPOLLIN);
				}
				else if (IOStatus::NOTDONE == res)
				{
					m_conns[cur.fd]->changeFd(EPOLLOUT);
				}
				else
				{
					m_msgQue.pushBack({ cur.fd, EventType::RESET });
				}
				break;
			}
			case EventType::ACTIVITY_TIMER:
			{
				closeInactive(cur.fd);
				break;
			}
			case EventType::RESET:
			{
				m_conns[cur.fd]->reset();
				break;
			}
			case EventType::STOP:
			{
				return;
			}
			}
		}
	}


	void acceptNew(int listen_fd)
	{
		while (true)
		{
			struct sockaddr_in clientAddr;
			socklen_t addrlen = sizeof(clientAddr);
			int clientFd = accept(listen_fd, (struct sockaddr *)&clientAddr, &addrlen);
			if (clientFd == -1)
			{
				if (EAGAIN != errno && EWOULDBLOCK != errno)
				{
					perror("threadPool::acceptNew() accept()");
				}
				break;
			}
			else
			{
				//char ip[INET_ADDRSTRLEN];
				//inet_ntop(AF_INET, &(clientAddr.sin_addr), ip, INET_ADDRSTRLEN);
				//std::cout << "\naccept; fd:" << clientFd << "; IP:" << ip << "; Port:" << ntohs(clientAddr.sin_port) << std::endl;

				if (clientFd >= m_maxConns)
				{
					std::cout << "threadPool::acceptNew() reach the limit of connections\n";
					close(clientFd);
					continue;
				}

				fcntl(clientFd, F_SETFL, fcntl(clientFd, F_GETFL, 0) | O_NONBLOCK);

				m_conns[clientFd]->set(clientAddr);

				struct epoll_event clientEvent;
				clientEvent.events = EPOLLIN | EPOLLET | EPOLLRDHUP | EPOLLONESHOT;
				clientEvent.data.fd = clientFd;
				if (epoll_ctl(m_epollFd, EPOLL_CTL_ADD, clientFd, &clientEvent) == -1)
				{
					perror("threadPool::acceptNew() epoll_ctl");
					m_conns[clientFd]->reset();
				}
			}
		}
	}


	void closeInactive(int timer_fd)
	{
		uint64_t expirations;
		while (true)
		{
			if (read(timer_fd, &expirations, sizeof(expirations)) == -1)
			{
				if (EAGAIN != errno && EWOULDBLOCK != errno)
				{
					perror("threadPool::closeInactive()");
				}
				break;
			}
		}
		m_sessions.eraseNonactive(m_timeout);
	}


private:
	Que &m_msgQue;
	Session &m_sessions;
	std::vector<std::unique_ptr<Connection>> &m_conns;
	std::vector<std::thread> m_pool;
	const int m_maxConns, m_timeout, m_epollFd;
};