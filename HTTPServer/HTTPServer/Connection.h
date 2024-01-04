#pragma once

#include "HTTPServer.h"
#include "Sql.h"
#include "Session.h"


enum class Method:int
{
	GET, POST, PUT, DELETE
};

enum class ParseState:int
{
	START, HEADER, BODY, DONE
};

enum class IOStatus:int
{
	DONE, NOTDONE, ERROR
};

enum class URLs:int
{
	EMPTY = 10,
	INDEX_HTML,
	DASHBOARD_HTML,
	SETTING_HTML,
	REGISTER_HTML,
	API_LOGIN,
	API_LOGOUT,
	API_USER,
	STYLES_CSS,
	SCRIPT_JS,
	FAVICON_ICO,
	VIDEOS
};
std::unordered_map<std::string, URLs> Url2Int
{
	{"",		URLs::EMPTY},
	{"index.html",	URLs::INDEX_HTML},
	{"dashboard.html",	URLs::DASHBOARD_HTML},
	{"styles.css",		URLs::STYLES_CSS},
	{"script.js",		URLs::SCRIPT_JS},
	{"setting.html",	URLs::SETTING_HTML},
	{"api/login",		URLs::API_LOGIN},
	{"api/user",		URLs::API_USER},
	{"api/logout",		URLs::API_LOGOUT},
	{"register.html",	URLs::REGISTER_HTML},
	{"favicon.ico",		URLs::FAVICON_ICO},
	{"video.mp4",		URLs::VIDEOS}
};

const auto g_endOfUrl2Int = Url2Int.end();

enum class RespStatus:int
{
	OK_200,
	CREATED_201,
	NO_CONTENT_204,
	BAD_REQUEST_400,
	UNAUTHORIZED_401,
	FORBIDDEN_403,
	NOT_FOUND_404,
	METHOD_NOT_ALLOWED_405,
	CONFLICT_409,
	LENGTH_REQUIRED_411,
	SERVICE_UNAVAILABLE_503,
};
std::unordered_map<RespStatus, std::string> RespStatus2Str
{
	{RespStatus::OK_200,	"200 OK"},
	{RespStatus::CREATED_201,		"201 Created"},
	{RespStatus::NO_CONTENT_204,	"204 No Content"},
	{RespStatus::BAD_REQUEST_400,	"400 Bad Request"},
	{RespStatus::UNAUTHORIZED_401,	"401 Unauthorized"},
	{RespStatus::FORBIDDEN_403,		"403 Forbidden"},
	{RespStatus::NOT_FOUND_404,		"404 Not Found"},
	{RespStatus::METHOD_NOT_ALLOWED_405,	"405 Method Not Allowed"},
	{RespStatus::CONFLICT_409,		"409 Conflict"},
	{RespStatus::LENGTH_REQUIRED_411,		"411 Length Required"},
	{RespStatus::SERVICE_UNAVAILABLE_503,	"503 Service Unavailable"}
};


class Connection
{
public:
	Connection(int requestSz, int respHSz, int respBSz, const char *root, const char *homeP, int homePSize,
		const char *css, int cssSize, const char *js, int jsSize, int epollFd, int clientFd, SqlPool &sqlPool, Session &ses):
		m_root(root), m_homePageAddr(homeP), m_homePageSize(homePSize), m_cssAddr(css), m_cssSize(cssSize),
		m_jsAddr(js), m_jsSize(jsSize), m_epollFd(epollFd), m_clientFd(clientFd), m_sqlPool(sqlPool), m_sessions(ses)
	{
		m_reqBufSize = requestSz;
		m_respHeadBufSize = respHSz;
		m_respBodyBufSize = respBSz;
		m_respHeadSize = m_respHeadBufSize;
		m_respBodySize = m_respBodyBufSize;
		m_request = (char *)malloc(m_reqBufSize);
		m_respHead = (char *)malloc(m_respHeadBufSize);
		m_respBody = (char *)malloc(m_respBodyBufSize);
		m_fileFd = -1;
		m_fileAddr = nullptr;
		clean();
	}


