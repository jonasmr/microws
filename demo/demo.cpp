#include "microws.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <math.h>
#include <stdio.h>

int main()
{
	MicroWSInit(13338);
	int					   Delay   = 0;
	uint32_t			   Version = (uint32_t)-1;
	MicroWSConnectionState State;
	uint32_t			   Time = 0;
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

		{
			char  buffer[2048];
			float fTime = Time / 30.f;
			float t0	= (float)(sin(fTime) + sin(fTime * 10.0) * 0.1);
			int	  len	= snprintf(buffer, sizeof(buffer) - 1, "{\"t0\":\"%f\"}", t0);
			MicroWSSendMessage(MICROWS_ALL_CONNECTIONS, buffer, len);
			Time++;
		}

		uint8_t	 Buffer[1024 + 1];
		uint32_t Read = 0;
		Read		  = MicroWSGetMessage((uint32_t)-1, Buffer, 1024);
		if(Read > 0)
		{
			Buffer[Read] = '\0';
			printf("RECV: %s\n", Buffer);
		}
#ifdef _WIN32
		Sleep(30);
#else
		usleep(30 * 1000);
#endif
	}
	MicroWSShutdown();
}