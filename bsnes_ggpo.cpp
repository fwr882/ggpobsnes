#include "libretro.h"
#include "ggponet.h"
#include "ggpoclient.h"

GGPOSession *ggpo = NULL;

bool bNetGame = false;
char gGameName[128] = { 0 };

bool
ggpo_on_client_event_callback(GGPOClientEvent *info)
{
   switch (info->code) {
   case GGPOCLIENT_EVENTCODE_CONNECTING:
      printf("Connecting to Server...");
      break;

   case GGPOCLIENT_EVENTCODE_CONNECTED:
      printf("Connected");
      break;

   case GGPOCLIENT_EVENTCODE_RETREIVING_MATCHINFO:
      printf("Retrieving Match Info...");
      break;

   case GGPOCLIENT_EVENTCODE_DISCONNECTED:
      printf("Disconnected.");
      break;

   case GGPOCLIENT_EVENTCODE_MATCHINFO:
      printf("P1: %s P2: %s other: %s \n", info->u.matchinfo.p1, info->u.matchinfo.p2, info->u.matchinfo.blurb);
      break;

   case GGPOCLIENT_EVENTCODE_SPECTATOR_COUNT_CHANGED:
      printf("Spectator(s) %d\n", info->u.spectator_count_changed.count);
      break;

   case GGPOCLIENT_EVENTCODE_CHAT:
	   printf("%s > %s\n", info->u.chat.username, info->u.chat.text);
      break;
   }
   return true;
}

bool __cdecl
ggpo_on_event_callback(GGPOEvent *info)
{
   TCHAR status[256];

   if (ggpo_is_client_eventcode(info->code)) {
      return ggpo_on_client_event_callback((GGPOClientEvent *)info);
   }

   switch (info->code) {
   case GGPO_EVENTCODE_CONNECTED_TO_PEER:
      printf("Connected to peer.");
      break;

   case GGPO_EVENTCODE_SYNCHRONIZING_WITH_PEER:
      printf("Synchronizing with peer (%d/%d)...", info->u.synchronizing.count, info->u.synchronizing.total);
      break;

   case GGPO_EVENTCODE_RUNNING:
      // printf("\n");
      break;

   case GGPO_EVENTCODE_DISCONNECTED_FROM_PEER:
      printf("Disconnected from peer.");
      break;

   case GGPO_EVENTCODE_TIMESYNC:
      Sleep(1000 * info->u.timesync.frames_ahead / 60);
      break;
   }
   return true;
}

bool __cdecl
retro_load_game(const struct retro_game_info *info)
{
  // Support loading a manifest directly.
  core_bind.manifest = info->path && string(info->path).endsWith(".bml");
  init_descriptors();

  const uint8_t *data = (const uint8_t*)info->data;
  size_t size = info->size;
  if ((size & 0x7ffff) == 512) {
    size -= 512;
    data += 512;
  }
  retro_cheat_reset();
  if (info->path) {
    core_bind.load_request_error = false;
    core_bind.basename = info->path;

    char *posix_slash = (char*)strrchr(core_bind.basename, '/');
    char *win_slash = (char*)strrchr(core_bind.basename, '\\');
    if (posix_slash && !win_slash)
       posix_slash[1] = '\0';
    else if (win_slash && !posix_slash)
       win_slash[1] = '\0';
    else if (posix_slash && win_slash)
       max(posix_slash, win_slash)[1] = '\0';
    else
      core_bind.basename = "./";
  }

  core_interface.mode = SuperFamicomCartridge::ModeNormal;
  std::string manifest;
  if (core_bind.manifest)
    manifest = std::string((const char*)info->data, info->size); // Might not be 0 terminated.
  return snes_load_cartridge_normal(core_bind.manifest ? manifest.data() : info->meta, data, size);

  //return false;
}

bool __cdecl
retro_run(void) {
  core_bind.input_polled=false;
  SuperFamicom::system.run();
  if(core_bind.sampleBufPos) {
    core_bind.paudio(core_bind.sampleBuf, core_bind.sampleBufPos/2);
    core_bind.sampleBufPos = 0;
  }
  return true;
}

bool  __cdecl
retro_serialize(void *data, size_t size, int *checksum, int frame) {
  SuperFamicom::system.runtosave();
  serializer s = SuperFamicom::system.serialize();
  if(s.size() > size) return false;
  memcpy(data, s.data(), s.size());
  return true;
}

bool __cdecl
retro_unserialize(const void *data, size_t size) {// unsigned char *buffer, int len
  serializer s((const uint8_t*)data, size);
  return SuperFamicom::system.unserialize(s);
  //return true;
}

void __cdecl 
ggpo_free_buffer_callback(void *buffer)
{
   free(buffer);
}

void
GGPOInit(TCHAR *connect)
{
   GGPOSessionCallbacks cb = { 0 };

   bNetGame = TRUE;

   cb.begin_game = retro_load_game;
   cb.load_game_state = retro_unserialize;
   cb.save_game_state = retro_serialize;
   cb.free_buffer = ggpo_free_buffer_callback;
   cb.advance_frame = retro_run;
   cb.on_event = ggpo_on_event_callback;
   //cb.log_game_state = ggpo_log_game_state_callback;

   if (strncmp(connect, "quark:direct", strlen("quark:direct")) == 0) {
      int localPort, remotePort, p1 = 0;
      char ip[128], game[128];
      sscanf(connect, "quark:direct,%[^,],%d,%[^,],%d,%d", game, &localPort, ip, &remotePort, &p1);
      ggpo = ggpo_start_session(&cb, game, localPort, ip, remotePort, p1);
   } 

   else if (strncmp(connect, "quark:synctest", strlen("quark:synctest")) == 0) {
      int frames;
      char game[128];
      sscanf(connect, "quark:synctest,%[^,],%d", game, &frames);
      ggpo = ggpo_start_synctest(&cb, game, frames);
   }

}

bool
QuarkNetworkGame()
{
   return bNetGame;
}

void
QuarkRunIdle(int sleepTime)
{
   ggpo_idle(ggpo, sleepTime);
}

int16_t inputPoll(unsigned port, unsigned device, unsigned id) {
	if(id > 11) return 0;
	if (!input_polled) {
		pinput_poll();
		input_polled=true;
	}
	return pinput_state(port, snes_to_retro(device), 0, snes_to_retro(device, id));
}

bool
QuarkGetInput(void *values, int size, int players)
{
   return ggpo_synchronize_input(ggpo, values, size, players);
}

bool
QuarkIncrementFrame()
{
   ggpo_advance_frame(ggpo);
   return true;
}

void
QuarkSendChatText(char *text)
{
   // Later
	printf("Sending Chat Text: %s\n", text);
}

void RunMainLoop(HWND hwnd) {

	int start, next, now;
	start = next = now = timeGetTime();

	// Emulation LOOP
	while(1) {
		MSG msg = { 0 };

		// UI LOOP
		while(PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg); 
			DispatchMessage(&msg);
			if (msg.message == WM_QUIT) return;
		}

		// EMU LOOP
		now = timeGetTime();
		QuarkRunIdle(max(0, next - now - 1));

		if (now >= next) {
			// TODO: values = GetBsnesInput
			// TODO: if(QuarkGetInput(values)) return QuarkRunIdle(0);
			// TODO: SetBsnesInput(values)
			retro_run();
			QuarkIncrementFrame();
			next = now + (1000 / 60); // 60HZ
		}

	}

}


int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int)
{
	RunMainLoop(hInstance);
}
