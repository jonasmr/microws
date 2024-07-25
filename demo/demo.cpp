#include "microws.h"
#include <windows.h>

#include <stdio.h>
// void MicroWSInit(uint16_t ListenPort);
// void MicroWSUpdate(uint32_t* ConnectionsOpened, uint32_t* ConnectionsClosed, uint32_t* IncomingMessages);
// bool MicroWSGetMessage(uint32_t Connection, uint32_t* ConnectionOut, uint64_t* OutBufferSize, uint8_t* OutBuffer);
// void MicroWSSendMessage(uint32_t Connection, uint32_t BufferSize, uint8_t* Data);
// void MicroWSShutdown();

int main()
{
	MicroWSInit(13338);
	while(true)
	{

		uint32_t Opened, Closed, Messages;
		MicroWSUpdate(&Opened, &Closed, &Messages);

		const char* msg = "hello";
		MicroWSSendMessage((uint32_t)-1, msg, sizeof("hello"));

		uint8_t	 Buffer[1024 + 1];
		uint32_t Read = 0;
		Read		  = MicroWSGetMessage((uint32_t)-1, Buffer, 1024);
		if(Read > 0)
		{
			if(Read >= 1024)
				__debugbreak();
			Buffer[Read + 1] = '\0';
			printf("RECV: %s\n", Buffer);
		}

		Sleep(30);
	}
	MicroWSShutdown();
}