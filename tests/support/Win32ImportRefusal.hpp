#ifndef LOKA_TESTS_WIN32_IMPORT_REFUSAL_HPP
#define LOKA_TESTS_WIN32_IMPORT_REFUSAL_HPP

#include <windows.h>
#include <cstring>
#include "TestVerify.hpp"

/** Replaces one named import in the statically linked test executable only.
    The scope owns restoration; no production failure-injection door is needed. */
class Win32ImportRefusal
{
public:
  Win32ImportRefusal(const char *module, const char *name, ULONG_PTR replacement)
      : slot_(findImport(module, name)), original_(0)
  {
    LOKA_VERIFY(this->slot_ != NULL);
    this->original_ = this->slot_->u1.Function;
    this->write(replacement);
  }
  ~Win32ImportRefusal()
  {
    this->write(this->original_);
  }

private:
  static IMAGE_THUNK_DATA *findImport(const char *module, const char *name)
  {
    BYTE *base = reinterpret_cast<BYTE *>(GetModuleHandleW(NULL));
    LOKA_VERIFY(base != NULL);
    const IMAGE_DOS_HEADER *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
    LOKA_VERIFY(dos->e_magic == IMAGE_DOS_SIGNATURE);
    const IMAGE_NT_HEADERS *nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
    LOKA_VERIFY(nt->Signature == IMAGE_NT_SIGNATURE);
    const DWORD rva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (!rva)
      return NULL;
    const IMAGE_IMPORT_DESCRIPTOR *entry = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR *>(base + rva);
    for (; entry->Name; ++entry)
    {
      if (_stricmp(reinterpret_cast<const char *>(base + entry->Name), module) != 0)
        continue;
      if (!entry->OriginalFirstThunk)
        return NULL;
      const IMAGE_THUNK_DATA *names = reinterpret_cast<const IMAGE_THUNK_DATA *>(base + entry->OriginalFirstThunk);
      IMAGE_THUNK_DATA *slots = reinterpret_cast<IMAGE_THUNK_DATA *>(base + entry->FirstThunk);
      for (; names->u1.AddressOfData; ++names, ++slots)
      {
        if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))
          continue;
        const IMAGE_IMPORT_BY_NAME *import =
            reinterpret_cast<const IMAGE_IMPORT_BY_NAME *>(base + names->u1.AddressOfData);
        if (std::strcmp(reinterpret_cast<const char *>(import->Name), name) == 0)
          return slots;
      }
    }
    return NULL;
  }
  void write(ULONG_PTR value)
  {
    DWORD protection = 0;
    LOKA_VERIFY(VirtualProtect(&this->slot_->u1.Function, sizeof(this->slot_->u1.Function),
                              PAGE_READWRITE, &protection));
    this->slot_->u1.Function = value;
    DWORD ignored = 0;
    LOKA_VERIFY(VirtualProtect(&this->slot_->u1.Function, sizeof(this->slot_->u1.Function),
                              protection, &ignored));
  }
  IMAGE_THUNK_DATA *slot_;
  ULONG_PTR original_;
  Win32ImportRefusal(const Win32ImportRefusal &);
  Win32ImportRefusal &operator=(const Win32ImportRefusal &);
};

#endif
