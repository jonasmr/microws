#include "microws.h"
#include <windows.h>

#include <stdio.h>

int main()
{
	MicroWSInit(13338);
	int					   Delay   = 0;
	uint32_t			   Version = (uint32_t)-1;
	MicroWSConnectionState State;
	while(true)
	{
		uint32_t MaxData;
		uint32_t NewVersion;
		MicroWSUpdate(&NewVersion, &MaxData);
		if(NewVersion != Version)
		{
			MicroWSGetState(State);
			printf("Active Connections %d\n", State.NumConnections);
		}

		const char* msg = "hello";
		if(0 == (Delay++ % 30))
		{
			printf("sent message!\n");
			for(uint32_t i = 0; i < State.NumConnections; ++i)
			{
				char buffer[128];
				int	 len = snprintf(buffer, sizeof(buffer) - 1, "direct %d", State.Connections[i]);
				MicroWSSendMessage(State.Connections[i], buffer, len);
			}
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