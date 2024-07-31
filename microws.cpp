#include "microws.h"

#define MWS_ASSERT(a)                                                                                                                                                                                  \
	do                                                                                                                                                                                                 \
	{                                                                                                                                                                                                  \
		if(!(a))                                                                                                                                                                                       \
		{                                                                                                                                                                                              \
			MWS_BREAK();                                                                                                                                                                               \
		}                                                                                                                                                                                              \
	} while(0)

typedef void* (*MicroWSThreadFunc)(void*);

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <basetsd.h>
#include <windows.h>

#define MWS_BREAK() __debugbreak()
typedef UINT_PTR MWSSocket;
#define MWS_INVALID_SOCKET(f) (f == INVALID_SOCKET)
typedef HANDLE MicroWSThread;

#else

#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>

#define MWS_BREAK() __builtin_trap()
#define MWS_INVALID_SOCKET(f) (f < 0)
typedef int		  MWSSocket;
typedef pthread_t MicroWSThread;

#endif

#if defined(MICROWS_SYSTEM_STB)
#include <stb_sprintf.h>
#else
#define STB_SPRINTF_IMPLEMENTATION
#include "stb/stb_sprintf.h"
#endif

#ifndef MICROWS_DEBUG
#define MICROWS_DEBUG 1
#endif

#ifndef MICROWS_LOG
#define MICROWS_LOG 1
#endif

void mws_log_impl(int error, uint32_t ConnectionId, const char* fmt, ...);

#if MICROWS_LOG || MICROWS_DEBUG
#define mws_log(id, ...) mws_log_impl(0, id, __VA_ARGS__)
#define mws_error(id, ...) mws_log_impl(1, id, __VA_ARGS__)
#else
#define mws_log(id, ...)                                                                                                                                                                               \
	do                                                                                                                                                                                                 \
	{                                                                                                                                                                                                  \
	} while(0)
#define mws_error(id, ...)                                                                                                                                                                             \
	do                                                                                                                                                                                                 \
	{                                                                                                                                                                                                  \
		MWS_BREAK();                                                                                                                                                                                   \
	} while(0)
#endif

typedef struct
{
	uint32_t	  state[5];
	uint32_t	  count[2];
	unsigned char buffer[64];
} MicroWS_SHA1_CTX;

static void		MicroWShreadStart(MicroWSThread* pThread, MicroWSThreadFunc Func);
static void		MicroWSThreadJoin(MicroWSThread* pThread);
static uint32_t MicroWSSendRaw(uint32_t ConnectionId, uint8_t* Data, uint32_t Size);
static bool		MicroWSOpening(uint32_t i);
static bool		MicroWSOpen(uint32_t i);
static bool		MicroWSWebServerStart();
static void*	MicroWSAllocRing();
static void		MicroWS_SHA1_Transform(uint32_t[5], const unsigned char[64]);
static void		MicroWS_SHA1_Init(MicroWS_SHA1_CTX* context);
static void		MicroWS_SHA1_Update(MicroWS_SHA1_CTX* context, const unsigned char* data, unsigned int len);
static void		MicroWS_SHA1_Final(unsigned char digest[20], MicroWS_SHA1_CTX* context);
static void		MicroWSBase64Encode(char* pOut, const uint8_t* pIn, uint32_t nLen);
static void		MicroWSWebServerStop();
template <typename T>
static T MicroWSMin(T a, T b);
template <typename T>
static T MicroWSMax(T a, T b);
template <typename T>
static T MicroWSClamp(T a, T min_, T max_);

struct MicroWSConnection
{
	uint32_t SendPut;
	uint32_t SendGet;
	uint8_t* SendBuffer;

	uint32_t RecvPut;
	uint32_t RecvGet;
	uint8_t* RecvBuffer;

	uint32_t Opening;
	uint32_t Open;
	uint32_t Closed;
	uint32_t SendBlocked = 0;

	uint32_t FailRSV;
	uint32_t Fail88;

	MWSSocket Socket = INVALID_SOCKET;
};
struct MicroWSState
{
	MWSSocket		  ListenerSocket;
	bool			  IsRunning			 = false;
	uint16_t		  nWebServerPort	 = 1999;
	uint32_t		  LastConnection	 = 0;
	uint32_t		  ConnectionVersion	 = 0;
	uint64_t		  nWebServerDataSent = 0;
	MicroWSConnection Connections[MICROWS_MAX_CONNECTIONS];
	uint32_t		  RejectCount = 0;
};
static MicroWSState S;
static void			MicroWSAtExitHandler()
{
	if(S.IsRunning)
	{
		MicroWSWebServerStop();
	}
}

bool MicroWSInit(uint16_t ListenPort)
{
	MWS_ASSERT(!S.IsRunning);
	S.nWebServerPort = ListenPort;
	if(MicroWSWebServerStart())
	{
		S.IsRunning = true;
		atexit(MicroWSAtExitHandler);
	}
	return S.IsRunning;
}

static uint32_t MicroWSPutSpace(uint32_t Put, uint32_t Get)
{
	if(Put < Get)
	{
		return Put - Get - 1;
	}
	else
	{
		return MICROWS_BUFFER_SPACE + Get - Put - 1;
	}
}
static uint32_t MicroWSPutAdvance(uint32_t Put, uint32_t Get, uint32_t Bytes)
{
	MWS_ASSERT(Bytes <= MicroWSPutSpace(Put, Get));
	Put += Bytes;
	if(Put >= MICROWS_BUFFER_SPACE)
		Put -= MICROWS_BUFFER_SPACE;
	return Put;
}

static uint32_t MicroWSGetSpace(uint32_t Get, uint32_t Put)
{
	if(Get <= Put)
		return Put - Get;
	else
		return Put + MICROWS_BUFFER_SPACE - Get;
}

static uint32_t MicroWSGetAdvance(uint32_t Get, uint32_t Put, uint32_t Bytes)
{
	MWS_ASSERT(Bytes <= MicroWSGetSpace(Put, Get));
	Get += Bytes;
	if(Get >= MICROWS_BUFFER_SPACE)
		Get -= MICROWS_BUFFER_SPACE;
	return Get;
}