	~Connection()
	{
		free(m_request);
		free(m_respHead);
		free(m_respBody);
		clean();
	}


	void set(struct sockaddr_in &clientAddr)
	{
		m_clientAddr = clientAddr;
		DEBUG_LOG("connection::set() m_clientFd = " << m_clientFd);
	}


	void reset()
	{
		DEBUG_LOG("connection::reset() m_clientFd = " << m_clientFd);
		epoll_ctl(m_epollFd, EPOLL_CTL_DEL, m_clientFd, NULL);
		clean();
		close(m_clientFd);
	}


	void clean()
	{
		m_bytesRead = 0;
		m_bytesWritten = 0;
		m_bytesToWrite = 0;
		memset(m_respHead, '\0', m_respHeadSize);
		memset(m_respBody, '\0', m_respBodySize);
		m_respHeadSize = 0;
		m_respBodySize = 0;
		m_iov[0].iov_len = 0;
		m_iov[1].iov_len = 0;
		m_parseIndex = 0;
		m_reqContentLength = 0;
		m_URL.clear();
		m_userID.clear();
		m_sessionID.clear();
		m_parseState = ParseState::START;
		m_respStatus = RespStatus::OK_200;
		m_contentType = "text/html; charset=utf-8";
		if (m_fileAddr != nullptr)
		{
			munmap(m_fileAddr, m_sb.st_size);
			m_fileAddr = nullptr;
		}
		if (m_fileFd != -1)
		{
			close(m_fileFd);
			m_fileFd = -1;
		}
	}


	bool changeFd(int event)
	{
		DEBUG_LOG("connection::change_fd() event == IN " << (event & EPOLLIN));
		struct epoll_event clientEvent {};
		clientEvent.events = event | EPOLLET | EPOLLRDHUP | EPOLLONESHOT;
		clientEvent.data.fd = m_clientFd;
		if (epoll_ctl(m_epollFd, EPOLL_CTL_MOD, m_clientFd, &clientEvent) == -1)
		{
			perror("connection::changeFd()");
			return false;
		}
		return true;
	}


	IOStatus readReq()
	{
		DEBUG_LOG(" :) :) :) :) :)");
		int bytes = 0;
		while (true)
		{
			bytes = read(m_clientFd, m_request + m_bytesRead, m_reqBufSize - 1 - m_bytesRead);
			DEBUG_LOG("connection::readReq() m_clientFd = " << m_clientFd << " bytes = " << bytes);
			if (bytes == -1)
			{
				if (EAGAIN == errno || EWOULDBLOCK == errno)
				{
					break;
				}
				else
				{
					perror("connection::readReq() read()");
					return IOStatus::ERROR;
				}
			}
			else if (bytes == 0)
			{
				return IOStatus::ERROR;
			}

			m_bytesRead += bytes;
			m_request[m_bytesRead] = '\0';

			if (m_bytesRead >= m_reqBufSize - 1)
			{
				m_reqBufSize *= 2;
				m_request = (char *)realloc(m_request, m_reqBufSize);
			}
			DEBUG_LOG("connection::readReq() m_clientFd = " << m_clientFd << " m_bytesRead = " << m_bytesRead <<
				" m_reqBufSize = " << m_reqBufSize);
		}
		DEBUG_LOG("connection::readReq() m_request = \n" << m_request);
		return IOStatus::DONE;
	}


