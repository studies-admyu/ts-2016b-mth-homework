#pragma once

bool init_serv_int(void (*shutdown_function)());
void cleanup_serv_int();