static bool MicroWSTryAccept(uint32_t Index)
{
	MicroWSConnection& C		 = S.Connections[Index];
	bool			   IsOpen	 = MicroWSOpen(Index);
	bool			   IsOpening = MicroWSOpening(Index);
	MWS_ASSERT(!IsOpen);
	MWS_ASSERT(IsOpening);

	uint32_t Put   = C.RecvPut;
	uint32_t Get   = C.RecvGet;
	uint8_t* Data  = C.RecvBuffer + Get;
	uint32_t Bytes = MicroWSGetSpace(Get, Put);
	if(Bytes > 0)
	{
		mws_log(C.Opening, "->TRY_ACCEPT\n");
		//  check its null terminated
		int Terminated = -1;
		{
			// search for "\r\n\r\n" terminator
			for(int i = 0; i < (int)Bytes - 3; ++i)
			{
				if(0 == memcmp(Data + i, "\r\n\r\n", 4))
				{
					Terminated = i + 3;
					break;
				}
			}
		}
		if(Terminated == -1)
		{
			return false;
		}
		const uint8_t Term = Data[Terminated];
		Data[Terminated]   = '\0';
		char* Req		   = (char*)Data;
#if MICROPROFILE_MINIZ
		// Expires: Tue, 01 Jan 2199 16:00:00 GMT\r\n
#define MICROPROFILE_HTML_HEADER "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\nContent-Encoding: deflate\r\n\r\n"
#else
#define MICROPROFILE_HTML_HEADER "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\n\r\n"
#endif

		char* pHttp			= strstr(Req, "HTTP/");
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
			pWebSocketKey += sizeof("Sec-WebSocket-Key: ") - 1;
			Terminate(pWebSocketKey);

			const char* pGUID	   = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
			const char* pHandShake = "HTTP/1.1 101 Switching Protocols\r\n"
									 "Upgrade: websocket\r\n"
									 "Connection: Upgrade\r\n"
									 "Sec-WebSocket-Accept: ";

			char EncodeBuffer[512];
			int	 nLen = stbsp_snprintf(EncodeBuffer, sizeof(EncodeBuffer) - 1, "%s%s", pWebSocketKey, pGUID);

			uint8_t			 sha[20];
			MicroWS_SHA1_CTX ctx;
			MicroWS_SHA1_Init(&ctx);
			MicroWS_SHA1_Update(&ctx, (unsigned char*)EncodeBuffer, nLen);
			MicroWS_SHA1_Final((unsigned char*)&sha[0], &ctx);
			char HashOut[(2 + sizeof(sha) / 3) * 4];
			memset(&HashOut[0], 0, sizeof(HashOut));
			MicroWSBase64Encode(&HashOut[0], &sha[0], sizeof(sha));

			char Reply[1024];
			nLen = stbsp_snprintf(Reply, sizeof(Reply) - 1, "%s%s\r\n\r\n", pHandShake, HashOut);
			MWS_ASSERT(nLen < 1024 && nLen >= 0);
			MicroWSSendRaw(Index, (uint8_t*)&Reply[0], nLen);

			Data[Terminated] = Term;

			C.RecvGet = MicroWSGetAdvance(Get, Put, Terminated + 1);
			C.Open	  = C.Opening;
			mws_log(C.Open, "-> OPEN\n");

			S.ConnectionVersion++;
			return true;
		}
		else
		{
			mws_log(C.Opening, "No web socket key\n");
		}
	}
	return false;
}
static const char* WSAGetErrorString(int Error)
{
	switch(Error)
	{
	case WSAEINTR:
		return "WSAEINTR";
	case WSAEBADF:
		return "WSAEBADF";
	case WSAEACCES:
		return "WSAEACCES";
	case WSAEFAULT:
		return "WSAEFAULT";
	case WSAEINVAL:
		return "WSAEINVAL";
	case WSAEMFILE:
		return "WSAEMFILE";
	case WSAEWOULDBLOCK:
		return "WSAEWOULDBLOCK";
	case WSAEINPROGRESS:
		return "WSAEINPROGRESS";
	case WSAEALREADY:
		return "WSAEALREADY";
	case WSAENOTSOCK:
		return "WSAENOTSOCK";
	case WSAEDESTADDRREQ:
		return "WSAEDESTADDRREQ";
	case WSAEMSGSIZE:
		return "WSAEMSGSIZE";
	case WSAEPROTOTYPE:
		return "WSAEPROTOTYPE";
	case WSAENOPROTOOPT:
		return "WSAENOPROTOOPT";
	case WSAEPROTONOSUPPORT:
		return "WSAEPROTONOSUPPORT";
	case WSAESOCKTNOSUPPORT:
		return "WSAESOCKTNOSUPPORT";
	case WSAEOPNOTSUPP:
		return "WSAEOPNOTSUPP";
	case WSAEPFNOSUPPORT:
		return "WSAEPFNOSUPPORT";
	case WSAEAFNOSUPPORT:
		return "WSAEAFNOSUPPORT";
	case WSAEADDRINUSE:
		return "WSAEADDRINUSE";
	case WSAEADDRNOTAVAIL:
		return "WSAEADDRNOTAVAIL";
	case WSAENETDOWN:
		return "WSAENETDOWN";
	case WSAENETUNREACH:
		return "WSAENETUNREACH";
	case WSAENETRESET:
		return "WSAENETRESET";
	case WSAECONNABORTED:
		return "WSAECONNABORTED";
	case WSAECONNRESET:
		return "WSAECONNRESET";
	case WSAENOBUFS:
		return "WSAENOBUFS";
	case WSAEISCONN:
		return "WSAEISCONN";
	case WSAENOTCONN:
		return "WSAENOTCONN";
	case WSAESHUTDOWN:
		return "WSAESHUTDOWN";
	case WSAETOOMANYREFS:
		return "WSAETOOMANYREFS";
	case WSAETIMEDOUT:
		return "WSAETIMEDOUT";
	case WSAECONNREFUSED:
		return "WSAECONNREFUSED";
	case WSAELOOP:
		return "WSAELOOP";
	case WSAENAMETOOLONG:
		return "WSAENAMETOOLONG";
	case WSAEHOSTDOWN:
		return "WSAEHOSTDOWN";
	case WSAEHOSTUNREACH:
		return "WSAEHOSTUNREACH";
	case WSAENOTEMPTY:
		return "WSAENOTEMPTY";
	case WSAEPROCLIM:
		return "WSAEPROCLIM";
	case WSAEUSERS:
		return "WSAEUSERS";
	case WSAEDQUOT:
		return "WSAEDQUOT";
	case WSAESTALE:
		return "WSAESTALE";
	case WSAEREMOTE:
		return "WSAEREMOTE";
	case WSASYSNOTREADY:
		return "WSASYSNOTREADY";
	case WSAVERNOTSUPPORTED:
		return "WSAVERNOTSUPPORTED";
	case WSANOTINITIALISED:
		return "WSANOTINITIALISED";
	case WSAEDISCON:
		return "WSAEDISCON";
	case WSAENOMORE:
		return "WSAENOMORE";
	case WSAECANCELLED:
		return "WSAECANCELLED";
	case WSAEINVALIDPROCTABLE:
		return "WSAEINVALIDPROCTABLE";
	case WSAEINVALIDPROVIDER:
		return "WSAEINVALIDPROVIDER";
	case WSAEPROVIDERFAILEDINIT:
		return "WSAEPROVIDERFAILEDINIT";
	case WSASYSCALLFAILURE:
		return "WSASYSCALLFAILURE";
	case WSASERVICE_NOT_FOUND:
		return "WSASERVICE_NOT_FOUND";
	case WSA_E_NO_MORE:
		return "WSA_E_NO_MORE";
	case WSA_E_CANCELLED:
		return "WSA_E_CANCELLED";
	case WSA_QOS_RECEIVERS:
		return "WSA_QOS_RECEIVERS";
	case WSA_QOS_SENDERS:
		return "WSA_QOS_SENDERS";
	case WSA_QOS_NO_SENDERS:
		return "WSA_QOS_NO_SENDERS";
	case WSA_QOS_NO_RECEIVERS:
		return "WSA_QOS_NO_RECEIVERS";
	case WSA_QOS_REQUEST_CONFIRMED:
		return "WSA_QOS_REQUEST_CONFIRMED";
	case WSA_QOS_ADMISSION_FAILURE:
		return "WSA_QOS_ADMISSION_FAILURE";
	case WSA_QOS_POLICY_FAILURE:
		return "WSA_QOS_POLICY_FAILURE";
	case WSA_QOS_BAD_STYLE:
		return "WSA_QOS_BAD_STYLE";
	case WSA_QOS_BAD_OBJECT:
		return "WSA_QOS_BAD_OBJECT";
	case WSA_QOS_TRAFFIC_CTRL_ERROR:
		return "WSA_QOS_TRAFFIC_CTRL_ERROR";
	case WSA_QOS_GENERIC_ERROR:
		return "WSA_QOS_GENERIC_ERROR";
	case WSA_QOS_ESERVICETYPE:
		return "WSA_QOS_ESERVICETYPE";
	case WSA_QOS_EFLOWSPEC:
		return "WSA_QOS_EFLOWSPEC";
	case WSA_QOS_EPROVSPECBUF:
		return "WSA_QOS_EPROVSPECBUF";
	case WSA_QOS_EFILTERSTYLE:
		return "WSA_QOS_EFILTERSTYLE";
	case WSA_QOS_EFILTERTYPE:
		return "WSA_QOS_EFILTERTYPE";
	case WSA_QOS_EFILTERCOUNT:
		return "WSA_QOS_EFILTERCOUNT";
	case WSA_QOS_EOBJLENGTH:
		return "WSA_QOS_EOBJLENGTH";
	case WSA_QOS_EFLOWCOUNT:
		return "WSA_QOS_EFLOWCOUNT";
	case WSA_QOS_EUNKOWNPSOBJ:
		return "WSA_QOS_EUNKOWNPSOBJ";
	case WSA_QOS_EPOLICYOBJ:
		return "WSA_QOS_EPOLICYOBJ";
	case WSA_QOS_EFLOWDESC:
		return "WSA_QOS_EFLOWDESC";
	case WSA_QOS_EPSFLOWSPEC:
		return "WSA_QOS_EPSFLOWSPEC";
	case WSA_QOS_EPSFILTERSPEC:
		return "WSA_QOS_EPSFILTERSPEC";
	case WSA_QOS_ESDMODEOBJ:
		return "WSA_QOS_ESDMODEOBJ";
	case WSA_QOS_ESHAPERATEOBJ:
		return "WSA_QOS_ESHAPERATEOBJ";
	case WSA_QOS_RESERVED_PETYPE:
		return "WSA_QOS_RESERVED_PETYPE";
	}
	return "unknown?";
}