	IOStatus writeResp()
	{
		if (m_sessionID != "")
		{
			m_sessions.updateActiveTime(m_sessionID);
		}

		int bytes = 0;
		while (true)
		{
			bytes = writev(m_clientFd, m_iov, 2);
			DEBUG_LOG("connection::writeResp() m_clientFd = " << m_clientFd << " bytes = " << bytes);
			if (bytes < 0)
			{
				if (EAGAIN == errno || EWOULDBLOCK == errno)
				{
					break;
				}
				else
				{
					perror("connection::writeResp() writev()");
					return IOStatus::ERROR;
				}
			}

			m_bytesWritten += bytes;

			DEBUG_LOG("connection::writeResp() m_bytesWritten = " << m_bytesWritten << " m_bytesToWrite = " << m_bytesToWrite);

			if (m_bytesWritten >= m_bytesToWrite)
			{
				return IOStatus::DONE;
			}

			if (m_bytesWritten <= m_respHeadSize)
			{
				m_iov[0].iov_base = m_respHead + m_bytesWritten;
				m_iov[0].iov_len -= bytes;
			}
			else
			{
				m_iov[0].iov_len = 0;
				if (m_bytesWritten - bytes < m_respHeadSize)
					bytes = m_bytesWritten - m_respHeadSize;

				m_iov[1].iov_base = m_iov[1].iov_base + bytes;
				m_iov[1].iov_len -= bytes;
			}
			DEBUG_LOG("connection::writeResp() m_iov[0].iov_len = " << m_iov[0].iov_len << " m_iov[1].iov_len = " << m_iov[1].iov_len);
		}
		return IOStatus::NOTDONE;
	}


	IOStatus process()
	{
		if (!parseReq())
			return IOStatus::NOTDONE;
		generateResp();
		return IOStatus::DONE;
	}


private:
	// true: parse completed, including bad request.
	// false: so far no error, but request is not completed
	bool parseReq()
	{
		while (m_parseState != ParseState::DONE && m_respStatus == RespStatus::OK_200)
		{
			DEBUG_LOG("connection::parseReq() m_clientFd = " << m_clientFd << " m_parseState = " << (int)m_parseState <<
				" RespStatus = " << RespStatus2Str[m_respStatus]);
			switch (m_parseState)
			{
			case ParseState::START:
			{
				if (!parseStart())
					return false;
				m_parseState = ParseState::HEADER;
				break;
			}
			case ParseState::HEADER:
			{
				if (!parseHeader())
					return false;
				m_parseState = ParseState::BODY;
				break;
			}
			case ParseState::BODY:
			{
				if (!parseBody())
					return false;
				m_parseState = ParseState::DONE;
				break;
			}
			}
		}
		return true;
	}


	bool parseStart()
	{
		int lineEnd = findLineEnd();
		if (lineEnd == -1)
			return false;

		if (lineEnd >= 7 && checkWord(m_parseIndex, lineEnd, "get"))
		{
			m_method = Method::GET;
			m_parseIndex += 5;
		}
		else if (lineEnd >= 7 && checkWord(m_parseIndex, lineEnd, "put"))
		{
			m_method = Method::PUT;
			m_parseIndex += 5;
		}
		else if (lineEnd >= 8 && checkWord(m_parseIndex, lineEnd, "post"))
		{
			m_method = Method::POST;
			m_parseIndex += 6;
		}
		else if (lineEnd >= 10 && checkWord(m_parseIndex, lineEnd, "delete"))
		{
			m_method = Method::DELETE;
			m_parseIndex += 8;
		}
		else
		{
			m_respStatus = RespStatus::BAD_REQUEST_400;
			DEBUG_LOG("connection::parseStart() m_clientFd = " << m_clientFd << " RespStatus = " << RespStatus2Str[m_respStatus]);
			return true;
		}

		while (m_parseIndex < lineEnd)
		{
			if (m_request[m_parseIndex] == ' ')
				break;
			else
			{
				m_URL += m_request[m_parseIndex];
				++m_parseIndex;
			}
		}
		m_parseIndex = lineEnd + 1;

		DEBUG_LOG("connection::parseStart() m_clientFd = " << m_clientFd << " m_method = " << (int)m_method <<
			" RespStatus = " << RespStatus2Str[m_respStatus] << " m_URL = " << m_URL);
		return true;
	}


