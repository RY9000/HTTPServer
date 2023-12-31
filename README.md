# HTTPServer

A simple HTTP server to check users' recent sign-in activity.

## Environment
1. WSL2 Ubuntu
2. MySQL
3. Visual Studio

## Demo
https://github.com/RY9000/HTTPServer/assets/92776817/096a59f5-6f8d-423b-bdab-7062b6c99edd

## Test
Running the server and the test tool on the same laptop with the number of threads in thread pool being 4.

### Access the homepage.
<img width="653" alt="2023-12-28_143006" src="https://github.com/RY9000/HTTPServer/assets/92776817/362e7686-be0f-47c6-ba8a-58aaf916c2ce">

### Access large files.
The video file is not provided. Put any mp4 file in the folder "root" and name it "video.mp4" for test.

<img width="653" alt="2023-12-28_160103" src="https://github.com/RY9000/HTTPServer/assets/92776817/fb8fa3e5-e557-4be0-b36d-5b5c55a4a223">

## Description

### Modules
![image](https://github.com/RY9000/HTTPServer/assets/92776817/e1dbe778-990a-4623-8396-6725825d31de)

### epoll
epoll is employed as a edge-triggered interface with nonblocking sockets.

![image](https://github.com/RY9000/HTTPServer/assets/92776817/5758e8f0-9995-4add-8366-a6e1265f4acf)

### Sessions
A new session will be created for each successful sign-in. If a user signs in from different IPs at about the same time, multiple sessions will be created for each IP. They are independent to each other. Sessions also track the last active time. Those that have been inactive for longer (2 minutes longer at most) than a specified timeout will be removed.

![image](https://github.com/RY9000/HTTPServer/assets/92776817/631a8ac0-60ab-49f9-8d37-822e6279b96d)

### Tables in database
There are two tables in the database named "HTTPServer" described below.

<img width="653" alt="2023-12-28_151707" src="https://github.com/RY9000/HTTPServer/assets/92776817/31beae74-6cb5-42c1-bdfa-0a0dbc166a68">

### Restful APIs
1. Login: `POST /api/login` Request Payload in JSON: `{"username":"abc","password":"123"}`
2. Registration: `POST /api/user` Request Payload in JSON: `{"username":"abc","password":"123"}`
3. Get user's data: `GET /api/user/userID`
4. Update user's password: `PUT /api/user/userID` Request Payload in JSON: `{"old_pwd":"123","password":"456"}`

![APIs](https://github.com/RY9000/HTTPServer/assets/92776817/aea6bd1c-b6ea-483a-bfef-48ce46c6f23c)