static void MicroWSCheckErrorSelect(int Error)
{
	MWS_ASSERT(Error != EAGAIN);
	MWS_ASSERT(Error != EWOULDBLOCK);

	if(Error == SOCKET_ERROR)
	{
		int err1 = WSAGetLastError();
		if(err1 == WSAEWOULDBLOCK)
			return;
		switch(err1)
		{
		case WSAEWOULDBLOCK:
			return;
		default:
			mws_error(MICROWS_INVALID_CONNECTION, "Unknown WSA Error: %d:%s\n", err1, WSAGetErrorString(err1));
		}
	}
}
static void MicroWSClose(uint32_t i)
{
	MicroWSConnection& C = S.Connections[i];
	shutdown(C.Socket, 2);
	closesocket(C.Socket);
	C.Socket = INVALID_SOCKET;
	C.Open = C.Closed = C.Opening;
	S.ConnectionVersion++;
}

static void MicroWSCheckError(uint32_t i, int Error)
{
	MWS_ASSERT(Error != EAGAIN);
	MWS_ASSERT(Error != EWOULDBLOCK);

	if(Error == SOCKET_ERROR)
	{
		MicroWSConnection& C	= S.Connections[i];
		int				   err1 = WSAGetLastError();
		switch(err1)
		{
		case WSAEWOULDBLOCK:
			return;
		case WSAENETRESET:
		case WSAECONNABORTED:
		case WSAECONNRESET:
			mws_log(C.Opening, "->CLOSE (WSAError %d:%s)\n", err1, WSAGetErrorString(err1));
			MicroWSClose(i);
			break;
		default:
			mws_error(MICROWS_INVALID_CONNECTION, "Unknown WSA Error: %d:%s\n", err1, WSAGetErrorString(err1));
		}
	}
}
static uint32_t MicroWSDrain()
{
	uint32_t FailCount		  = 0;
	uint32_t MaxDataAvailable = 0;
	for(uint32_t i = 0; i < MICROWS_MAX_CONNECTIONS; ++i)
	{
		MicroWSConnection& C		 = S.Connections[i];
		bool			   IsOpen	 = MicroWSOpen(i);
		bool			   IsOpening = MicroWSOpening(i);
		if(IsOpen || IsOpening)
		{
			// read everything possible
			{
				uint32_t Put	  = C.RecvPut;
				uint32_t Get	  = C.RecvGet;
				uint32_t PutSpace = MicroWSPutSpace(Put, Get);
				int		 Bytes	  = recv(C.Socket, (char*)C.RecvBuffer + Put, PutSpace, 0);
				if(Bytes > 0)
				{
					Put		  = MicroWSPutAdvance(Put, Get, (uint32_t)Bytes);
					C.RecvPut = Put;
				}
				else if(Bytes < 0)
				{
					MicroWSCheckError(i, Bytes);
				}
				uint32_t DataAvailable = MicroWSGetSpace(Get, Put);
				MaxDataAvailable	   = MaxDataAvailable > DataAvailable ? MaxDataAvailable : DataAvailable;
			}
		}
		IsOpen	  = MicroWSOpen(i);
		IsOpening = MicroWSOpening(i);
		if(IsOpening && !IsOpen)
		{
			MicroWSTryAccept(i);
		}
		IsOpen	  = MicroWSOpen(i);
		IsOpening = MicroWSOpening(i);
		if(IsOpen || IsOpening)
		{
			// write everything possible.
			{
				uint32_t Put	  = C.SendPut;
				uint32_t Get	  = C.SendGet;
				uint32_t GetSpace = MicroWSGetSpace(Get, Put);
				int		 Bytes	  = send(C.Socket, (char*)C.SendBuffer + Get, GetSpace, 0);
				if(Bytes > 0)
				{
					Get		  = MicroWSGetAdvance(Get, Put, (uint32_t)Bytes);
					C.SendGet = Get;
				}
				else if(Bytes < 0)
				{
					MicroWSCheckError(i, Bytes);
				}
			}
		}
	}
	return MaxDataAvailable;
}
static uint32_t MicroWSSendRaw(uint32_t ConnectionId, uint8_t* Data, uint32_t Size)
{
	uint32_t FailCount = 0;
	for(uint32_t i = 0; i < MICROWS_MAX_CONNECTIONS; ++i)
	{
		MicroWSConnection& C = S.Connections[i];
		if(((C.Open == ConnectionId || C.Opening == ConnectionId) && C.Closed != ConnectionId) || (ConnectionId == MICROWS_INVALID_CONNECTION && MicroWSOpen(i)))
		{
			uint32_t Put   = C.SendPut;
			uint32_t Get   = C.SendGet;
			uint32_t Bytes = MicroWSPutSpace(Put, Get);
			if(Bytes < Size)
			{
				FailCount++;
				continue;
			}
			memcpy(C.SendBuffer + Put, Data, Size);
			C.SendPut = MicroWSPutAdvance(Put, Get, Size);
		}
	}
	return FailCount;
}