	bool parseHeader()
	{
		while (true)
		{
			int lineEnd = findLineEnd();
			if (lineEnd == -1)
				return false;

			if (m_request[m_parseIndex] == '\r')
			{
				m_parseIndex += 2;
				DEBUG_LOG("connection::parseHeader() m_clientFd = " << m_clientFd << " Empty line m_parseIndex = " << m_parseIndex);
				break;
			}

			if (lineEnd - m_parseIndex >= 18 && checkWord(m_parseIndex, lineEnd, "content-length: "))
			{
				m_parseIndex += 16;
				while (m_request[m_parseIndex] >= '0' && m_request[m_parseIndex] <= '9')
				{
					m_reqContentLength = m_reqContentLength * 10 + (m_request[m_parseIndex] - '0');
					++m_parseIndex;
				}
			}
			else if (lineEnd - m_parseIndex >= 10 && checkWord(m_parseIndex, lineEnd, "cookie: "))
			{
				m_parseIndex += 8;

				while (m_parseIndex < lineEnd)
				{
					if (checkWord(m_parseIndex, lineEnd, "session_ID="))
					{
						m_parseIndex += 11;
						while (m_request[m_parseIndex] != ';' && m_request[m_parseIndex] != '\r')
						{
							m_sessionID += m_request[m_parseIndex];
							++m_parseIndex;
						}
						m_userID = m_sessions.checkSessionID(m_sessionID, m_clientAddr);
						if (m_userID == "")
							m_sessionID.clear();
						else
						{
							if (m_sessions.updateActiveTime(m_sessionID) == -1)
							{
								m_sessionID.clear();
								m_userID.clear();
							}
						}
					}

					m_parseIndex += 2;
				}
			}

			m_parseIndex = lineEnd + 1;
		}
		DEBUG_LOG("connection::parseHeader() m_clientFd = " << m_clientFd << " m_sessionID = " << m_sessionID << " m_userID = " << m_userID <<
			" m_reqContentLength = " << m_reqContentLength);
		return true;
	}


	bool parseBody() // parse body and generate the body of the response
	{
		if (m_reqContentLength > 0 && (m_bytesRead - m_parseIndex) < m_reqContentLength)
			return false;

		switch (m_method)
		{
		case Method::GET:
		{
			processGET();
			break;
		}
		case Method::PUT:
		{
			processPUT();
			break;
		}
		case Method::POST:
		{
			processPOST();
			break;
		}
		case Method::DELETE:
		{
			break;
		}
		}
		return true;
	}


