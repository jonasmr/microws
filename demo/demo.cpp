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
			Version = NewVersion;
			printf("Active Connections %d\n", State.NumConnections);
		}

		const char* msg = "hello";
		if(0 == (Delay++ % 30))
		{
			char	 buffer[128];
			uint32_t Ind   = (Delay / 30);
			uint32_t Index = Ind % (State.NumConnections + 1);
			if(Index == 0)
			{
				int len = snprintf(buffer, sizeof(buffer) - 1, "broadcast %d", Ind);
				MicroWSSendMessage(MICROWS_ALL_CONNECTIONS, buffer, len);
			}
			else
			{
				int len = snprintf(buffer, sizeof(buffer) - 1, "Send %d -> %d", Ind, State.Connections[Index - 1]);
				MicroWSSendMessage(State.Connections[Index - 1], buffer, len);
			}
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