#ifdef _WIN32
static void* MicroWSAllocRing()
{
	// Stolen from https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualalloc2#examples
	HANDLE		 Section = nullptr;
	SYSTEM_INFO	 SysInfo;
	void*		 RingBuffer	  = nullptr;
	void*		 Placeholder1 = nullptr;
	void*		 Placeholder2 = nullptr;
	void*		 View1		  = nullptr;
	void*		 View2		  = nullptr;
	const size_t BufferSize	  = MICROWS_BUFFER_SPACE;

	GetSystemInfo(&SysInfo);
	if((BufferSize % SysInfo.dwAllocationGranularity) != 0)
	{
		return nullptr;
	}

	Placeholder1 = (PCHAR)VirtualAlloc2(nullptr, nullptr, 2 * BufferSize, MEM_RESERVE | MEM_RESERVE_PLACEHOLDER, PAGE_NOACCESS, nullptr, 0);

	if(!Placeholder1)
	{
		mws_log(MICROWS_INVALID_CONNECTION, "VirtualAlloc2 failed, error %#x\n", GetLastError());
		goto Exit;
	}
	if(FALSE == VirtualFree(Placeholder1, BufferSize, MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER))
	{
		mws_log(MICROWS_INVALID_CONNECTION, "VirtualFreeEx failed, error %#x\n", GetLastError());
		goto Exit;
	}
	Placeholder2 = (void*)((ULONG_PTR)Placeholder1 + BufferSize);
	Section		 = CreateFileMapping(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, BufferSize, nullptr);
	if(!Section)
	{
		mws_log(MICROWS_INVALID_CONNECTION, "CreateFileMapping failed, error %#x\n", GetLastError());
		goto Exit;
	}

	View1 = MapViewOfFile3(Section, nullptr, Placeholder1, 0, BufferSize, MEM_REPLACE_PLACEHOLDER, PAGE_READWRITE, nullptr, 0);
	if(!View1)
	{
		mws_log(MICROWS_INVALID_CONNECTION, "MapViewOfFile3 failed, error %#x\n", GetLastError());
		goto Exit;
	}
	Placeholder1 = nullptr;
	View2		 = MapViewOfFile3(Section, nullptr, Placeholder2, 0, BufferSize, MEM_REPLACE_PLACEHOLDER, PAGE_READWRITE, nullptr, 0);
	if(!View2)
	{
		mws_log(MICROWS_INVALID_CONNECTION, "MapViewOfFile3 failed, error %#x\n", GetLastError());
		goto Exit;
	}
	Placeholder2 = nullptr;
	RingBuffer	 = View1;
	View1		 = nullptr;
	View2		 = nullptr;

Exit:

	if(Section)
	{
		CloseHandle(Section);
	}

	if(Placeholder1)
	{
		VirtualFree(Placeholder1, 0, MEM_RELEASE);
	}

	if(Placeholder2)
	{
		VirtualFree(Placeholder2, 0, MEM_RELEASE);
	}

	if(View1)
	{
		UnmapViewOfFileEx(View1, 0);
	}

	if(View2)
	{
		UnmapViewOfFileEx(View2, 0);
	}

	return RingBuffer;
}
#endif
static uint32_t MicroWSFindConnection()
{
	uint32_t ConnectionId = MICROWS_INVALID_CONNECTION;
	uint32_t Last		  = S.LastConnection;
	if(Last >= MICROWS_ALL_CONNECTIONS || (Last + MICROWS_MAX_CONNECTIONS >= MICROWS_ALL_CONNECTIONS) || (Last + MICROWS_MAX_CONNECTIONS < Last))
		Last = 0;
	for(uint32_t i = 0; i < MICROWS_MAX_CONNECTIONS; ++i)
	{
		uint32_t		   id	 = i + Last;
		uint32_t		   index = id % MICROWS_MAX_CONNECTIONS;
		MicroWSConnection& C	 = S.Connections[index];
		if(C.Opening == C.Closed)
		{
			MWS_ASSERT(C.Opening != id);
			S.LastConnection = id;
			ConnectionId	 = id;
			break;
		}
	}
	return ConnectionId;
}