	void processGET()
	{
		DEBUG_LOG("connection::processGET() m_clientFd = " << m_clientFd << " m_URL = " << m_URL);

		auto it = Url2Int.find(m_URL);

		if (it == g_endOfUrl2Int)
		{
			if (m_URL.substr(0, 9) == "api/user/")
			{
				if (m_sessionID != "" && m_userID != "" && m_URL.substr(9, m_URL.length() - 9) == m_userID)
				{
					char queryStatement[200];
					snprintf(queryStatement, 200, "select users.username, log.date, log.IP "
						"from users join log on users.userID = log.userID where users.userID=\"%s\" order by log.date DESC",
						m_userID.data());
					MYSQL_RES *result = sqlQuery(queryStatement);
					if (result == NULL)
					{
						m_respStatus = RespStatus::SERVICE_UNAVAILABLE_503;
					}
					else
					{
						int totalRows = mysql_num_rows(result);
						MYSQL_ROW row;
						addLine(&m_respBody, &m_respBodySize, &m_respBodyBufSize, "[\r\n");
						while (row = mysql_fetch_row(result))
						{
							addLine(&m_respBody, &m_respBodySize, &m_respBodyBufSize, "{\"name\": \"%s\", \"date\": \"%s\", \"IP\": \"%s\"}",
								row[0], row[1], row[2]);
							--totalRows;
							if (totalRows > 0)
							{
								addLine(&m_respBody, &m_respBodySize, &m_respBodyBufSize, ",\r\n");
							}
						}
						addLine(&m_respBody, &m_respBodySize, &m_respBodyBufSize, "\r\n]");
						mysql_free_result(result);
						m_iov[1].iov_base = m_respBody;
						m_iov[1].iov_len = m_respBodySize;
						m_contentType = "application/json";
					}
				}
				else
				{
					m_respStatus = RespStatus::UNAUTHORIZED_401;
				}
			}
			else
			{
				m_respStatus = RespStatus::BAD_REQUEST_400;
			}
		}
		else
		{
			switch (it->second)
			{
			case URLs::EMPTY:
			case URLs::INDEX_HTML:
			{
				if (m_sessionID != "")
				{
					if (false == openFile("dashboard.html"))
					{
						m_respStatus = RespStatus::NOT_FOUND_404;
					}
				}
				else
				{
					m_iov[1].iov_base = (char *)m_homePageAddr;
					m_iov[1].iov_len = m_homePageSize;
				}
				break;
			}
			case URLs::SETTING_HTML:
			case URLs::DASHBOARD_HTML:
			{
				if (m_sessionID != "")
				{
					if (false == openFile(m_URL.data()))
					{
						m_respStatus = RespStatus::NOT_FOUND_404;
					}
				}
				else
				{
					m_iov[1].iov_base = (char *)m_homePageAddr;
					m_iov[1].iov_len = m_homePageSize;
				}
				break;
			}
			case URLs::STYLES_CSS:
			{
				m_iov[1].iov_base = (char *)m_cssAddr;
				m_iov[1].iov_len = m_cssSize;
				m_contentType = "text/css; charset=utf-8";
				break;
			}
			case URLs::SCRIPT_JS:
			{
				m_iov[1].iov_base = (char *)m_jsAddr;
				m_iov[1].iov_len = m_jsSize;
				m_contentType = "application/javascript; charset=utf-8";
				break;
			}
			default: // register.html, favicon.ico, video.mp4
			{
				if (false == openFile(m_URL.data()))
				{
					m_respStatus = RespStatus::NOT_FOUND_404;
					break;
				}
				if (m_URL == "favicon.ico")
					m_contentType = "image/x-icon";
				else if (m_URL == "video.mp4")
					m_contentType = "video/mp4";
				break;
			}
			}
		}
		DEBUG_LOG("connection::processGET() m_clientFd = " << m_clientFd << " RespStatus = " << RespStatus2Str[m_respStatus] <<
			" m_contentType = " << m_contentType << " m_iov[1].iov_len = " << m_iov[1].iov_len << " m_respBodySize = " << m_respBodySize <<
			" m_respBodyBufSize = " << m_respBodyBufSize << " m_respBody = \n" << m_respBody);
	}


