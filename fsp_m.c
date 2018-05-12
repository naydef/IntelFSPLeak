#include "fsp.h"

// This starts at the middle of the exit function of FSP-M. This is what gets called (returned into)
// when TempRamExit or SiliconInit get called.
EFI_STATUS into_new_stack_retvalue() {
  FSP_DATA *fsp_data = *FSP_DATA_ADDR;
  char last_tsc_byte;
  uint32_t fixed_mtrrs[0xB] = {0x250, 0x258, 0x259, 0x268, 0x269, 0x26A, 0x26B, 0x26C,
			       0x26D, 0x26E, 0x26F};

  if (fsp_data->Action == FSP_ACTION_TEMP_RAM_EXIT) {
    fsp_data->PostCode = 0xB000; // TempRamInit POST Code
    last_tsc_byte = 0xF4;
  } else {
    fsp_data->PostCode = 0x9000; // SiliconInit POST Code
    last_tsc_byte = 0xF6;
  }

  store_and_return_tsc(last_tsc_byte);
  
  if (fsp_data->Action == FSP_ACTION_TEMP_RAM_EXIT) {
    post_code(fsp_data->PostCode | 0x800); // 0xB800 TempRamInit API Entry
    sub_C4362();
    sub_C345F();
    store_and_return_tsc(0xF5);
    fsp_data->StackPointer[0x24] = 0; // Set eax in the old stack
    swap_esp_and_fsp_stack();
    fsp_data->PostCode = 0x9000; // SiliconInit POST Code
    store_and_return_tsc(0xF6);
  }
  post_code(fsp_data->PostCode | 0x800); // 0x9800 SiliconInit API Entry
  
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

  info_header = fsp_data->StackPointer[0x2C];
  if (info_header.Signature != 'FSPH')
    info_header = fsp_data->InfoHeaderPtr;

  void *ptr = info_header.ImageBase;
  upper_limit = info_header.ImageBase + info_header.ImageSize - 1;

  while (ptr < upper_limit && ptr[0x28] == '_FVH') {
    uint32_t guid[] = {0x1B5C27FE, 0x4FBCF01C, 0x1B34AEAE, 0x172A992E};

    if (*(uint16_t *)&ptr[0x34] != 0 && compare_guid(ptr+*(uint16_t *)&ptr[0x34], guid) != 0) {
      install_silicon_init_ppi(ptr, ptr[0x20]);
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
  FSP_DATA *fsp_data = *FSP_DATA_ADDR;
  uint32_t index = fsp_data->TSCIndex;
  uint64_t time;

  if (index < 0x20) {
    // Read Time Stamp Counter (64 bits)
    fsp_data->TSC[index] = (__rdtsc() & 0xFFFFFFFFFFFFFF00L) | cl;
  }
  time = fsp_data->TSC[index];
  fsp_data->TSCIndex = ++index;

  return time;
}


void install_silicon_init_ppi(void * image_base, int image_size) {
  uint32_t *Ppi = AllocatePool_and_memset_0(0x20);
  uint32_t *PpiDescriptor;
  uint8_t SiliconPpi_Guid[16] = {0xC1, 0xB1, 0xED, 0x49,
				 0x21, 0xBF, 0x61, 0x47,
				 0xBB, 0x12, 0xEB, 0x00,
				 0x31, 0xAA, 0xBB, 0x39};

  Ppi[0] = 0x8C8CE578;
  Ppi[1] = 0x4F1C8A3D;
  Ppi[2] = 0x61893599;
  Ppi[3] = 0xD32DC385;
  Ppi[4] = image_base;
  Ppi[5] = image_size;
  PpiDescriptor = AllocatePool(0xC);
  PpiDescriptor[0] = 0x80000010; // Flags
  PpiDescriptor[1] = SiliconPpi_Guid;
  PpiDescriptor[2] = Ppi;
  return InstallPpi(&PpiDescriptor);
}