static void MicroWSAssignConnection(uint32_t Id, MWSSocket Socket)
{

	uint32_t Index = Id % MICROWS_MAX_CONNECTIONS;

	MicroWSConnection& C = S.Connections[Index];
	MWS_ASSERT(C.Opening == C.Closed);
	if(!C.SendBuffer)
		C.SendBuffer = (uint8_t*)MicroWSAllocRing();
	if(!C.RecvBuffer)
		C.RecvBuffer = (uint8_t*)MicroWSAllocRing();

	C.Opening = Id;
	C.Socket  = Socket;

	S.ConnectionVersion++;

	C.SendBlocked = 0;
	C.SendPut	  = 0;
	C.SendGet	  = 0;
	C.RecvPut	  = 0;
	C.RecvGet	  = 0;
	C.Fail88	  = 0;
	C.FailRSV	  = 0;
	mws_log(Id, "->ASSIGN\n");
}

void MicroWSGetState(MicroWSConnectionState& State)
{
	uint32_t NumConnections = 0;
	for(uint32_t i = 0; i < MICROWS_MAX_CONNECTIONS; ++i)
	{
		MicroWSConnection& C = S.Connections[i];
		if(MicroWSOpen(i))
		{
			uint32_t Put = C.RecvPut;
			uint32_t Get = C.RecvGet;

			State.Connections[NumConnections] = C.Open;
			State.Data[NumConnections]		  = MicroWSGetSpace(Get, Put);
			NumConnections++;
		}
	}
	State.NumConnections	= NumConnections;
	State.ConnectionVersion = S.ConnectionVersion;
}

void MicroWSUpdate(uint32_t* ConnectionsVersion, uint32_t* MaxMessageData)
{
	for(int i = 0; i < MAX_CONNECTIONS_PER_UPDATE; ++i)
	{
		uint32_t NewConnection = MicroWSFindConnection();
		if(NewConnection == MICROWS_INVALID_CONNECTION)
			break; // don't accept if we dont have a slot to accept the connection
		MWSSocket Socket = accept(S.ListenerSocket, 0, 0);
		if(MWS_INVALID_SOCKET(Socket))
		{
			int err1 = WSAGetLastError();
			if(err1 != WSAEWOULDBLOCK)
			{
				mws_log(MICROWS_INVALID_CONNECTION, "No Connection WSA Error: %d:%s\n", err1, WSAGetErrorString(err1));
			}
			break;
		}
		MicroWSAssignConnection(NewConnection, Socket);
	}
	uint32_t MaxData = MicroWSDrain();
	if(MaxMessageData)
		*MaxMessageData = MaxData;
	if(ConnectionsVersion)
		*ConnectionsVersion = S.ConnectionVersion;
}

#define WEBSOCKET_HEADER_MAX 18
struct MicroWSWebSocketHeader0
{
	union
	{
		struct
		{
			uint8_t opcode : 4;
			uint8_t RSV3 : 1;
			uint8_t RSV2 : 1;
			uint8_t RSV1 : 1;
			uint8_t FIN : 1;
		};
		uint8_t v;
	};
};

struct MicroWSWebSocketHeader1
{
	union
	{
		struct
		{
			uint8_t payload : 7;
			uint8_t MASK : 1;
		};
		uint8_t v;
	};
};

uint32_t MicroWSTryRead(void* Src, uint32_t Size, uint32_t& OutOffset, uint32_t Connection)
{

	//  0                   1                   2                   3
	//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
	// +-+-+-+-+-------+-+-------------+-------------------------------+
	// |F|R|R|R| opcode|M| Payload len |    Extended payload length    |
	// |I|S|S|S|  (4)  |A|     (7)     |             (16/64)           |
	// |N|V|V|V|       |S|             |   (if payload len==126/127)   |
	// | |1|2|3|       |K|             |                               |
	// +-+-+-+-+-------+-+-------------+ - - - - - - - - - - - - - - - +
	MicroWSConnection& C = S.Connections[Connection];
	if(Size < 2)
		return 0;
	uint8_t* Data	   = (uint8_t*)Src;
	uint8_t* DataStart = Data;
	uint8_t* DataEnd   = Data + Size;

	MicroWSWebSocketHeader0* h0 = (MicroWSWebSocketHeader0*)(Data++);
	MicroWSWebSocketHeader1* h1 = (MicroWSWebSocketHeader1*)(Data++);
	if(h0->v == 0x88)
	{
		C.Fail88++;
		return 0; // should we discard it?
	}

	if(h0->RSV1 != 0 || h0->RSV2 != 0 || h0->RSV3 != 0)
	{
		C.FailRSV++;
		return 0;
	}

	uint32_t PacketSize = h1->payload;
	uint32_t NumBytes	= 0;
	switch(PacketSize)
	{
	case 126:
		NumBytes = 2;
		break;
	case 127:
		NumBytes = 8;
		break;
	default:
		break;
	}
	if(NumBytes)
	{
		PacketSize			   = 0;
		uint64_t MessageLength = 0;

		uint8_t* BytesMessage = Data;
		Data += NumBytes;
		if(Data > DataEnd)
			return 0; // incomplete message
		for(uint32_t i = 0; i < NumBytes; i++)
		{
			PacketSize <<= 8;
			PacketSize += BytesMessage[i];
		}

		for(uint32_t i = 0; i < NumBytes; i++)
			MessageLength |= BytesMessage[i] << ((NumBytes - 1 - i) * 8);
		MWS_ASSERT(MessageLength == PacketSize);
	}
	uint8_t* pMask = nullptr;
	if(h1->MASK)
	{
		pMask = Data;
		Data += 4;
		if(Data > DataEnd)
			return 0;
	}
	uint8_t* Bytes = Data;

	OutOffset = (uint32_t)(Data - DataStart);
	Data += PacketSize;
	if(Data > DataEnd)
		return 0;
	if(pMask && (pMask[0] != 0 || pMask[1] != 0 || pMask[2] != 0 || pMask[3] != 0))
	{
		for(uint32_t i = 0; i < PacketSize; ++i)
			Bytes[i] ^= pMask[i & 3];
		// clear so we can run code repeatedly if caller calls with a buffer too small.
		pMask[0] = 0;
		pMask[1] = 0;
		pMask[2] = 0;
		pMask[3] = 0;
	}

	return PacketSize;
}