	void processPUT() // JSON request body: {"old_pwd":"123","password":"asdf"}
	{
		DEBUG_LOG("connection::processPUT() m_clientFd = " << m_clientFd << " m_URL = " << m_URL);
		std::string old_pwd, pwd;
		m_parseIndex += 2;
		while (m_parseIndex < m_bytesRead)
		{
			if (m_bytesRead - m_parseIndex > 8 && checkWord(m_parseIndex, m_parseIndex + 8, "password"))
			{
				m_parseIndex += 11;
				while (m_parseIndex < m_bytesRead && m_request[m_parseIndex] != '"')
				{
					pwd += m_request[m_parseIndex];
					++m_parseIndex;
				}
				m_parseIndex += 3;
			}
			else if (m_bytesRead - m_parseIndex > 7 && checkWord(m_parseIndex, m_parseIndex + 7, "old_pwd"))
			{
				m_parseIndex += 10;
				while (m_parseIndex < m_bytesRead && m_request[m_parseIndex] != '"')
				{
					old_pwd += m_request[m_parseIndex];
					++m_parseIndex;
				}
				m_parseIndex += 3;
			}
			else
			{
				m_respStatus = RespStatus::BAD_REQUEST_400;
				return;
			}
		}
		DEBUG_LOG("connection::processPUT() m_clientFd = " << m_clientFd << " old_pwd = " << old_pwd << " pwd = " << pwd);

		if (m_URL.substr(0, 9) == "api/user/" && checkUserInput(old_pwd) && checkUserInput(pwd))
		{
			if (m_sessionID != "" && m_userID != "" && m_URL.substr(9, m_URL.length() - 9) == m_userID)
			{
				std::string username = "";
				SqlConn sqlCon(m_sqlPool);
				if (0 == sqlCon.checkUser(username, old_pwd, m_userID))
				{

					if (sqlCon.updatePassword(m_userID, pwd))
					{
						m_respStatus = RespStatus::OK_200;
						m_contentType = "application/json";
					}
					else
					{
						m_respStatus = RespStatus::SERVICE_UNAVAILABLE_503;
					}
				}
				else
				{
					m_respStatus = RespStatus::FORBIDDEN_403;
				}
			}
			else
			{
				m_respStatus = RespStatus::UNAUTHORIZED_401;
			}
		}
		else
		{
			m_respStatus = RespStatus::BAD_REQUEST_400;
		}
	}


	void processPOST() // JSON request body: {"username":"123","password":"asdf"}
	{
		DEBUG_LOG("connection::processPOST() m_clientFd = " << m_clientFd << " m_URL = " << m_URL);
		std::string username, pwd;
		m_parseIndex += 2;
		while (m_parseIndex < m_bytesRead)
		{
			if (m_bytesRead - m_parseIndex > 8 && checkWord(m_parseIndex, m_parseIndex + 8, "username"))
			{
				m_parseIndex += 11;
				while (m_parseIndex < m_bytesRead && m_request[m_parseIndex] != '"')
				{
					username += m_request[m_parseIndex];
					++m_parseIndex;
				}
				m_parseIndex += 3;
			}
			else if (m_bytesRead - m_parseIndex > 8 && checkWord(m_parseIndex, m_parseIndex + 8, "password"))
			{
				m_parseIndex += 11;
				while (m_parseIndex < m_bytesRead && m_request[m_parseIndex] != '"')
				{
					pwd += m_request[m_parseIndex];
					++m_parseIndex;
				}
				m_parseIndex += 3;
			}
			else
			{
				m_respStatus = RespStatus::BAD_REQUEST_400;
				return;
			}
		}
		DEBUG_LOG("connection::processPOST() m_clientFd = " << m_clientFd << " username = " << username << " pwd = " << pwd);

		switch (Url2Int[m_URL])
		{
		case URLs::API_LOGIN:
		{
			if (checkUserInput(username) && checkUserInput(pwd))
			{
				if (m_sqlPool.checkUsername(username))
				{
					SqlConn sqlCon(m_sqlPool);
					if (0 == sqlCon.checkUser(username, pwd, m_userID))
					{
						m_respStatus = RespStatus::OK_200;
						m_sessionID = m_sessions.create(m_userID, m_clientAddr);

						char ip[INET_ADDRSTRLEN];
						inet_ntop(AF_INET, &(m_clientAddr.sin_addr), ip, INET_ADDRSTRLEN);
						char queryStatement[200];
						snprintf(queryStatement, 200, "insert into log (userID, date, IP) values (\"%s\", \"%s\", \"%s\")",
							m_userID.data(), getLocalDate().data(), ip);
						MYSQL_RES *result = sqlQuery(queryStatement);
						mysql_free_result(result);
					}
					else
					{
						m_respStatus = RespStatus::UNAUTHORIZED_401;
					}
				}
				else
				{
					m_respStatus = RespStatus::UNAUTHORIZED_401;
				}
			}
			else
			{
				m_respStatus = RespStatus::BAD_REQUEST_400;
			}
			break;
		}
		case URLs::API_USER:
		{
			if (checkUserInput(username) && checkUserInput(pwd))
			{
				if (!m_sqlPool.checkUsername(username))
				{
					SqlConn sqlCon(m_sqlPool);
					int res = sqlCon.insertUser(username, pwd);
					if (0 == res)
					{
						m_respStatus = RespStatus::CREATED_201;
					}
					else if (1 == res)
					{
						m_respStatus = RespStatus::CONFLICT_409;
					}
					else
					{
						m_respStatus = RespStatus::SERVICE_UNAVAILABLE_503;
					}
				}
				else
				{
					m_respStatus = RespStatus::CONFLICT_409;
				}
			}
			else
			{
				m_respStatus = RespStatus::BAD_REQUEST_400;
			}
			break;
		}
		case URLs::API_LOGOUT:
		{
			if (m_sessionID != "")
				m_sessions.erase(m_sessionID);
			m_respStatus = RespStatus::OK_200;
			break;
		}
		default:
		{
			m_respStatus = RespStatus::BAD_REQUEST_400;
			break;
		}
		}
	}


