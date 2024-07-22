#include "microws.h"

#define MICROWS_INVALID_CONNECTION ((uint32_t)0xffffffff)
#define MICROWS_FLAG_FLUSH 0x1

#define MWS_ASSERT(a)                                                                                                                                                                                  \
	do                                                                                                                                                                                                 \
	{                                                                                                                                                                                                  \
		if(!(a))                                                                                                                                                                                       \
		{                                                                                                                                                                                              \
			MP_BREAK();                                                                                                                                                                                \
		}                                                                                                                                                                                              \
	} while(0)

#ifdef _WIN32
#include <basetsd.h>
typedef UINT_PTR MWSSocket;
#else
typedef int MWSSocket;
#endif

#ifdef _WIN32
#define MWS_INVALID_SOCKET(f) (f == INVALID_SOCKET)
#else
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#define MWS_INVALID_SOCKET(f) (f < 0)
#endif

typedef void* (*MicroProfileThreadFunc)(void*);

#ifndef _WIN32
typedef pthread_t MicroProfileThread;
#elif defined(_WIN32)
typedef HANDLE MicroProfileThread;
#else
#include <thread>
typedef std::thread* MicroProfileThread;
#endif

void MicroProfileThreadStart(MicroProfileThread* pThread, MicroProfileThreadFunc Func);
void MicroProfileThreadJoin(MicroProfileThread* pThread);
bool MicroWSSendRaw(uint32_t ConnectionId, uint8_t* Data, uint32_t Size, uint32_t Flags = 0);

struct MicroWSConnection
{
	bool	 IsRunning = false;
	uint16_t nWebServerPort;
	uint32_t SendPut;
	uint32_t SendGet;
	uint8_t	 SendBuffer[MICROWS_SEND_MEMORY_PER_CONNECTION];

	uint32_t RecvPut;
	uint32_t RecvGet;
	uint8_t	 RecvBuffer[MICROWS_RECV_MEMORY_PER_CONNECTION];

	uint32_t Openening;
	uint32_t Open;
	uint32_t Closed;

	MWSConnection Socket = INVALID_SOCKET;
};
struct MicroWSState
{
	MpSocket ListenerSocket;

	uint32_t		  LastConnection = 0;
	MicroWSConnection Connections[MICROWS_MAX_CONNECTIONS];
};
static MicroWSState S;

bool MicroWSWebServerStart();

void MicroWSInit(uint16_t ListenPort)
{
	MWS_ASSERT(!S.IsRunning);
	S.nWebServerPort = ListenPort;
	if(MicroWSWebServerStart())
	{
		S.IsRunning = true;
	}
#if MICROPROFILE_WEBSERVER
	if(MICROPROFILE_FLIP_FLAG_START_WEBSERVER == (MICROPROFILE_FLIP_FLAG_START_WEBSERVER & FlipFlag) && S.nWebServerDataSent == (uint64_t)-1)
	{
		MicroProfileWebServerStart();
		S.nWebServerDataSent = 0;
	}
#endif
}
static void MicroWSDrain()
{
	uint32_t FailCount = 0;
	for(uint32_t i = 0; i < MICROWS_MAX_CONNECTIONS; ++i)
	{
		MicroWSConnection& C		 = S.Connections[i];
		bool			   IsOpen	 = MicroWSOpen(i);
		bool			   IsOpening = MicroWSOpening(i);
		if(IsOpen || IsOpening)
		{
			// read everything possible

			// write everything possible.
		}
	}
}
static uint32_t MicroWSSendRaw(uint32_t ConnectionId, uint8_t* Data, uint32_t Size, uint32_t Flags)
{
	uint32_t FailCount = 0;
	for(uint32_t i = 0; i < MICROWS_MAX_CONNECTIONS; ++i)
	{
		MicroWSConnection& C = S.Connections[i];
		if((C.Open == ConnectionId && C.Closed != ConnectionId) || (ConnectionId == MICROWS_INVALID_CONNECTION && S.MicroWSOpen(i)))
		{
			uint32_t Put = C.SendPut;
			uint32_t Get = C.SendGet;
			int32_t	 Bytes;
			if(Put < Get)
			{
				Bytes = Put - Get - 1;
			}
			else
			{
				Bytes = (MICROWS_SEND_MEMORY_PER_CONNECTION - Put) + Get - 1;
			}
			if(Bytes < Size)
			{
				FailCount++;
				continue;
			}
			if(Put < Get)
			{
				memcpy(&C.SendBuffer[Put], Data, Size);
				C.SendPut = Put + Size;
			}
			else
			{
				uint32_t Top = (MICROWS_SEND_MEMORY_PER_CONNECTION - Put);
				if(Top < Size)
				{
					uint32_t Bottom = Size - Top;
					memcpy(&C.SendBuffer[Put], Data, Top);
					memcpy(&C.SendBuffer[0], Data + Top, Bottom);
					C.SendPut = Bottom;
					MWS_ASSERT(C.SendPut < C.SendGet);
				}
				else
				{
					memcpy(&C.SendBuffer[Put], Data, Size);
					Put += Size;
					C.SendPut = Put == MICROWS_SEND_MEMORY_PER_CONNECTION ? 0 : Put;
				}
			}
		}
	}
	if(Flags & MICROWS_FLAG_FLUSH)
		MicroWSDrain(ConnectionId);
}

