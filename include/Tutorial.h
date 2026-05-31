#pragma once
#ifndef KATANE_TUTORIAL_H
#define KATANE_TUTORIAL_H

#include "VM.h"

// Entry point called by the tutorial() native function.
// Blocks until the user exits tutorial mode.
void KTN_TutorialRun(KTN_VM* vm);

#endif