	// mainly generate the head of the response
	bool generateResp()
	{
		addLine(&m_respHead, &m_respHeadSize, &m_respHeadBufSize, "HTTP/1.1 %s\r\n", RespStatus2Str[m_respStatus].data());

		addLine(&m_respHead, &m_respHeadSize, &m_respHeadBufSize, "Content-Type: %s\r\n", m_contentType.data());

		addLine(&m_respHead, &m_respHeadSize, &m_respHeadBufSize, "Content-Length: %d\r\n", m_iov[1].iov_len);

		if (m_method == Method::POST && m_URL == "api/login" && m_respStatus == RespStatus::OK_200)
		{
			addLine(&m_respHead, &m_respHeadSize, &m_respHeadBufSize, "Set-Cookie: user_ID=%s; path=/\r\n", m_userID.data());
			addLine(&m_respHead, &m_respHeadSize, &m_respHeadBufSize, "Set-Cookie: session_ID=%s; path=/\r\n", m_sessionID.data());
		}
		else if (m_method == Method::POST && m_URL == "api/logout")
		{
			addLine(&m_respHead, &m_respHeadSize, &m_respHeadBufSize, "Set-Cookie: user_ID=; "
				"expires=Thu, 01 Jan 1970 00:00:00 GMT; path=/\r\n");
			addLine(&m_respHead, &m_respHeadSize, &m_respHeadBufSize, "Set-Cookie: session_ID=; "
				"expires=Thu, 01 Jan 1970 00:00:00 GMT; path=/\r\n");
		}

		addLine(&m_respHead, &m_respHeadSize, &m_respHeadBufSize, "Cache-Control: no-cache, no-store, must-revalidate\r\n");

		addLine(&m_respHead, &m_respHeadSize, &m_respHeadBufSize, "Date: %s\r\n", getGMTDate().data());

		addLine(&m_respHead, &m_respHeadSize, &m_respHeadBufSize, "\r\n");

		m_iov[0].iov_base = m_respHead;
		m_iov[0].iov_len = m_respHeadSize;
		m_bytesToWrite = m_iov[0].iov_len + m_iov[1].iov_len;

		DEBUG_LOG("connection::generateResp() m_clientFd = " << m_clientFd << " m_respHeadSize = " << m_respHeadSize <<
			" m_respHeadBufSize = " << m_respHeadBufSize << " m_bytesToWrite = " << m_bytesToWrite << " m_respHead = \n" << m_respHead);
		return true;
	}


	std::string getGMTDate()
	{
		std::ostringstream oss;
		std::time_t t = time(nullptr);
		std::tm *tm = std::gmtime(&t);
		oss << std::put_time(tm, "%a, %d %b %Y %H:%M:%S GMT");
		return oss.str();
	}

