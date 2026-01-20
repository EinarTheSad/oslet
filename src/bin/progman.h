#pragma once
#include "progmod.h"

#define PROGMAN_REGISTRY_MAX   16
#define PROGMAN_INSTANCES_MAX   8

void progman_init(void);
int progman_register(const progmod_t *module);
int progman_launch(const char *name);
void progman_close(uint16_t instance_id);
void progman_update_all(void);
int progman_route_event(void *form, int event);
prog_instance_t* progman_find_by_window(void *form);
int progman_is_running(const char *name);
int progman_get_registered_count(void);
const progmod_t* progman_get_registered(int index);
int progman_get_running_count(void);
prog_instance_t* progman_get_instance(int index);
