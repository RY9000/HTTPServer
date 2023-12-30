#pragma once

#include "HTTPServer.h"


struct sqlNode
{
	MYSQL *mysql;
	sqlNode *next;
};


class sqlPool
{
public:
	sqlPool(int num)
	{
		m_numSqlConn = num;
		m_count = m_numSqlConn;
		m_head = nullptr;
		m_end = nullptr;
	}


	~sqlPool()
	{
		if (m_head == nullptr)
			return;

		m_end = m_head;
		m_head = m_head->next;
		m_end->next = nullptr;
		while (m_head != nullptr)
		{
			mysql_close(m_head->mysql);
			m_end = m_head;
			m_head = m_head->next;
			free(m_end);
		}
	}


	bool initSqlPool(const char *host, const char *user, const char *passwd, const char *db, const int port)
	{
		for (int i = 0; i < m_numSqlConn; ++i)
		{
			sqlNode *node = (sqlNode *)malloc(sizeof(sqlNode));
			node->mysql = mysql_init(NULL);
			if (NULL == mysql_real_connect(node->mysql, host, user, passwd, db, port, NULL, 0))
			{
				std::cout << "sqlPool::initSqlPool() " << mysql_error(node->mysql) << std::endl;
				return false;
			}
			node->next = m_head;
			m_head = node;
			m_end = m_end == nullptr ? m_head : m_end;
		}
		m_end->next = m_head;
		m_end = m_head;
		if (!getAllUsers())
			return false;
		return true;
	}


	// true: username already exists
	bool checkUsername(std::string &username)
	{
		std::scoped_lock<std::mutex> lock(m_mutexAllUsers);
		if (m_allUsers.count(username) == 0)
			return false;
		else
			return true;
	}


private:
	MYSQL *getSqlConn()
	{
		std::unique_lock<std::mutex> lock(m_mutexSQL);
		m_cv.wait(lock, [this] { return m_count > 0; });
		--m_count;
		MYSQL *conn = m_head->mysql;
		m_head = m_head->next;
		lock.unlock();
		m_cv.notify_one();
		return conn;
	}


	void releaseSqlConn(MYSQL *conn)
	{
		std::unique_lock<std::mutex> lock(m_mutexSQL);
		m_cv.wait(lock, [this] { return m_count < m_numSqlConn; });
		++m_count;
		m_end->mysql = conn;
		m_end = m_end->next;
		lock.unlock();
		m_cv.notify_one();
	}


	// true: success; false: username already exists
	bool insertUser(char *username)
	{
		std::scoped_lock<std::mutex> lock(m_mutexAllUsers);
		if (m_allUsers.count(username) != 0)
			return false;
		m_allUsers.insert(username);
		return true;
	}


	bool getAllUsers()
	{
		MYSQL *conn = m_head->mysql;
		if (mysql_query(conn, "SELECT * FROM users"))
		{
			std::cout << "sqlPool: getAllUsers() " << mysql_error(conn) << std::endl;
			return false;
		}

		MYSQL_RES *result = mysql_store_result(conn);
		MYSQL_ROW row;
		while (row = mysql_fetch_row(result))
		{
			m_allUsers.insert(row[1]);
		}
		mysql_free_result(result);
		return true;
	}


private:
	sqlNode *m_head, *m_end;
	int m_numSqlConn, m_count;
	std::unordered_set<std::string> m_allUsers;
	std::mutex m_mutexSQL, m_mutexAllUsers;
	std::condition_variable m_cv;
	friend class sqlConn;
};



class sqlConn
{
public:
	sqlConn(sqlPool &pool): m_pool(pool)
	{
		m_sqlConn = m_pool.getSqlConn();
	}


	~sqlConn()
	{
		m_pool.releaseSqlConn(m_sqlConn);
	}


	// 0: success; 1: username already exists; 2: sql error
	int insertUser(std::string &username, std::string &password)
	{
		if (!m_pool.insertUser(username.data()))
			return 1;

		char query[200];
		snprintf(query, 200, "insert into users (username, password) values (\"%s\", \"%s\")", username.data(), password.data());
		if (mysql_query(m_sqlConn, query))
		{
			std::cout << "sqlConn::insertUser() " << mysql_error(m_sqlConn) << std::endl;
			return 2;
		}
		return 0;
	}


	// Check password based on username or userID
	// if username is not "", write userID
	// if username is "" but userID is not "", write username
	// 0: success; 1: sql error; 2: username or userID doesn't exist; 3: password not correct; 4: parameters error
	int checkUser(std::string &username, std::string &password, std::string &userID)
	{
		if (username == "" && userID == "")
			return 4;

		char query[200];
		if (username != "")
			snprintf(query, 200, "select * from users where username=\"%s\"", username.data());
		else
			snprintf(query, 200, "select * from users where userID=\"%s\"", userID.data());

		if (mysql_query(m_sqlConn, query))
		{
			std::cout << "sqlConn::checkUser() " << mysql_error(m_sqlConn) << std::endl;
			return 1;
		}
		MYSQL_RES *result = mysql_store_result(m_sqlConn);
		MYSQL_ROW row;
		if (row = mysql_fetch_row(result))
		{
			if (row[2] == password)
			{
				if (username != "")
					userID = row[0];
				else
					username = row[1];

				mysql_free_result(result);
				return 0;
			}
			else
			{
				return 3;
			}
		}
		mysql_free_result(result);
		return 2;
	}


	bool updatePassword(std::string &userID, std::string &password)
	{
		char query[200];
		snprintf(query, 200, "update users set password=\"%s\" where userID=\"%s\"", password.data(), userID.data());
		if (mysql_query(m_sqlConn, query))
		{
			std::cout << "sqlConn::updatePassword() " << mysql_error(m_sqlConn) << std::endl;
			return false;
		}
		return true;
	}


	MYSQL_RES *query(char *statement)
	{
		DEBUG_LOG("sqlConn::query() statement = " << statement);
		if (mysql_query(m_sqlConn, statement))
		{
			std::cout << "sqlConn::query() " << mysql_error(m_sqlConn) << std::endl;
			return NULL;
		}
		return mysql_store_result(m_sqlConn);
	}


private:
	MYSQL *m_sqlConn;
	sqlPool &m_pool;
};