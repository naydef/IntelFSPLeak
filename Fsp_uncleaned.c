
typedef uint32_t EFI_STATUS;

#define EFI_SUCCESS		0
#define EFI_INVALID_PARAMETER	0x80000002
#define EFI_UNSUPPORTED		0x80000003 /* The FSP calling conditions were not met. */

// This might be reserved by FSP-M. It seems to be there's a iomap resource HOB at 0xFED00000 of size 0x1000
#define FSP_DATA_ADDR (void **) 0xFED00148


enum {
  FSP_ACTION_TEMP_RAM_INIT = 1,
  FSP_ACTION_NOTIFY = 2,
  FSP_ACTION_MEMORY_INIT = 3,
  FSP_ACTION_TEMP_RAM_EXIT = 4,
  FSP_ACTION_SILICON_INIT = 5,
}

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

void * get_fsp_info_header() {
  uint32_t stack = 0xFFF40244;
  // call $+5
  // pop eax
  // sub eax, 0xFFF40244
  // Uses the above to store the offset of the code in case the code was relocated
  stack -= 0xFFF40244; // stack = 0;
  stack += 0xFFF4023F;
  return stack - 0x1AB; // 0xFFF40094
}

void hang_inifinite_loop() {
  while(1);
}

EFI_STATUS validate_parameters(uint8_t action, void *arg) {
  // Looks like "mov edi, ds:0xFED00148" moves the value pointed by 0xFED00148 into edi
  // Because later, 'edi' itself is compared against NULL and 0xFFFFFFFF
  void *fsp_data = *FSP_DATA_ADDR;

  if (action == FSP_ACTION_NOTIFY || action == FSP_ACTION_TEMP_RAM_EXIT) {
    if (fsp_data == NULL || fsp_data == 0xFFFFFFFF || *fsp_data != 0x44505446 /* 'FSPD' */)
      return EFI_UNSUPPORTED;
    fsp_data[0x58] = action;
  } else if (action == FSP_ACTION_MEMORY_INIT) {
    if (fsp_data != 0xFFFFFFFF)
      return EFI_UNSUPPORTED;
    if (validate_upd_config(3, arg) < 0)
      return EFI_INVALID_PARAMETERS;
  } else if (action == FSP_ACTION_SILICON_INIT) {
    if (fsp_data == NULL || fsp_data == 0xFFFFFFFF || *fsp_data != 0x44505446 /* 'FSPD' */)
      return EFI_UNSUPPORTED;
    if (validate_upd_config(5, arg) < 0)
      return EFI_INVALID_PARAMETERS;
    fsp_data[0x58] = action;
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
  uint32_t *fsp_data = *FSP_DATA_ADDR;
  uint32_t ret;

  ret = fsp_data[8];
  fsp_data[8] = esp;
  return ret;
}

EFI_STATUS switch_stack_and_run(void *arg, FSP_INFO_HEADER *fsp_info_header) {
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
  return into_new_stack_retvalue();
}

EFI_STATUS into_new_stack_retvalue() {
  uint32_t *fsp_data = *FSP_DATA_ADDR;
  char last_tsc_byte;
  uint32_t fixed_mtrrs[0xB] = {0x250, 0x258, 0x259, 0x268, 0x269, 0x26A, 0x26B, 0x26C,
			       0x26D, 0x26E, 0x26F};

  if (fsp_data[0x58] == FSP_ACTION_TEMP_RAM_EXIT) {
    fsp_data[0xC] = 0xB000; // TempRamInit POST Code
    last_tsc_byte = 0xF4;
  } else {
    fsp_data[0xC] = 0x9000; // SiliconInit POST Code
    last_tsc_byte = 0xF6;
  }

  store_and_return_tsc(last_tsc_byte);
  
  if (fsp_data[0x58] == FSP_ACTION_TEMP_RAM_EXIT) {
    post_code(fsp_data[0xC] | 0x800); // 0xB800 TempRamInit API Entry
    sub_C4362();
    sub_C345F();
    store_and_return_tsc(0xF5);
    fsp_data[0x8][0x24] = 0; // Set eax in the old stack
    swap_esp_and_fsp_stack();
    fsp_data[0xC] = 0x9000; // SiliconInit POST Code
    store_and_return_tsc(0xF6);
  }
  post_code(fsp_data[0xC] | 0x800); // 0x9800 SiliconInit API Entry
  
  int mtrr_index = 0;
  while (rdmsr(fixed_mtrr[mtrr_index]) == 0) {
    mtrr_index++;
    if (mtrr_index >= 0xB) {
      int mtrrcap = rdmsr(IA32_MTRRCAP); // 0xFE;
      int num_mttr = (mtrrcap & 0xFF) * 2;

      if (num_mttr) {
	mttr_index = 0;
	while (mttr_index rdmsr(0x200 + mttr_index) == 0) {
	  mttr_index++;
	  if (mttr_index >= num_mttr) {
	    sub_C345F();
	  }
	}
      } else{
	sub_C345F();
      }
    }
  }

  info_header = fsp_data[8][0x2C];
  if (info_header.Signature != 'FSPH')
    info_header = fsp_data[0x44];

  ptr = info_header.ImageBase;
  upper_limit = info_header.ImageBase + info_header.ImageSize - 1;

  while (ptr < upper_limit && ptr[0x28] == '_FVH') {
    uint32_t guid[] = {0x1B5C27FE, 0x4FBCF01C, 0x1B34AEAE, 0x172A992E};

    if (*(uint16_t *)&ptr[0x34] != 0 && compare_guid(ptr+*(uint16_t *)&ptr[0x34], guid) != 0) {
      sub_C3A14(ptr, ptr[0x20]);
    }
    ptr += ptr[0x20];
  }
  return 0;
}

bool compare_guid(uint32_t *ptr, uint32_t *guid) {
  return (ptr[0] == guid[0] &&
	  ptr[1] == guid[1] &&
	  ptr[2] == guid[2] &&
	  ptr[3] == guid[3]);
}

uint64_t store_and_return_tsc(char last_byte) {
  uint32_t *fsp_data = *FSP_DATA_ADDR;
  uint32_t index = fsp_data[0x80];
  uint64_t time;

  if (index < 0x20) {
    // Read Time Stamp Counter (64 bits)
    *(uint64_t *)(&fsp_data[0x84+index]) = (__rdtsc() & 0xFFFFFFFFFFFFFF00L) | cl;
  }
  time = *(uint64_t *)(&fsp_data[0x84+index]);
  fsp_data[0x80] = ++index;

  return time;
}

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
		  get_fsp_image_base() + 0xa9c, old_esp, action);
}

