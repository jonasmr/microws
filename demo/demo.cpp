#include "microws.h"
#include <windows.h>

#include <stdio.h>

int main()
{
	MicroWSInit(13338);
	int delay = 0;
	while(true)
	{

		uint32_t Opened, Closed, Messages;
		MicroWSUpdate(&Opened, &Closed, &Messages);

		const char* msg = "hello";
		if(0 == (delay++ % 30))
		{
			printf("sent message!\n");
			MicroWSSendMessage(MICROWS_ALL_CONNECTIONS, msg, (uint32_t)strlen("hello"));
		}

		uint8_t	 Buffer[1024 + 1];
		uint32_t Read = 0;
		Read		  = MicroWSGetMessage((uint32_t)-1, Buffer, 1024);
		if(Read > 0)
		{
			if(Read > 1024)
				__debugbreak();
			Buffer[Read] = '\0';
			printf("RECV: %s\n", Buffer);
		}

		Sleep(30);
	}
	MicroWSShutdown();
}