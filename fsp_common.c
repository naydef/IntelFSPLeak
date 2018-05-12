#include "fsp.h"

#ifdef FSP_S_IMAGE
EFI_STATUS notify_phase_entry(int phase_enum) {
  return fsp_init_entry((void *) phase_enum, FSP_ACTION_NOTIFY);
}
EFI_STATUS silicon_init_entry(FSPS_UPD *upd_data) {
  return fsp_init_entry((void *) upd_data, FSP_ACTION_SILICON_INIT);
}
#else
EFI_STATUS fsp_memory_init_entry(FSPS_UPD *upd_data) {
  return fsp_init_entry((void *) upd_data, FSP_ACTION_MEMORY_INIT);
}
EFI_STATUS temp_ram_exit_entry(FSPS_UPD *upd_data) {
  return fsp_init_entry((void *) upd_data, FSP_ACTION_TEMP_RAM_EXIT);
}
#endif

EFI_STATUS fsp_init_entry(void *arg, uint32_t action) {
  // push  eax
  // add   esp, 4
  // cmp   eax, [esp-4]
  // This looks like it pushes something on the stack, then pops/drops it then compares the value with
  // the content of memory where the stack was. It seems to be used to either verify if the stack
  // is setup correcrly or to verify that the stack is indeed growing right to left.. I'm not sure
  // The error it returns if it fails is EFI_UNSUPPORTED which is defined as "FSP calling conditions
  // were not met" so it might be to check the calling conditions somehow ?
  uint32_t store_action = action;
  EFI_STATUS status;

  if (action != store_action)
    return EFI_UNSUPPORTED;

  status = validate_parameters(action, arg);
  if (status != 0)
    return status;

  if (action == FSP_ACTION_MEMORY_INIT) {
#ifdef FSP_M_IMAGE
    return fsp_memory_init(arg, action);
#else
    hang_infinite_loop();
    return 0;
#endif
  } else {
    return switch_stack_and_run(arg, get_fsp_info_header());
  }
}

// Just gets the current FSP module's InfoHeader address (offset 0x94 into the FSP module).
// Relocation-safe
#ifdef FSP_S
void * get_fsp_info_header() {
  uint32_t stack = 0xFFF40244;
  // call $+5
  // pop eax
  // sub eax, 0xFFF40244
  // Uses the above to store the offset of the code in case the code was relocated
  stack -= 0xFFF40244; // stack = 0;
  stack += 0xFFF4023F;
  return stack - 0x1AB; // 0xFFF40094 for FSP-S if not relocated
}
#else
// The values here are of course independent on the build, the address of the 'pop eax' is what
// gets substracted, the address of the function itself is what gets added and the difference
// between the offset 0x94 and the offset of the function is what gets substracted at the end.
void * get_fsp_info_header() {
  uint32_t stack = 0xFFF6E294;
  // call $+5
  // pop eax
  // sub eax, 0xFFF6E294
  // Uses the above to store the offset of the code in case the code was relocated
  stack -= 0xFFF6E294; // stack = 0;
  stack += 0xFFF6E28F;
  return stack - 0x1FB; // 0xFFF6E094 for FSP-M if not relocated
#endif

void hang_inifinite_loop() {
  while(1);
}

EFI_STATUS validate_parameters(uint8_t action, void *arg) {
  // Looks like "mov edi, ds:0xFED00148" moves the value pointed by 0xFED00148 into edi
  // Because later, 'edi' itself is compared against NULL and 0xFFFFFFFF
  FSP_DATA *fsp_data = *FSP_DATA_ADDR;

  if (action == FSP_ACTION_NOTIFY || action == FSP_ACTION_TEMP_RAM_EXIT) {
    if (fsp_data == NULL || fsp_data == 0xFFFFFFFF || fsp_data->Signature != 0x44505446 /* 'FSPD' */)
      return EFI_UNSUPPORTED;
    fsp_data->Action = action;
  } else if (action == FSP_ACTION_MEMORY_INIT) {
    if (fsp_data != 0xFFFFFFFF)
      return EFI_UNSUPPORTED;
    if (validate_upd_config(3, arg) < 0)
      return EFI_INVALID_PARAMETERS;
  } else if (action == FSP_ACTION_SILICON_INIT) {
    if (fsp_data == NULL || fsp_data == 0xFFFFFFFF || fsp_data->Signature != 0x44505446 /* 'FSPD' */)
      return EFI_UNSUPPORTED;
    if (validate_upd_config(5, arg) < 0)
      return EFI_INVALID_PARAMETERS;
    fsp_data->Action = action;
  }
  return EFI_SUCCESS;
}

EFI_STATUS validate_upd_config(uint8_t action, void *arg) {
  if (action == FSP_ACTION_MEMORY_INIT) {
    FSPM_UPD *upd = (FSPM_UPD *) arg;
    if (upd == NULL)
      return EFI_SUCCESS;
    if (upd->FspUpdHeader.Signature != 0x4D5F4450554C424B /* 'KBLUPD_M' */) 
      return EFI_INVALID_PARAMETERS;
    if (upd->FspmArchUpd.StackBase == NULL)
      return EFI_INVALID_PARAMETERS;
    if (upd->FspmArchUpd.StackSize < 0x26000)
      return EFI_INVALID_PARAMETERS;
    if (upd->FspmArchUpd.BootloaderTolumSize & 0xFFF)
      return EFI_INVALID_PARAMETERS;
  } else if (action == FSP_ACTION_SILICON_INIT) {
    FSPS_UPD *upd = (FSPS_UPD *) arg;
    if (upd == NULL)
      return EFI_INVALID_PARAMETERS;
    if (upd->FspUpdHeader.Signature != 0x535F4450554C424B /* 'KBLUPD_S' */) 
      return EFI_INVALID_PARAMETERS;
  }
  return EFI_SUCCESS;
}

uint32_t save_fspd_stack(uint32_t esp)
{
  FSP_DATA *fsp_data = *FSP_DATA_ADDR;
  uint32_t ret;

  ret = fsp_data->StackPointer;
  fsp_data->StackPointer = esp;
  return ret;
}

void switch_stack_and_run(void *arg, FSP_INFO_HEADER *fsp_info_header) {
  register int esp asm ("esp");
  //push fsp_info_header;
  //pushf;
  //cli;
  //pusha;
  //sidt
  esp = save_fspd_stack(esp);
  //lidt
  //popa
  //popf
  //pop
  return; // This will return into whatever return address was on the new stack
  // into_new_stack_retvalue();
}