typedef union {
  struct {
    uint16_t offset_1; // offset bits 0..15
    uint16_t selector; // a code segment selector in GDT or LDT
    
    uint8_t zero;      // unused, set to 0
    uint8_t type_attr; // type and attributes, see below
    uint16_t offset_2; // offset bits 16..31
  };
  struct {
    uint32_t idt_1;
    uint32_t idt_2;
  }
} IDTDescr;

void setup_fspd_and_run_entrypoint(uint32_t stack_size, uint32_t stack_base, uint32_t image_base, uint32_t entrypoint, uint32_t old_esp, uint32_t action)
{
  uint32_t extended_feature_information; // unused it seems ?
  uint16_t random_short; // unused it seems ?
  uint32_t IDT_entry[2];
  uint8_t IDT_table[0x22 * 8];
  struct {
    uint16_t limit;
    uint32_t base;
  } IDT;
  uint32_t IDT_ptr;
  IDTDescr idt_descriptor;
  uint8_t *ptr;
  int i, j;

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
  var_2A0 = 0;
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
  edx = old_stack_ptr;
  ecx = &var_188;
  setup_fspd(&var_188, old_stack_ptr, action);
  struct {
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
  memory_init_entrypoint_argument.size = 0x24;
  memory_init_entrypoint_argument.image_base = image_base;
  memory_init_entrypoint_argument.image_size = image_base[0x20]; // Image size taken from the pre-info header (eufi guid stuff)
  memory_init_entrypoint_argument.stack_base = stack_base;
  memory_init_entrypoint_argument.stack_size = stack_size;
  memory_init_entrypoint_argument.stack_base2 = stack_base;  
  memory_init_entrypoint_argument.half_stack_size = stack_size * 0x32 / 0x64;
  memory_init_entrypoint_argument.half_stack_address = stack_base + (stack_size * 0x32 / 0x64);
  memory_init_entrypoint_argument.half_stack_size_2 = stack_size - (stack_size * 0x32 / 0x64);

  entrypoint(&memory_init_entrypoint_argument, unk_ffff6E924);

  while (1);
  // This doesn't look like it's meant to return, it sets idt_ptr var to 0, then loops and checks if it changed...
  // that's an infinite loop unless it expects another thread to modify some variable within its own stack...
}


uint8_t * setup_fspd(uint8_t *fspd, uint32_t old_stack_ptr, char action)
{
  uint32_t *config;

  *FSP_DATA_ADDR = fspd;
  memset(fspd, 0, 0x184);
  edi = fspd;
  esi = old_stack;
  fspd[0] = "FSPD";
  fspd[4] = 0;
  fspd[8] = old_stack_ptr;
  fspd[0x80] = 2;
  fspd[0x78] = "PREF";
  if (fspd[0x80] < 0x20) {
    // Read Time Stamp Counter (64 bits)
    *(uint64_t *)(&fspd[0x84+fspd[0x80]]) = (__rdtsc() & 0xFFFFFFFFFFFFFF00L) | 0xF2;
  }
  fspd[0x80]++;
  fspd[0x44] = get_fsp_info_header();
  sub_FFF6E526(fspd); // more setupd of fspd struct, looks for MCUD and REP0 at 0xFEF3FFF8 address ?
  fspd[0x58] = action;
  config = fspd[8][0x34]; // First argument from the previous stack;
  if (config == NULL) {
    config = fspd[0x44].CfgRegionOffset + fspd[0x44].ImageBase;
  }
  fspd[0x48] = config;
  fspd[0x50] = config;
  fspd[0x54] = 0;

  return fspd;
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
  _cpuid(0x01);

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
  if (_rdrand(&rand)) {
    *ptr = (uint16_t) rand;
    return 1;
  }
  return 0;
}

