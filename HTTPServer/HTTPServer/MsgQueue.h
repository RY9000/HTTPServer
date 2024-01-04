#pragma once

#include "HTTPServer.h"


enum class EventType: int
{
	ACCEPT,
	IN,
	OUT,
	ACTIVITY_TIMER,
	RESET,
	STOP
};


struct Message
{
	int fd;
	EventType type;
};


class Que
{
public:
	Que(int size): m_size(size)
	{
		m_counter = 0;
		m_head = 0;
		m_end = 0;
		m_msgQue = (Message *)malloc(size * sizeof(Message));
	}


	Que(const Que &) = delete;


	~Que()
	{
		free(m_msgQue);
	}


	Message popFront()
	{
		std::unique_lock<std::mutex> lock(m_mutex);
		m_cv.wait(lock, [this] { return m_counter > 0; });

		--m_counter;
		Message front = m_msgQue[m_head];
		m_head = m_head == m_size - 1 ? 0 : m_head + 1;
		lock.unlock();
		m_cv.notify_one();
		return front;
	}


	bool pushBack(Message item)
	{
		std::unique_lock<std::mutex> lock(m_mutex);
		m_cv.wait(lock, [this] { return m_counter < m_size; });

		++m_counter;
		m_msgQue[m_end] = item;
		m_end = m_end == m_size - 1 ? 0 : m_end + 1;
		lock.unlock();
		m_cv.notify_one();
		return true;
	}


private:
	const int m_size;
	int m_counter, m_head, m_end;
	Message *m_msgQue;
	std::mutex m_mutex;
	std::condition_variable m_cv;
};