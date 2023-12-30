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


struct message
{
	int fd;
	EventType type;
};


class que
{
public:
	que(int size): m_size(size)
	{
		m_counter = 0;
		m_head = 0;
		m_end = 0;
		m_msgQue = (message *)malloc(size * sizeof(message));
	}


	que(const que &) = delete;


	~que()
	{
		free(m_msgQue);
	}


	message pop_front()
	{
		std::unique_lock<std::mutex> lock(m_mutex);
		m_cv.wait(lock, [this] { return m_counter > 0; });

		--m_counter;
		message front = m_msgQue[m_head];
		m_head = m_head == m_size - 1 ? 0 : m_head + 1;
		lock.unlock();
		m_cv.notify_one();
		return front;
	}


	bool push_back(message item)
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
	message *m_msgQue;
	std::mutex m_mutex;
	std::condition_variable m_cv;
};