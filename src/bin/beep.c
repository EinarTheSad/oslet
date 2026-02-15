#include "../syscall.h"

__attribute__((section(".entry"), used))
void _start(void) {
    if (!sys_sound_detected()) {
        sys_write("Sound Blaster 16 not detected.\n");
        sys_exit();
    }

    sys_write("Playing test tones...\n\n");
    sys_sound_set_volume(15, 15);

    uint16_t frequencies[] = {262, 294, 330, 349, 392, 440, 494, 523};
    const char *notes[] = {"C4 ", "D4 ", "E4 ", "F4 ", "G4 ", "A4 ", "B4 ", "C5 "};

    for (int i = 0; i < 8; i++) {
        sys_write(notes[i]);
        sys_write(" ");
        sys_sound_play_tone(frequencies[i], 250, WAVE_SINE);
        sys_sound_play_tone(0, 25, WAVE_SINE);  /* Silence between notes */
    }

    sys_write("\nDone!\n");
    sys_exit();
}