uint32_t MicroWSWrite(uint8_t* Dst, const void* Src, uint32_t Size)
{
	MicroWSWebSocketHeader0 h0;
	MicroWSWebSocketHeader1 h1;
	h0.v					 = 0;
	h1.v					 = 0;
	h0.opcode				 = 1;
	h0.FIN					 = 1;
	uint32_t nExtraSizeBytes = 0;
	uint8_t	 nExtraSize[8];
	if(Size > 125)
	{
		if(Size > 0xffff)
		{
			nExtraSizeBytes = 8;
			h1.payload		= 127;
		}
		else
		{
			h1.payload		= 126;
			nExtraSizeBytes = 2;
		}
		uint64_t nCount = Size;
		for(uint32_t i = 0; i < nExtraSizeBytes; ++i)
		{
			nExtraSize[nExtraSizeBytes - i - 1] = nCount & 0xff;
			nCount >>= 8;
		}

		uint32_t SizeSum = 0;
		for(uint32_t i = 0; i < nExtraSizeBytes; i++)
		{
			SizeSum <<= 8;
			SizeSum += nExtraSize[i];
		}
		MWS_ASSERT(SizeSum == Size); // verify
	}
	else
	{
		h1.payload = Size;
	}
	char* Out = (char*)Dst;

	*Out++ = *(char*)&h0;
	*Out++ = *(char*)&h1;
	if(nExtraSizeBytes)
	{
		memcpy(Out, &nExtraSize[0], nExtraSizeBytes);
		Out += nExtraSizeBytes;
	}
	memcpy(Out, Src, Size);
	return 2 + nExtraSizeBytes + Size;
}

uint32_t MicroWSGetMessage(uint32_t Connection, uint8_t* OutBuffer, uint32_t BufferSize, uint32_t* ConnectionOut)
{
	uint32_t start		   = 0;
	uint32_t end		   = MICROWS_MAX_CONNECTIONS;
	bool	 AnyConnection = Connection != MICROWS_ANY_CONNECTION;

	if(Connection == MICROWS_ALL_CONNECTIONS)
		return 0;
	if(!AnyConnection)
	{
		start = (Connection % MICROWS_MAX_CONNECTIONS);
		end	  = start + 1;
	}
	for(uint32_t i = start; i < end; ++i)
	{
		MicroWSConnection& C = S.Connections[i];
		if(MicroWSOpen(i) && (AnyConnection || C.Open == Connection))
		{
			uint32_t Put   = C.RecvPut;
			uint32_t Get   = C.RecvGet;
			uint32_t Bytes = MicroWSGetSpace(Get, Put);
			if(Bytes)
			{

				uint8_t* Data		   = C.RecvBuffer + Get;
				uint32_t MessageOffset = 0;
				uint32_t MessageSize   = MicroWSTryRead(Data, Bytes, MessageOffset, i);
				if(MessageSize && MessageSize <= BufferSize)
				{
					memcpy(OutBuffer, Data + MessageOffset, MessageSize);
					C.RecvGet = MicroWSGetAdvance(Get, Put, MessageOffset + MessageSize);
					return MessageSize;
				}
			}
		}
	}
	return 0;
}

bool MicroWSSendMessage(uint32_t Connection, const void* Ptr, uint32_t Size)
{
	uint32_t start			= 0;
	uint32_t end			= MICROWS_MAX_CONNECTIONS;
	bool	 AnyConnection	= Connection == MICROWS_ANY_CONNECTION;
	bool	 AllConnections = Connection == MICROWS_ALL_CONNECTIONS;
	if(Connection < MICROWS_ALL_CONNECTIONS)
	{
		start = (Connection % MICROWS_MAX_CONNECTIONS);
		end	  = start + 1;
	}
	int Failed = 0;
	for(uint32_t i = start; i < end; ++i)
	{
		MicroWSConnection& C = S.Connections[i];
		if(MicroWSOpen(i) && (AnyConnection || AllConnections || C.Open == Connection))
		{
			uint32_t Put   = C.SendPut;
			uint32_t Get   = C.SendGet;
			uint32_t Bytes = MicroWSPutSpace(Put, Get);
			if(Bytes >= Size + WEBSOCKET_HEADER_MAX)
			{
				uint8_t* SendData	= C.SendBuffer + Put;
				uint32_t WriteBytes = MicroWSWrite(SendData, Ptr, Size);
				C.SendPut			= MicroWSPutAdvance(Put, Get, WriteBytes);
			}
			else
			{
				Failed++;
				C.SendBlocked++;
			}
		}
	}
	return Failed == 0;
}
void MicroWSShutdown()
{
	if(S.IsRunning)
	{
		MicroWSWebServerStop();
		S.nWebServerDataSent = (uint64_t)-1; // Will cause the web server and its thread to be restarted next time MicroWSFlip() is called.
	}
}