	std::string getLocalDate()
	{
		std::ostringstream oss;
		std::time_t t = time(nullptr);
		std::tm *tm = std::localtime(&t);
		oss << std::put_time(tm, "%F %T %Z");
		return oss.str();
	}


	void addLine(char **dest, int *size, int *bufSize, const char *fmt, ...)
	{
		va_list ap{};
		va_start(ap, fmt);
		int num = vsnprintf(*dest + *size, *bufSize - *size, fmt, ap);
		if (num + *size > *bufSize - 1)
		{
			*bufSize = *bufSize * 2 > *size + num + 1 ? *bufSize * 2 : *size + num + 1;
			*dest = (char *)realloc(*dest, *bufSize);
			va_start(ap, fmt);
			num = vsnprintf(*dest + *size, *bufSize - *size, fmt, ap);
		}
		*size += num;
		(*dest)[*size] = '\0';
		va_end(ap);
	}


	int findLineEnd()
	{
		for (int i = m_parseIndex; i < m_bytesRead; ++i)
		{
			if (m_request[i] == '\n')
			{
				return i;
			}
		}
		return -1;
	}


	bool checkWord(int start, int end, char *word)
	{
		int i = start;
		while (i <= end && m_request[i] == ' ')
			++i;

		for (; i <= end; ++i, ++word)
		{
			if (*word == '\0')
				return true;

			if (m_request[i] != *word && m_request[i] + 32 != *word)
				return false;
		}
		return false;
	}


	bool openFile(char *url)
	{
		std::string path = m_root;
		path += url;
		m_fileFd = open(path.data(), O_RDONLY);
		if (-1 == m_fileFd)
		{
			perror("connection::openFile() open()");
			std::cout << "connection::openFile() path = " << path << std::endl;
			return false;
		}
		fstat(m_fileFd, &m_sb);
		m_fileAddr = (char *)mmap(0, m_sb.st_size, PROT_READ, MAP_PRIVATE, m_fileFd, 0);
		if (m_fileAddr == MAP_FAILED)
		{
			perror("connection::openFile() mmap()");
			std::cout << "connection::openFile() path = " << path << std::endl;
			close(m_fileFd);
			m_fileFd = -1;
			m_fileAddr = nullptr;
			return false;
		}
		m_iov[1].iov_base = m_fileAddr;
		m_iov[1].iov_len = m_sb.st_size;

		DEBUG_LOG("connection::openFile() m_clientFd = " << m_clientFd << " url = " << url << " m_iov[1].iov_len = " << m_iov[1].iov_len);
		return true;
	}


	bool checkUserInput(std::string &input)
	{
		if (input.size() == 0 || input.size() > 20)
			return false;
		for (auto c : input)
		{
			if (!(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') && !(c >= '0' && c <= '9') &&
				c != '-' && c != '_' && c != '.' && c != '@' && c != '=')
				return false;
		}
		return true;
	}


	MYSQL_RES *sqlQuery(char *statement)
	{
		SqlConn sql(m_sqlPool);
		return sql.query(statement);
	}


private:
	const int m_clientFd, m_epollFd;
	struct sockaddr_in m_clientAddr;

	char *m_request, *m_respHead, *m_respBody;
	int m_reqBufSize, m_respHeadBufSize, m_respBodyBufSize;
	int m_bytesRead, m_bytesWritten, m_bytesToWrite, m_respHeadSize, m_respBodySize;
	struct iovec m_iov[2];

	const char *const m_root, *const m_homePageAddr, *const m_cssAddr, *const m_jsAddr;
	const int m_homePageSize, m_cssSize, m_jsSize;
	int m_fileFd;
	char *m_fileAddr;
	struct stat m_sb;

	SqlPool &m_sqlPool;
	Session &m_sessions;
	std::string m_userID, m_sessionID;

	int m_parseIndex, m_reqContentLength;
	ParseState m_parseState;
	Method m_method;
	RespStatus m_respStatus;
	std::string m_URL, m_contentType;
};
