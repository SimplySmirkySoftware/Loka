#include "ToolboxOutOfMemory.hpp"
#include "ToolboxDialogConstruction.hpp"
#include "ToolboxReserveGrowZone.hpp"
#include <LowMem.h>
#include <Memory.h>
#include <Processes.h>
#include <cstring>

namespace
{
  const long kOutOfMemoryReserveBytes = 8 * 1024;
  /** Process-owned reserve and its one-way release/reporting phases. */
  class OutOfMemoryReserve
  {
  public:
    OutOfMemoryReserve()
        : block_(0),
          phase_(UNARMED)
    {
    }

    void arm()
    {
      this->block_ = NewPtr(kOutOfMemoryReserveBytes);
      this->phase_ = this->block_ ? HELD : UNARMED;
    }
    bool held() const
    {
      return this->phase_ == HELD;
    }
    bool spentForSystem() const
    {
      return this->phase_ == RELEASED_FOR_SYSTEM;
    }
    long releaseForSystem(Size cbNeeded)
    {
      if (!this->held() || cbNeeded > kOutOfMemoryReserveBytes)
        return 0;
      this->release();
      this->phase_ = RELEASED_FOR_SYSTEM;
      return kOutOfMemoryReserveBytes;
    }
    bool beginReporting()
    {
      switch (this->phase_)
      {
      case HELD:
        this->release();
        break;
      case RELEASED_FOR_SYSTEM:
        break;
      case UNARMED:
      case REPORTING:
        return false;
      }
      this->phase_ = REPORTING;
      return true;
    }

  private:
    enum Phase
    {
      UNARMED,
      HELD,
      RELEASED_FOR_SYSTEM,
      REPORTING
    };
    Ptr block_;
    Phase phase_;
    void release()
    {
      DisposePtr(this->block_);
      this->block_ = 0;
    }
    OutOfMemoryReserve(const OutOfMemoryReserve &);
    OutOfMemoryReserve &operator=(const OutOfMemoryReserve &);
  };
  OutOfMemoryReserve gOutOfMemoryReserve;

  pascal long ReleaseReserveForSystem(Size cbNeeded)
  {
    // Retro68 68K globals use absolute addresses, not A5-relative storage.
    // No SetCurrentA5 is needed; the UPP also supplies the PPC CFM entry.
    return gOutOfMemoryReserve.releaseForSystem(cbNeeded);
  }
  void ShowOutOfMemoryDialog()
  {
    const char prefix[] = "Not enough memory to keep ";
    const char suffix[] = " running. In the Finder, use Get Info to raise its memory size.";
    unsigned char text[255];
    unsigned char name[32];
    BlockMoveData(LMGetCurApName(), name, sizeof(name));
    const unsigned int fixedLength = sizeof(prefix) + sizeof(suffix) - 2;
    unsigned int nameLength = name[0];
    if (nameLength > sizeof(name) - 1)
      nameLength = sizeof(name) - 1;
    if (nameLength > sizeof(text) - fixedLength)
      nameLength = sizeof(text) - fixedLength;
    std::memcpy(text, prefix, sizeof(prefix) - 1);
    std::memcpy(text + sizeof(prefix) - 1, name + 1, nameLength);
    std::memcpy(text + sizeof(prefix) - 1 + nameLength, suffix, sizeof(suffix) - 1);
    const unsigned char textLength = static_cast<unsigned char>(fixedLength + nameLength);

    loka::toolbox::RunEmergencyQuitDialog(reinterpret_cast<const char *>(text), textLength);
  }
} // namespace

namespace loka
{
  namespace toolbox
  {
    void ArmOutOfMemoryReserve()
    {
      gOutOfMemoryReserve.arm();
      if (gOutOfMemoryReserve.held())
      {
        InstallReserveGrowZone(NewGrowZoneUPP(ReleaseReserveForSystem));
      }
    }

    void QuitIfOutOfMemoryReserveSpent()
    {
      if (gOutOfMemoryReserve.spentForSystem())
        QuitForOutOfMemory();
    }

    void QuitForOutOfMemory()
    {
      if (gOutOfMemoryReserve.beginReporting())
        ShowOutOfMemoryDialog();
      ExitToShell();
#if defined(LOKA_RETRO68)
      __builtin_unreachable();
#endif
    }
  } // namespace toolbox
} // namespace loka
