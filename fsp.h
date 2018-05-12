
typedef uint32_t EFI_STATUS;

#define EFI_SUCCESS		0
#define EFI_INVALID_PARAMETER	0x80000002
#define EFI_UNSUPPORTED		0x80000003 /* The FSP calling conditions were not met. */
#define EFI_OUT_OF_RESOURCES	0x80000009

// This might be reserved by FSP-M. It seems to be there's a iomap resource HOB at 0xFED00000 of size 0x1000
#define FSP_DATA_ADDR (void **) 0xFED00148


enum {
  FSP_ACTION_TEMP_RAM_INIT = 1,
  FSP_ACTION_NOTIFY = 2,
  FSP_ACTION_MEMORY_INIT = 3,
  FSP_ACTION_TEMP_RAM_EXIT = 4,
  FSP_ACTION_SILICON_INIT = 5,
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


typedef struct {
  uint16_t limit;
  uint32_t base;
} IDT_s;

typedef struct {
  uint32_t Signature; // 0x00
  uint32_t Zero1;
  uint32_t StackPointer; // 0x08
  uint32_t PostCode; // 0x0C
  uint32_t Unused1[13];
  uint32_t InfoHeaderPtr; // 0x44
  uint32_t ConfigPtr; // 0z48
  uint32_t Unused2;
  uint32_t ConfigPtr2; // Gets initialized to config pointer, ends up as something else
  uint32_t Zero2;
  uint8_t Action; // 0x58
  uint8_t Unused3[31];
  uint32_t PerfSignature; // 0x78
  uint32_t Zero3;
  uint32_t TSCIndex; // 0x80
  // Last byte of the TSC is replaced by a value given by the FSP code
  uint64_t TSC[0x20]; // 0x84-0x88 + index*8
} FSP_DATA;

typedef struct
{
  char Signature[4];
  int HeaderLength;
  __int16 Reserved1;
  char SpecVersion;
  char HeaderRevision;
  int ImageRevision;
  char ImageId[8];
  int ImageSize;
  int ImageBase;
  __int16 ImageAttribute;
  __int16 ComponentAttribute;
  void *CfgRegionOffset;
  int CfgRegionSize;
  int Reserved2;
  void *TempRamInitEntryOffset;
  int Reserved3;
  void *NotifyPhaseEntryOffset;
  void *FspMemoryinitEntryOffset;
  void *TempRamExitEntryOffset;
  void *FspSiliconInitEntryOffset;
} InfoHeader;

/* 24 */
struct CPU_IO_PPI
{
  void *MemRead;
  void *MemWrite;
  void *IoRead;
  void *IoWrite;
  void *IoRead8;
  void *IoRead16;
  void *IoRead32;
  void *IoRead64;
  void *IoWrite8;
  void *IoWrite16;
  void *IoWrite32;
  void *IoWrite64;
  void *MemRead8;
  void *MemRead16;
  void *MemRead32;
  void *MemRead64;
  void *MemWrite8;
  void *MemWrite16;
  void *MemWrite32;
  void *MemWrite64;
};

/* 25 */
struct __unaligned __declspec(align(2)) PCI_CFG2_PPI
{
  void *Read;
  void *Write;
  void *Modify;
  __int16 Segment;
};

/* 23 */
struct EFI_PEI_SERVICES
{
  EFI_TABLE_HEADER Hdr;
  void *InstallPpi;
  void *ReInstallPpi;
  void *LocatePpi;
  void *NotifyPpi;
  void *GetBootMode;
  void *SetBootMode;
  void *GetHobList;
  void *CreateHob;
  void *FfsFindNextVolume;
  void *FfsFindNextFile;
  void *FfsFindSectionData;
  void *InstallPeiMemory;
  void *AllocatePages;
  void *AllocatePool;
  void *CopyMem;
  void *SetMem;
  void *ReportStatusCode;
  void *ResetSystem;
  CPU_IO_PPI *CpuIo;
  PCI_CFG2_PPI *PciCfg;
  void *FfsFindFileByName;
  void *FfsGetFileInfo;
  void *FfsGetVolumeInfo;
  void *RegisterForShadow;
  void *FindSectionData3;
  void *FfsGetFileInfo2;
  void *ResetSystem2;
};

inline PEI_SERVICE ** GetPeiServices () {
  IDT_s IDT;
  void *ptr;

  __sidt(IDT);
  ptr = IDT.base;
  return *(ptr - 4);
}
