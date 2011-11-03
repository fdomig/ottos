/* ProcessManager.cpp
 * 
 * Copyright (c) 2011 The ottos project.
 *
 * This work is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 2.1 of the License, or (at your option)
 * any later version.
 * 
 * This work is distributed in the hope that it will be useful, but without
 * any warranty; without even the implied warranty of merchantability or
 * fitness for a particular purpose. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 *
 *  Created on: 27 Oct 2011
 *      Author: Thomas Bargetz <thomas.bargetz@gmail.com>
 */

asm("\t .bss _stack_pointer, 4");
asm("\t .bss _kernel_stack_pointer, 4");
asm("\t .bss _func_pointer, 4");
asm("\t .global _stack_pointer");
asm("\t .global _kernel_stack_pointer");
asm("\t .global _func_pointer");


#include <vector>

#include <ottos/const.h>

#include "Process.h"
#include "ProcessManager.h"

ProcessManager::ProcessManager() {
  current_ = PID_INVALID;
}

ProcessManager::~ProcessManager() {
}

void ProcessManager::init() {
  asm("stack_pointer_a .field _stack_pointer, 32");
  asm("kernel_stack_pointer_a .field _kernel_stack_pointer, 32");
  asm("func_pointer_a .field _func_pointer, 32");

  process_table_ = std::vector<Process *>(PROCESS_MAX_COUNT);
}

int ProcessManager::run(function_t function)
{
  return -1;
}

#include <stdio.h>
int ProcessManager::switch_process(pid_t to)
{
  // TODO(fdomig@gmail.com) must use ATOMIC_START


  if(current_ != PID_INVALID) {
    // save registers


    stack_pointer = process_table_[current_]->stack_pointer();
    printf("process_stack_pointer: %x\n", stack_pointer);

    // save kernel stack pointer
    asm("\t PUSH {r0, sp}");
    asm("\t LDR r0, kernel_stack_pointer_a");
    asm("\t ADD sp, sp, #8");
    asm("\t STR sp, [r0, #0]");
    asm("\t SUB sp, sp, #8");
    asm("\t POP {r0, sp}");

    printf("kernel_stack_pointer: %x\n", kernel_stack_pointer);
    printf("current %i\n", current_);
    printf("switching to process stack\n");

    // switch to process stack
    asm("\t LDR sp, stack_pointer_a");

    // save the process' registers
    SAVE_REGISTERS;

    // store new stack pointer of the process
    asm("\t STR sp, stack_pointer_a");

    // switch back to kernel stack
    asm("\t LDR sp, kernel_stack_pointer_a");

    printf("back in kernel stack\n");
    printf("current %i\n", current_);
    printf("new process_stack_pointer: %x\n", stack_pointer);

    // save new stack pointer of the process
    process_table_[current_]->set_stack_pointer(stack_pointer);

    // set process to ready
    process_table_[current_]->set_state(READY);
  }

  current_ = to;
  process_table_[current_]->set_state(RUNNING);

  stack_pointer = process_table_[current_]->stack_pointer();
  printf("new process_stack_pointer: %x\n", stack_pointer);

  if(process_table_[current_]->executed() != 0) {

    printf("switch to process stack\n");

    // switch to process stack
    asm("\t LDR sp, stack_pointer_a");

    // load the process' registers
    LOAD_REGISTERS;

    // switch to process
    asm("\t MOV pc, lr");

  } else {

    process_table_[current_]->mark_as_executed();

    function_t asdf = process_table_[current_]->func();
    func_pointer = (int)asdf;

    asm("\t PUSH {r0}");
    asm("\t LDR r0, func_pointer_a");
    asm("\t LDR r1, [r0, #0]");
    asm("\t POP {r0}");

    /*
    // save kernel stack pointer
    asm("\t PUSH {r0}");
    asm("\t LDR r0, kernel_stack_pointer_a");
    asm("\t ADD sp, sp, #8");
    asm("\t STR sp, [r0, #0]");
    asm("\t SUB sp, sp, #8");
    asm("\t POP {r0}");
*/

    // switch to process stack
    asm("\t LDR sp, stack_pointer_a");
    asm("\t MOV pc, r1");
  }

  // TODO(fdomig@gmail.com) must use ATOMIC_END
  return 0;
}

pid_t ProcessManager::current_process() {
  return current_;
}

pid_t ProcessManager::add(Process *proc)
{
  // TODO(fdomig@gmail.com) must use ATOMIC_START
  for(int i = 0;i<PROCESS_MAX_COUNT; i++) {
    if (process_table_[i] == NULL) {

      process_table_[i] = proc;
      proc->set_pid(static_cast<pid_t>(i));
      proc->set_state(READY);

      // allocate separate stack
      proc->set_stack_pointer(i * 64 + 12);

      return proc->pid();
    }
  }
  return PID_INVALID;
  // TODO(fdomig@gmail.com) must use ATOMIC_END
}

void ProcessManager::switch_to_kernel_stack()
{
  // save stack pointer of current process
  //if(current_ != PID_INVALID) {

  printf("saving process stack pointer\n");

    asm("\t PUSH {r0}");
    asm("\t LDR r0, stack_pointer_a");
    asm("\t ADD sp, sp, #8");
    asm("\t STR sp, [r0, #0]");
    asm("\t SUB sp, sp, #8");
    asm("\t POP {r0}");

    printf("process_stack_pointer to save: %x\n", stack_pointer);
  //}

  //asm("\t LDR sp, kernel_stack_pointer_a");
  if(current_ != PID_INVALID) {
    process_table_[current_]->set_stack_pointer(stack_pointer);
  }
}

std::vector<Process *>* ProcessManager::process_table() {
  return &process_table_;
}


