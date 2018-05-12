#include "fsp.h"

typedef struct {
  uint16_t size; // probably ?
  uint16_t padding;
  uint32_t image_base;
  uint32_t image_size;
  uint32_t stack_base;
  uint32_t stack_size;
  uint32_t stack_base2;
  uint32_t half_stack_size;
  uint32_t half_stack_address;
  uint32_t half_stack_size_2;
} memory_init_entrypoint_argument;

EFI_STATUS fsp_memory_init(FSPM_UPD *config, uint32_t action) {
  //push get_fsp_info_header();
  //pushf;
  //cli;
  //pusha;
  //sidt;
  if (config == NULL) {
    FSP_INFO_HEADER *info = get_fsp_info_header();
    config = info->ImageBase + info->CfgRegionOffset;
  }
  edi = config->FspmArchUpd.StackBase + config->FspmArchUpd.StackSize;
  //xchg edi, esp

  return setup_fspd_and_run_entrypoint(config->FspmArchUpd->StackSize, config->FspmArchUpd->StackBase, get_fsp_image_base(),
				       memory_init_main_entrypoint/*get_fsp_image_base() + 0xa9c*/, old_esp, action);
}

void setup_fspd_and_run_entrypoint(uint32_t stack_size, uint32_t stack_base, uint32_t image_base, uint32_t entrypoint, uint32_t old_esp, uint32_t action)
{
  uint32_t extended_feature_information; // unused it seems ?
  uint16_t random_short; // unused it seems ?
  uint32_t IDT_entry[2];
  uint8_t IDT_table[0x22 * 8];
  IDT_s IDT;
  uint32_t IDT_ptr;
  IDTDescr idt_descriptor;
  uint8_t *ptr;
  FSP_DATA fspd;
  uint32_t unused_zero; // var_2A0
  int i, j;
  memory_init_entrypoint_argument entrypoint_arg;

  /* 2nd and 4th arguments are ecx which contain the stack size, but the arguments are unused and the compiler
   * knows it which is probably why it pushes ecx instead of NULL */
  get_cpuid_1_eax_and_ecx(NULL, NULL, &extended_feature_information, NULL);

  // Looks like this is used to loop on the RNG until it starts generating data, so it's initializing it basically...
  for (i = 0; i < 0x80000; i++) {
    for (i = 0; i < 10; i++) {
      if (gen_random_16(&random_short) != 0)
	goto break_rng_loop;
    }
  }
 break_rng_loop:
  initialize_FPU();
  unused_zero = 0;
  // Set the IDT to offset 0xFFFFFFE4 (so, 0x100000000 - 0x1C) with GDT selector 8 and type attributes 0x8E (Present, 32-bit interrupt gate)
  idt_descriptor.idt_1 = 0x8FFE4;
  idt_descriptor.idt_2 = 0xFFFF8E00; 
  idt_descriptor.offset_1 = (uint16_t) FSP_INFO_HEADER.ImageBase + FSP_INFO_HEADER.ImageSize - 0x1C;
  idt_descriptor.offset_2 = (uint16_t) (FSP_INFO_HEADER.ImageBase + FSP_INFO_HEADER.ImageSize - 0x1C) >> 16
  IDT_entry[0] = idt_descriptor.idt_1;
  IDT_entry[1] = idt_descriptor.idt_2;
  ptr = IDT_table;
  for (i = 0x22 ; i > 0; i--) {
    if (&IDT_entry != ptr) {
      memmove(ptr, &IDT_entry, 8);
    }
    ptr += 8;
  }
  IDT.base = IDT_table;
  IDT.limit = 0x10F;
  IDT_ptr = &IDT;
  // pushf
  // cli
  __lidt(IDT_ptr);
  // popf
  setup_fspd(&fspd, old_stack_ptr, action);
  entrypoint_arg.size = 0x24;
  entrypoint_arg.image_base = image_base;
  entrypoint_arg.image_size = image_base[0x20]; // Image size taken from the pre-info header (eufi guid stuff)
  entrypoint_arg.stack_base = stack_base;
  entrypoint_arg.stack_size = stack_size;
  entrypoint_arg.stack_base2 = stack_base;  
  entrypoint_arg.half_stack_size = stack_size * 0x32 / 0x64;
  entrypoint_arg.half_stack_address = stack_base + (stack_size * 0x32 / 0x64);
  entrypoint_arg.half_stack_size_2 = stack_size - (stack_size * 0x32 / 0x64);

  entrypoint(&entrypoint_arg, unk_ffff6E924);

  while (1);
  // This doesn't look like it's meant to return, it sets idt_ptr var to 0, then loops and checks if it changed...
  // that's an infinite loop unless it expects another thread to modify some variable within its own stack...
}


