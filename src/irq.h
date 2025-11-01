#pragma once
#include <stdint.h>

void irq_install_handler(int irq, void (*handler)(void));
void irq_uninstall_handler(int irq);
void irq_invoke_from_stub(int vector);