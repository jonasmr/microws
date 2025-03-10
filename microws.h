#pragma once

#include <initializer_list>
#include <stddef.h>
#include <stdint.h>
#include <type_traits>

#define MICROWS_INVALID_CONNECTION ((uint32_t)0xffffffff)
#define MICROWS_ANY_CONNECTION ((uint32_t)0xfffffffe)
#define MICROWS_ALL_CONNECTIONS ((uint32_t)0xfffffffd)

#ifndef MICROWS_BUFFER_SPACE
#define MICROWS_BUFFER_SPACE (64llu << 10llu) // must be a multiple of the page size, so we can map it twice for use as a ring buffer/
#endif

#ifndef MICROWS_MESSAGE_MAX_SIZE
#define MICROWS_MESSAGE_MAX_SIZE (8llu << 10llu)
#endif // MICROWS_MESSAGE_MAX_SIZE

#ifndef MICROWS_MAX_CONNECTIONS
#define MICROWS_MAX_CONNECTIONS (16)
#endif // MICROWS_MESSAGE_MAX_SIZE

#ifndef MAX_CONNECTIONS_PER_UPDATE
#define MAX_CONNECTIONS_PER_UPDATE 2
#endif // MAX_CONNECTIONS_PER_UPDATE

struct MicroWSConnectionState
{
	uint32_t NumConnections;
	uint32_t ConnectionVersion;
	uint32_t Connections[MICROWS_MAX_CONNECTIONS];
	uint32_t Data[MICROWS_MAX_CONNECTIONS];
};
bool	 MicroWSInit(uint16_t ListenPort);
void	 MicroWSUpdate(uint32_t* ConnectionsVersion = nullptr, uint32_t* MessageData = nullptr);
void	 MicroWSGetState(MicroWSConnectionState& State);
uint32_t MicroWSGetMessage(uint32_t Connection, uint8_t* OutBuffer, uint32_t BufferSize, uint32_t* ConnectionOut = nullptr);
bool	 MicroWSSendMessage(uint32_t Connection, const void* Data, uint32_t Size);
void	 MicroWSShutdown();
