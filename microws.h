#pragma once

#include <initializer_list>
#include <stddef.h>
#include <stdint.h>
#include <type_traits>

#ifndef MICROWS_SEND_MEMORY_PER_CONNECTION
#define MICROWS_SEND_MEMORY_PER_CONNECTION (64llu << 10llu)
#endif

#ifndef MICROWS_RECV_MEMORY_PER_CONNECTION
#define MICROWS_RECV_MEMORY_PER_CONNECTION (64llu << 10llu)
#endif

#ifndef MICROWS_MESSAGE_MAX_SIZE
#define MICROWS_MESSAGE_MAX_SIZE (8llu << 10llu)
#endif // MICROWS_MESSAGE_MAX_SIZE

#ifndef MICROWS_MAX_CONNECTIONS
#define MICROWS_MAX_CONNECTIONS (8)
#endif // MICROWS_MESSAGE_MAX_SIZE

#ifndef MAX_CONNECTIONS_PER_UPDATE
#define MAX_CONNECTIONS_PER_UPDATE 2
#endif
void MicroWSInit(uint16_t ListenPort);
void MicroWSUpdate(uint32_t* ConnectionsOpened, uint32_t* ConnectionsClosed, uint32_t* IncomingMessages);
bool MicroWSGetMessage(uint32_t Connection, uint32_t* ConnectionOut, uint64_t* OutBufferSize, uint8_t* OutBuffer);
void MicroWSSendMessage(uint32_t Connection, uint32_t BufferSize, uint8_t* Data);
void MicroWSShutdown();