static bool MicroWSTryAccept(uint32_t ConnectionId)
{
	MicroWSConnection& C	  = S.Connections[ConnectionId % MICROWS_MAX_CONNECTIONS];
	MWSConnection	   Socket = C.Socket;
	MWS_ASSERT(C.Opening == ConnectionId);
	char Req[8192];
	// todo: do this into the ring buffer properly..
	int nReceived = recv(Connection, Req, sizeof(Req) - 1, 0);
	if(nReceived > 0)
	{
		Req[nReceived] = '\0';
		uprintf("req received\n%s", Req);
#if MICROPROFILE_MINIZ
		// Expires: Tue, 01 Jan 2199 16:00:00 GMT\r\n
#define MICROPROFILE_HTML_HEADER "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\nContent-Encoding: deflate\r\n\r\n"
#else
#define MICROPROFILE_HTML_HEADER "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\n\r\n"
#endif
		char* pHttp = strstr(Req, "HTTP/");

		char* pGet			= strstr(Req, "GET /");
		char* pHost			= strstr(Req, "Host: ");
		char* pWebSocketKey = strstr(Req, "Sec-WebSocket-Key: ");
		auto  Terminate		= [](char* pString)
		{
			char* pEnd = pString;
			while(*pEnd != '\0')
			{
				if(*pEnd == '\r' || *pEnd == '\n' || *pEnd == ' ')
				{
					*pEnd = '\0';
					return;
				}
				pEnd++;
			}
		};

		if(pWebSocketKey)
		{
			if(S.nNumWebSockets) // only allow 1
			{
				return false;
			}
			pWebSocketKey += sizeof("Sec-WebSocket-Key: ") - 1;
			Terminate(pWebSocketKey);

			const char* pGUID	   = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
			const char* pHandShake = "HTTP/1.1 101 Switching Protocols\r\n"
									 "Upgrade: websocket\r\n"
									 "Connection: Upgrade\r\n"
									 "Sec-WebSocket-Accept: ";

			char EncodeBuffer[512];
			int	 nLen = stbsp_snprintf(EncodeBuffer, sizeof(EncodeBuffer) - 1, "%s%s", pWebSocketKey, pGUID);
			uprintf("encode buffer is '%s' %d, %d\n", EncodeBuffer, nLen, (int)strlen(EncodeBuffer));

			uint8_t				  sha[20];
			MicroProfile_SHA1_CTX ctx;
			MicroProfile_SHA1_Init(&ctx);
			MicroProfile_SHA1_Update(&ctx, (unsigned char*)EncodeBuffer, nLen);
			MicroProfile_SHA1_Final((unsigned char*)&sha[0], &ctx);
			char HashOut[(2 + sizeof(sha) / 3) * 4];
			memset(&HashOut[0], 0, sizeof(HashOut));
			MicroProfileBase64Encode(&HashOut[0], &sha[0], sizeof(sha));

			char Reply[1024];
			nLen = stbsp_snprintf(Reply, sizeof(Reply) - 1, "%s%s\r\n\r\n", pHandShake, HashOut);
			MP_ASSERT(nLen < 1024 && nLen >= 0);
			MicroWSSendRaw(ConnectionId, Reply, nLen, MICROWS_FLUSH);
			return true;
		}
		else
		{
			return false;
		}
	}
#ifdef _WIN32
	closesocket(Connection);
#else
	close(Connection);
#endif
}
static uint32_t MicroWSFindConnection(MpSocket Socket)
{
	uint32_t ConnectionId = MICROWS_INVALID_CONNECTION;
	for(uint32 i = 0; i < MICROWS_MAX_CONNECTIONS; ++i)
	{
		uint32_t id = i + S.LastConnection;
		if(id != MICROWS_INVALID_CONNECTION)
		{
			uint32_t index = id % MICROWS_MAX_CONNECTIONS;
			if(S.Connections[index].Opening == S.Connections[index].Closed)
			{
				S.Connections[index].Opening = id;
				S.Connections[index].Socket = Socket ConnectionId = id;
				S.LastConnection								  = id + 1;
				break;
			}
		}
	}
	return ConnectionId;
}