bool MicroWSOpening(uint32_t i)
{
	MicroWSConnection& C		 = S.Connections[i];
	uint32_t		   Openening = C.Opening;
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

#ifndef MicroWSSetNonBlocking // fcntl doesnt work on a some unix like platforms..
void MicroWSSetNonBlocking(MWSSocket Socket, int NonBlocking)
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
	S.LastConnection	 = 0;
	S.RejectCount		 = 0;

	for(MicroWSConnection& C : S.Connections)
	{
		C.SendPut	  = 0;
		C.SendGet	  = 0;
		C.RecvPut	  = 0;
		C.RecvGet	  = 0;
		C.Opening	  = MICROWS_INVALID_CONNECTION;
		C.Open		  = MICROWS_INVALID_CONNECTION;
		C.Closed	  = MICROWS_INVALID_CONNECTION;
		C.Socket	  = INVALID_SOCKET;
		C.SendBlocked = 0;
	}
	MicroWSConnection& C = S.Connections[0];
	if(!C.SendBuffer)
	{
		C.SendBuffer = (uint8_t*)MicroWSAllocRing();
		if(!C.SendBuffer)
			return false; // failed to allocate ring.
	}

#ifdef _WIN32
	WSADATA wsa;
	if(WSAStartup(MAKEWORD(2, 2), &wsa))
	{
		S.ListenerSocket = (MWSSocket)-1;
		return false;
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
	listen(S.ListenerSocket, MICROWS_MAX_CONNECTIONS);
	return true;
}

void MicroWSWebServerStop()
{
#ifdef _WIN32
	closesocket(S.ListenerSocket);
	WSACleanup();
#else
	close(S.ListenerSocket);
#endif
}

// begin: SHA-1 in C
// ftp://ftp.funet.fi/pub/crypt/hash/sha/sha1.c
// SHA-1 in C
// By Steve Reid <steve@edmweb.com>
// 100% Public Domain

#define rol(value, bits) (((value) << (bits)) | ((value) >> (32 - (bits))))

#define blk0(i) (block->l[i] = htonl(block->l[i]))
#define blk(i) (block->l[i & 15] = rol(block->l[(i + 13) & 15] ^ block->l[(i + 8) & 15] ^ block->l[(i + 2) & 15] ^ block->l[i & 15], 1))

#define R0(v, w, x, y, z, i)                                                                                                                                                                           \
	z += ((w & (x ^ y)) ^ y) + blk0(i) + 0x5A827999 + rol(v, 5);                                                                                                                                       \
	w = rol(w, 30);
#define R1(v, w, x, y, z, i)                                                                                                                                                                           \
	z += ((w & (x ^ y)) ^ y) + blk(i) + 0x5A827999 + rol(v, 5);                                                                                                                                        \
	w = rol(w, 30);
#define R2(v, w, x, y, z, i)                                                                                                                                                                           \
	z += (w ^ x ^ y) + blk(i) + 0x6ED9EBA1 + rol(v, 5);                                                                                                                                                \
	w = rol(w, 30);
#define R3(v, w, x, y, z, i)                                                                                                                                                                           \
	z += (((w | x) & y) | (w & x)) + blk(i) + 0x8F1BBCDC + rol(v, 5);                                                                                                                                  \
	w = rol(w, 30);
#define R4(v, w, x, y, z, i)                                                                                                                                                                           \
	z += (w ^ x ^ y) + blk(i) + 0xCA62C1D6 + rol(v, 5);                                                                                                                                                \
	w = rol(w, 30);

// Hash a single 512-bit block. This is the core of the algorithm.

static void MicroWS_SHA1_Transform(uint32_t state[5], const unsigned char buffer[64])
{
	uint32_t a, b, c, d, e;
	typedef union
	{
		unsigned char c[64];
		uint32_t	  l[16];
	} CHAR64LONG16;
	CHAR64LONG16* block;

	block = (CHAR64LONG16*)buffer;
	// Copy context->state[] to working vars
	a = state[0];
	b = state[1];
	c = state[2];
	d = state[3];
	e = state[4];
	// 4 rounds of 20 operations each. Loop unrolled.
	R0(a, b, c, d, e, 0);
	R0(e, a, b, c, d, 1);
	R0(d, e, a, b, c, 2);
	R0(c, d, e, a, b, 3);
	R0(b, c, d, e, a, 4);
	R0(a, b, c, d, e, 5);
	R0(e, a, b, c, d, 6);
	R0(d, e, a, b, c, 7);
	R0(c, d, e, a, b, 8);
	R0(b, c, d, e, a, 9);
	R0(a, b, c, d, e, 10);
	R0(e, a, b, c, d, 11);
	R0(d, e, a, b, c, 12);
	R0(c, d, e, a, b, 13);
	R0(b, c, d, e, a, 14);
	R0(a, b, c, d, e, 15);
	R1(e, a, b, c, d, 16);
	R1(d, e, a, b, c, 17);
	R1(c, d, e, a, b, 18);
	R1(b, c, d, e, a, 19);
	R2(a, b, c, d, e, 20);
	R2(e, a, b, c, d, 21);
	R2(d, e, a, b, c, 22);
	R2(c, d, e, a, b, 23);
	R2(b, c, d, e, a, 24);
	R2(a, b, c, d, e, 25);
	R2(e, a, b, c, d, 26);
	R2(d, e, a, b, c, 27);
	R2(c, d, e, a, b, 28);
	R2(b, c, d, e, a, 29);
	R2(a, b, c, d, e, 30);
	R2(e, a, b, c, d, 31);
	R2(d, e, a, b, c, 32);
	R2(c, d, e, a, b, 33);
	R2(b, c, d, e, a, 34);
	R2(a, b, c, d, e, 35);
	R2(e, a, b, c, d, 36);
	R2(d, e, a, b, c, 37);
	R2(c, d, e, a, b, 38);
	R2(b, c, d, e, a, 39);
	R3(a, b, c, d, e, 40);
	R3(e, a, b, c, d, 41);
	R3(d, e, a, b, c, 42);
	R3(c, d, e, a, b, 43);
	R3(b, c, d, e, a, 44);
	R3(a, b, c, d, e, 45);
	R3(e, a, b, c, d, 46);
	R3(d, e, a, b, c, 47);
	R3(c, d, e, a, b, 48);
	R3(b, c, d, e, a, 49);
	R3(a, b, c, d, e, 50);
	R3(e, a, b, c, d, 51);
	R3(d, e, a, b, c, 52);
	R3(c, d, e, a, b, 53);
	R3(b, c, d, e, a, 54);
	R3(a, b, c, d, e, 55);
	R3(e, a, b, c, d, 56);
	R3(d, e, a, b, c, 57);
	R3(c, d, e, a, b, 58);
	R3(b, c, d, e, a, 59);
	R4(a, b, c, d, e, 60);
	R4(e, a, b, c, d, 61);
	R4(d, e, a, b, c, 62);
	R4(c, d, e, a, b, 63);
	R4(b, c, d, e, a, 64);
	R4(a, b, c, d, e, 65);
	R4(e, a, b, c, d, 66);
	R4(d, e, a, b, c, 67);
	R4(c, d, e, a, b, 68);
	R4(b, c, d, e, a, 69);
	R4(a, b, c, d, e, 70);
	R4(e, a, b, c, d, 71);
	R4(d, e, a, b, c, 72);
	R4(c, d, e, a, b, 73);
	R4(b, c, d, e, a, 74);
	R4(a, b, c, d, e, 75);
	R4(e, a, b, c, d, 76);
	R4(d, e, a, b, c, 77);
	R4(c, d, e, a, b, 78);
	R4(b, c, d, e, a, 79);
	// Add the working vars back into context.state[]
	state[0] += a;
	state[1] += b;
	state[2] += c;
	state[3] += d;
	state[4] += e;
	// Wipe variables
	a = b = c = d = e = 0;
}

void MicroWS_SHA1_Init(MicroWS_SHA1_CTX* context)
{
	// SHA1 initialization constants
	context->state[0] = 0x67452301;
	context->state[1] = 0xEFCDAB89;
	context->state[2] = 0x98BADCFE;
	context->state[3] = 0x10325476;
	context->state[4] = 0xC3D2E1F0;
	context->count[0] = context->count[1] = 0;
}

// Run your data through this.

void MicroWS_SHA1_Update(MicroWS_SHA1_CTX* context, const unsigned char* data, unsigned int len)
{
	unsigned int i, j;

	j = (context->count[0] >> 3) & 63;
	if((context->count[0] += len << 3) < (len << 3))
		context->count[1]++;
	context->count[1] += (len >> 29);
	i = 64 - j;
	while(len >= i)
	{
		memcpy(&context->buffer[j], data, i);
		MicroWS_SHA1_Transform(context->state, context->buffer);
		data += i;
		len -= i;
		i = 64;
		j = 0;
	}

	memcpy(&context->buffer[j], data, len);
}

// Add padding and return the message digest.

void MicroWS_SHA1_Final(unsigned char digest[20], MicroWS_SHA1_CTX* context)
{
	uint32_t	  i, j;
	unsigned char finalcount[8];

	for(i = 0; i < 8; i++)
	{
		finalcount[i] = (unsigned char)((context->count[(i >= 4 ? 0 : 1)] >> ((3 - (i & 3)) * 8)) & 255); // Endian independent
	}
	MicroWS_SHA1_Update(context, (unsigned char*)"\200", 1);
	while((context->count[0] & 504) != 448)
	{
		MicroWS_SHA1_Update(context, (unsigned char*)"\0", 1);
	}
	MicroWS_SHA1_Update(context, finalcount, 8); // Should cause a SHA1Transform()
	for(i = 0; i < 20; i++)
	{
		digest[i] = (unsigned char)((context->state[i >> 2] >> ((3 - (i & 3)) * 8)) & 255);
	}
	// Wipe variables
	i = j = 0;
	memset(context->buffer, 0, 64);
	memset(context->state, 0, 20);
	memset(context->count, 0, 8);
	memset(&finalcount, 0, 8);
}

#undef rol
#undef blk0
#undef blk
#undef R0
#undef R1
#undef R2
#undef R3
#undef R4

// end: SHA-1 in C

void MicroWSBase64Encode(char* pOut, const uint8_t* pIn, uint32_t nLen)
{
	static const char* CODES = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=";
	//..straight from wikipedia.
	int	  b;
	char* o = pOut;
	for(uint32_t i = 0; i < nLen; i += 3)
	{
		b	 = (pIn[i] & 0xfc) >> 2;
		*o++ = CODES[b];
		b	 = (pIn[i] & 0x3) << 4;
		if(i + 1 < nLen)
		{
			b |= (pIn[i + 1] & 0xF0) >> 4;
			*o++ = CODES[b];
			b	 = (pIn[i + 1] & 0x0F) << 2;
			if(i + 2 < nLen)
			{
				b |= (pIn[i + 2] & 0xC0) >> 6;
				*o++ = CODES[b];
				b	 = pIn[i + 2] & 0x3F;
				*o++ = CODES[b];
			}
			else
			{
				*o++ = CODES[b];
				*o++ = '=';
			}
		}
		else
		{
			*o++ = CODES[b];
			*o++ = '=';
			*o++ = '=';
		}
	}
}

template <typename T>
static T MicroWSMin(T a, T b)
{
	return a < b ? a : b;
}

template <typename T>
static T MicroWSMax(T a, T b)
{
	return a > b ? a : b;
}
template <typename T>
static T MicroWSClamp(T a, T min_, T max_)
{
	return MicroWSMin(max_, MicroWSMax(min_, a));
}

void mws_log_impl(int error, uint32_t ConnectionId, const char* fmt, ...)
{
#if MICROWS_LOG || MICROWS_DEBUG
	if(MICROWS_LOG || error)
	{
		static const size_t BUF_SIZE = 1024;
		char				Buffer[BUF_SIZE];
		uint32_t			Index  = -1;
		MWSSocket			Socket = INVALID_SOCKET;
		if(ConnectionId < MICROWS_ALL_CONNECTIONS)
		{
			Index  = ConnectionId % MICROWS_MAX_CONNECTIONS;
			Socket = S.Connections[Index].Socket;
		}

		int		l = stbsp_snprintf(Buffer, sizeof(Buffer) - 1, "MicroWS:%5x(%02x)[Sock:%d] ", ConnectionId, Index, Socket);
		char*	p = &Buffer[0] + l;
		va_list args;
		va_start(args, fmt);
		stbsp_vsnprintf(p, BUF_SIZE - l - 1, fmt, args);
		OutputDebugStringA(Buffer);
		printf(Buffer);
		va_end(args);
	}
	MWS_ASSERT(!error);
#endif
}
