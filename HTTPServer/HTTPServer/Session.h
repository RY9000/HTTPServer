#pragma once

#include "HTTPServer.h"


struct SessionData
{
	std::string userID;
	sockaddr_in addr;
	std::chrono::time_point<std::chrono::system_clock> lastActiveTime;

	SessionData()
	{}

	SessionData(const std::string &ID, const sockaddr_in &sAddr, std::chrono::time_point<std::chrono::system_clock> t):
		userID(ID), addr(sAddr), lastActiveTime(t)
	{}
};


class Session
{
public:
	Session(int numBuckets): m_numBuckets(numBuckets), m_sessionBuckets(m_numBuckets), m_mutexBuckets(m_numBuckets)
	{}


	// return sessionID
	std::string create(const std::string &userID, const sockaddr_in &addr)
	{
		std::string sessionID = generateUUID(addr);
		int index = getBucketIndex(sessionID);
		DEBUG_LOG("session::create() sessionID = " << sessionID << " index = " << index);

		{
			std::scoped_lock<std::mutex> lock(m_mutexBuckets[index]);
			m_sessionBuckets[index][sessionID] = { userID, addr, std::chrono::system_clock::now() };
		}
		return sessionID;
	}


	int erase(const std::string &sessionID)
	{
		int index = getBucketIndex(sessionID);
		DEBUG_LOG("session::erase() sessionID = " << sessionID << " index = " << index);

		{
			std::scoped_lock<std::mutex> lock(m_mutexBuckets[index]);
			m_sessionBuckets[index].erase(sessionID);
		}
		return 0;
	}


	// return userID. If sessionID doesn't exist or IP is not correct, return "".
	std::string checkSessionID(const std::string &sessionID, const sockaddr_in &addr)
	{
		int index = getBucketIndex(sessionID);
		std::string userID = "";

		{
			std::scoped_lock<std::mutex> lock(m_mutexBuckets[index]);
			auto it = m_sessionBuckets[index].find(sessionID);
			if (it != m_sessionBuckets[index].end())
			{
				if (addr.sin_addr.s_addr == it->second.addr.sin_addr.s_addr)
					userID = it->second.userID;
			}
		}
		DEBUG_LOG("session::checkSessionID() sessionID = " << sessionID << " index = " << index << " userID = " << userID);
		return userID;
	}


	// 0: success; -1: failed
	int updateActiveTime(const std::string &sessionID)
	{
		int index = getBucketIndex(sessionID);
		DEBUG_LOG("session::updateActiveTime() sessionID = " << sessionID << " index = " << index);

		{
			std::scoped_lock<std::mutex> lock(m_mutexBuckets[index]);
			auto it = m_sessionBuckets[index].find(sessionID);
			if (it == m_sessionBuckets[index].end())
				return -1;

			it->second.lastActiveTime = std::chrono::system_clock::now();
		}
		return 0;
	}


	int eraseNonactive(int timeout)
	{
		auto currentTime = std::chrono::system_clock::now();

		for (int i = 0; i < m_numBuckets; ++i)
		{
			std::scoped_lock<std::mutex> lock(m_mutexBuckets[i]);
			auto it = m_sessionBuckets[i].begin();
			while (it != m_sessionBuckets[i].end())
			{
				auto elapsedTime = std::chrono::duration_cast<std::chrono::seconds>(currentTime - it->second.lastActiveTime).count();
				if (elapsedTime > timeout)
				{
					DEBUG_LOG("session::eraseNonactive() sessionID = " << it->first);
					it = m_sessionBuckets[i].erase(it);
				}
				else
				{
					++it;
				}
			}
		}
		return 0;
	}


private:
	std::string generateUUID(const sockaddr_in &addr)
	{
		auto now = std::chrono::system_clock::now();
		auto timeSinceEpoch = now.time_since_epoch();
		auto seconds = std::chrono::duration_cast<std::chrono::seconds>(timeSinceEpoch).count();
		std::stringstream ss;
		ss << std::hex << std::setw(16) << std::setfill('0') << seconds;

		ss << std::setw(8) << std::setfill('0') << ntohl(addr.sin_addr.s_addr);

		std::random_device rd;
		std::mt19937_64 gen(rd());
		std::uniform_int_distribution<uint64_t> dis;
		ss << std::setw(16) << std::setfill('0') << dis(gen);
		return ss.str();
	}


	int getBucketIndex(const std::string &sessionId)
	{
		std::hash<std::string> hashFunction;
		size_t hashValue = hashFunction(sessionId);
		return hashValue % m_numBuckets;
	}


private:
	const int m_numBuckets;
	std::vector<std::unordered_map<std::string, SessionData>> m_sessionBuckets;
	std::vector<std::mutex> m_mutexBuckets;
};