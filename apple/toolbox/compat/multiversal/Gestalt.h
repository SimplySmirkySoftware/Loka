#ifndef LOKA_TOOLBOX_MULTIVERSAL_GESTALT_H
#define LOKA_TOOLBOX_MULTIVERSAL_GESTALT_H

#include <Multiverse.h>

// Multiversal declares the native CPU type selector and every gestaltCPU*
// value but not the family selector; match the Universal Interfaces name.
enum
{
  gestaltNativeCPUfamily = 'cpuf'
};

// Multiversal names only gestalt68881 among the gestaltFPUType answers;
// match the Universal Interfaces names and values.
enum
{
  gestaltNoFPU = 0,
  gestalt68882 = 2,
  gestalt68040FPU = 3
};

#endif