void MicroWSUpdate(uint32_t* ConnectionsOpened, uint32_t* ConnectionsClosed, uint32_t* IncomingMessages)
{
	uint32_t Opened = 0, Closed = 0, Messages = 0;
	for(int i = 0; i < MAX_CONNECTIONS_PER_UPDATE; ++i)
	{
		MpSocket Connection = accept(S.ListenerSocket, 0, 0);
		if(MP_INVALID_SOCKET(Connection))
		{
			break;
		}
		uint32_t ConnectionId = MicroWSOpenConnection(Socket);
		if(ConnectionId != MICROWS_INVALID_CONNECTION)
		{
			MicroWSTryAccept(ConnectionId);
		}
	}
	MicroProfileWebSocketFrame();
}
bool MicroWSGetMessage(uint32_t Connection, uint32_t* ConnectionOut, uint64_t* OutBufferSize, uint8_t* OutBuffer)
{
}
bool MicroWSSendMessage(uint32_t Connection, uint32_t BufferSize, uint8_t* Data)
{
}
void MicroWSShutdown()
{
	if(S.IsRunning)
	{
		MicroProfileWebServerStop();
		S.nWebServerDataSent = (uint64_t)-1; // Will cause the web server and its thread to be restarted next time MicroProfileFlip() is called.
	}
}

bool MicroWSOpening(i)
{
	MicroWSConnection& C		 = S.Connections[i];
	uint32_t		   Openening = C.Openening;
	uint32_t		   Closed	 = C.Closed;
	return int32_t(Openening - Closed) > 0;
}

bool MicroWSOpen(uint32_t i)
{
	MicroWSConnection& C	  = S.Connections[i];
	uint32_t		   Open	  = C.Open;
	uint32_t		   Closed = C.Closed;
	return int32_t(Open - Closed) > 0;
}

static void MicroWSWrite(uint32_t i)
{
}

static void MicroWSRead(uint32_t i)
{
}

void MicroProfileWebSocketFrame()
{
	fd_set Read, Write, Error;
	FD_ZERO(&Read);
	FD_ZERO(&Write);
	FD_ZERO(&Error);
	MpSocket LastSocket		= 1;
	uint32_t NumOpenSockets = 0;
	for(uint32_t i = 0; i < MICROWS_MAX_CONNECTIONS; ++i)
	{
		if(MicroWSOpening(i) || MicroWSOpen(i))
		{
			MicroWSConnection& C = S.Connections[i];
			LastSocket			 = MicroProfileMax(LastSocket, C.Socket + 1);
			FD_SET(C.Socket, &Read);
			FD_SET(C.Socket, &Write);
			FD_SET(C.Socket, &Error);
			NumOpenSockets++;
		}
	}
	if(NumOpenSockets)
	{
		if(-1 == select(LastSocket, &Read, &Write, &Error, &tv))
		{
			MP_ASSERT(0);
		}
		else
		{
			timeval tv;
			tv.tv_sec  = 0;
			tv.tv_usec = 0;
			for(uint32_t i = 0; i < MICROWS_MAX_CONNECTIONS; ++i)
			{
				if(MicroWSOpening(i) || MicroWSOpen(i))
				{
					MicroWSConnection& C		  = S.Connections[i];
					MpSocket		   Socket	  = C.Socket[i];
					bool			   Disconnect = false;
					if(FD_ISSET(s, &Error))
					{
						Disconnect = true;
					}
					if(!Disconnect && FD_ISSET(s, &Read))
					{
						if(!MicroProfileWSRead(i))
						{
							Disconnect = true;
						}
					}
					if(!Disconnect && FD_ISSET(s, &Write))
					{
						if(!MicroProfileWSWrite(i))
						{
							Disconnect = true;
						}
					}

					if(Disconnect)
					{
						uprintf("removing socket %" PRId64 "\n", (uint64_t)Socket);
#ifndef _WIN32
						shutdown(C.Socket, SHUT_WR);
#else
						shutdown(C.Socket, 1);
#endif
						char tmp[128];
						int	 r = 1;
						while(r > 0)
						{
							r = recv(C.Socket, tmp, sizeof(tmp), 0);
						}
#ifdef _WIN32
						closesocket(C.Socket);
#else
						close(C.Socket);
#endif
						C.Socket = INVALID_SOCKET;
						C.Closed = C.Openening;
						C.Open	 = C.Opening;
					}
				}
			}
		}
	}
}

