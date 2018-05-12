
#include "fsp.h"


void *AllocatePool(uint size)
{
  EFI_PEI_SERVICES **pei_services = GetPeiServices();
  EFI_STATUS status;
  void *buffer;

  status = *pei_services->AllocatePool(pei_services, size, &buffer);
  if (status < 0)
    buffer = NULL;
  return buffer;
}
void *AllocatePool_and_memset_0(uint size)
{
  void *buffer = AllocatePool(size);
  if (buffer) {
    return memset_0(buffer, size);
  }
  return buffer;
}

EFI_STATUS PEI_Service_AllocatePool (EFI_PEI_SERVICES **PeiServices, uint size, void **buffer) {
  
  if (size > 0xFFF0) {
    return EFI_OUT_OF_RESOURCES;
  } else {
    EFI_PEI_SERVICES **pei_services = GetPeiServices();
    EFI_STATUS status;
    void *hob_buffer;

    status = *pei_services->CreateHob(pei_services, 7, size + 8, &hob_buffer);
    *buffer = hob_buffer + 8;

    return status;
  }
  
}


void *InstallPpi(EFI_PEI_PPI_DESCRIPTOR *PpiList)
{
  EFI_PEI_SERVICES **pei_services = GetPeiServices();
  return *pei_services->InstallPpi(pei_services, PpiList);
}
