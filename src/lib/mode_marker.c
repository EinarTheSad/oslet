#ifdef AGIX_BUILD
__attribute__((section(".oslet_mode"), used))
const char __oslet_mode_marker[] = "MODE=AGIX";
#else
__attribute__((section(".oslet_mode"), used))
const char __oslet_mode_marker[] = "MODE=GIX";
#endif