uint8_t * setup_fspd(FSP_DATA *fspd, uint32_t old_stack_ptr, char action)
{
  uint32_t *config;

  *FSP_DATA_ADDR = fspd;
  memset(fspd, 0, 0x184);
  edi = fspd;
  esi = old_stack;
  fspd->Signature = "FSPD";
  fspd->Zero1 = 0;
  fspd->StackPointer = old_stack_ptr;
  fspd->TSCIndex = 2;
  fspd->PERFSignature = 0x46524550; // "PERF"
  if (fspd->TSCIndex < 0x20) {
    // Read Time Stamp Counter (64 bits)
    fspd->TSC[fspd->TSCIndex] = (__rdtsc() & 0xFFFFFFFFFFFFFF00L) | 0xF2;
  }
  fspd->TSCIndex++;
  fspd->InfoHeaderPtr = get_fsp_info_header();
  setup_car_fspd(fspd); // more setupd of fspd struct, looks for MCUD and REP0 at the end of CAR region
  fspd->Action = action;
  config = fspd->StackPointer[0x34]; // First argument from the previous stack;
  if (config == NULL) {
    config = (InfoHeader *) (fspd->InfoHeaderPtr)->CfgRegionOffset + (InfoHeader *) (fspd->InfoHeaderPtr)->ImageBase;
  }
  fspd->ConfigPtr = config;
  fspd->ConfigPtr2 = config;
  fspd->Zero2 = 0;

  return fspd;
}

void setup_car_fspd(FSP_DATA *fspd)
{
  // This address is hardcoded in the code and it coincidently points to the end of the stack
  // that it receives from coreboot. Coreboot itself takes that value from _car_region_end
  // So it looks like this is looking backward through the CAR region
  // I have found references to 'MCUD' and 'PER0' being written in FSP-T
  // So I think this is taking the measurements that FSP-T did and storing them in the
  // FSPD now that it has been setup.
  uint32_t *ptr = 0xFEF3FFFC;
  fspd[0x30] = 0;
  fspd[0x34] = 0;
  fspd[0x38] = 0;
  fspd[0x3C] = 0;
  fspd[0x40] = 0;
  if (*0xFEF3FFF8 == 0x4455434D) { // 'MCUD'
    while (*ptr != 0) {
      if (*(ptr - 4) == 0x4455434D) { // 'MCUD'
	memmove(&fspd[0x34], (ptr-0x14), 0x10);
	ptr -= 0x18;
      } else if ((*ptr - 4) == 0x30524550) { // 'PER0' 
	memmove(&fspd[0x84], (ptr-0x14), 0x10);
	// Sets the values of the last byte in the TSC to F0 and F1 for the
	// TSC measurements at position 0 and 1
	((uint8_t *)(&fspd->TSC[0]))[7] = 0xF0;
	((uint8_t *)(&fspd->TSC[1]))[7] = 0xF1;
      } else {
	ptr -= (*ptr) * 4;
      }
    }
  }

}

int get_cpuid_1_eax_and_ecx(uint32_t *eax_ptr, uint32_t *unused1, uint32_t *ecx_ptr, uint32_t *unused2) 
{
  uint32_t *store_eax = eax_ptr; // passed as argument in edx
  uint32_t *store_ebx = NULL;
  uint32_t *store_ecx = ecx_ptr;
  uint32_t *store_edx = NULL;

  // Function takes two arguments that are unused, the store_ebx and store_edx are initialized to NULL
  // and not modified, and they are still checked later on to store ebx and edx in them
  // That code was not optimized out for some reason.
  __cpuid(0x01);

  if (store_eax) {
    *store_eax = _eax;
  }
  if (store_ebx) {
    *store_ebx = _ebx;
  }
  if (store_ecx) {
    *store_ecx = _ecx;
  }
  if (store_edx) {
    *store_edx = _edx;
  }
  return 1;
}

int gen_random_16(uint16_t *ptr) {
  uint32_t rand;
  if (__rdrand(&rand)) {
    *ptr = (uint16_t) rand;
    return 1;
  }
  return 0;
}


void memory_init_main_entrypoint(memory_init_entrypoint_argument * arg, uint32_t unk)
{
  memory_init_real_entrypoint(arg, unk, 0);
  while(1); // Inifinite loop by always checking if some stack variable has changed
}

extern EFI_PEI_SERVICES PEI_Services_Table; // "PEI SERV" revision 1.40
void memory_init_real_entrypoint(memory_init_entrypoint_argument * arg, uint32_t unk, uint32_t unk2)
{
  edi = 0xA8;
  if (unk2) {
    // to RE
  }  else {
    IDT_s IDT;
    void *ptr;
    EFI_PEI_SERVICES Pei_Service;

    memset(var_2A8, 0, 0x280);
    memmove(&Pei_Service, PEI_Services_Table, 0x88);
    __sidt(IDT);
    ptr = IDT.base;
    *(ptr - 4) = &Pei_Service

  }
}