#ifndef MicroWSSetNonBlocking // fcntl doesnt work on a some unix like platforms..
void MicroWSSetNonBlocking(MpSocket Socket, int NonBlocking)
{
#ifdef _WIN32
	u_long nonBlocking = NonBlocking ? 1 : 0;
	ioctlsocket(Socket, FIONBIO, &nonBlocking);
#else
	int Options = fcntl(Socket, F_GETFL);
	if(NonBlocking)
	{
		fcntl(Socket, F_SETFL, Options | O_NONBLOCK);
	}
	else
	{
		fcntl(Socket, F_SETFL, Options & (~O_NONBLOCK));
	}
#endif
}
#endif

bool MicroWSWebServerStart()
{
	S.nWebServerDataSent = 0;
#ifdef _WIN32
	WSADATA wsa;
	if(WSAStartup(MAKEWORD(2, 2), &wsa))
	{
		S.ListenerSocket = (MpSocket)-1;
		return;
	}
#endif

	S.ListenerSocket = socket(PF_INET, SOCK_STREAM, 6);
	MWS_ASSERT(!MWS_INVALID_SOCKET(S.ListenerSocket));
	MicroWSSetNonBlocking(S.ListenerSocket, 1);

	{
		int r  = 0;
		int on = 1;
#if defined(_WIN32)
		r = setsockopt(S.ListenerSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&on, sizeof(on));
#else
		r = setsockopt(S.ListenerSocket, SOL_SOCKET, SO_REUSEADDR, (void*)&on, sizeof(on));
#endif
		(void)r;
	}

	int nStartPort = S.nWebServerPort;

	struct sockaddr_in Addr;
	Addr.sin_family		 = AF_INET;
	Addr.sin_addr.s_addr = INADDR_ANY;
	for(int i = 0; i < 20; ++i)
	{
		Addr.sin_port = htons(nStartPort + i);
		if(0 == bind(S.ListenerSocket, (sockaddr*)&Addr, sizeof(Addr)))
		{
			S.nWebServerPort = (uint32_t)(nStartPort + i);
			break;
		}
	}
	listen(S.ListenerSocket, 8);

	if(!S.WebSocketThreadRunning)
	{
		S.WebSocketThreadRunning = 1;
		MicroProfileThreadStart(&S.WebSocketSendThread, MicroProfileSocketSenderThread);
	}
}

void MicroWSWebServerJoin()
{
	if(S.WebSocketThreadRunning)
	{
		MicroWSThreadJoin(&S.WebSocketSendThread);
	}
	S.WebSocketThreadJoined = 1;
}

void MicroWSWebServerStop()
{
	MicroWSWebServerJoin();
	MP_ASSERT(S.WebSocketThreadJoined);
#ifdef _WIN32
	closesocket(S.ListenerSocket);
	WSACleanup();
#else
	close(S.ListenerSocket);
#endif
}

#ifndef _WIN32
void MicroProfileThreadStart(MicroProfileThread* pThread, MicroProfileThreadFunc Func)
{
	pthread_attr_t Attr;
	int			   r = pthread_attr_init(&Attr);
	MP_ASSERT(r == 0);
	pthread_create(pThread, &Attr, Func, 0);
}
void MicroProfileThreadJoin(MicroProfileThread* pThread)
{
	int r = pthread_join(*pThread, 0);
	MP_ASSERT(r == 0);
}
#elif defined(_WIN32)
DWORD __stdcall ThreadTrampoline(void* pFunc)
{
	MicroProfileThreadFunc F = (MicroProfileThreadFunc)pFunc;
	return (uint32_t)(uintptr_t)F(0);
}
void MicroProfileThreadStart(MicroProfileThread* pThread, MicroProfileThreadFunc Func)
{
	*pThread = CreateThread(0, 0, ThreadTrampoline, Func, 0, 0);
}
void MicroProfileThreadJoin(MicroProfileThread* pThread)
{
	WaitForSingleObject(*pThread, INFINITE);
	CloseHandle(*pThread);
}
#else
void MicroProfileThreadStart(MicroProfileThread* pThread, MicroProfileThreadFunc Func)
{
	*pThread = MP_ALLOC_OBJECT(std::thread);
	new(*pThread) std::thread(Func, nullptr);
}
void MicroProfileThreadJoin(MicroProfileThread* pThread)
{
	(*pThread)->join();
	(*pThread)->~thread();
	MP_FREE(*pThread);
	*pThread = 0;
}
#endif
