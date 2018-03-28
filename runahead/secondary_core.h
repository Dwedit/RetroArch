#ifndef __SECONDARY_CORE_H__
#define __SECONDARY_CORE_H__

#include "core_type.h"
#include "retro_common_api.h"
#include "boolean.h"

bool secondary_core_run_no_input_polling();
bool secondary_core_deserialize(const void *buffer, int size);
void secondary_core_destroy();
void set_last_core_type(enum rarch_core_type type);
void remember_controller_port_device(long port, long device);
void clear_controller_port_map();

#endif
