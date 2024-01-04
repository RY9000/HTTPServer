// HTTPServer.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include <iostream>
#include <deque>
#include <vector>
#include <list>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <iomanip>
#include <cstring>
#include <random>


#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/time.h>
#include <sys/timerfd.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>


#include <mysql/mysql.h>


//#define DEBUG
#ifdef DEBUG
std::mutex muextOut;
#define DEBUG_LOG(S) { std::lock_guard<std::mutex> lock(muextOut); std::cout << S << std::endl << std::endl; }
#else
#define DEBUG_LOG(S)
#endif
