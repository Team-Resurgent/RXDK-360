/* <fenv.h> rounding-mode and exception-flag control (powerpc FPSCR). A real gap:
   fegetround/fesetround/feclearexcept/fetestexcept were undefined symbols until the
   libm/fenv sources were recovered for the 360, yet nothing in this suite exercised
   them -- so the suite passed while the samples that use them would fail. */
#include "rxdk_test.h"
#include <fenv.h>

/* This validates the recovered fenv *library* surface -- the read/write path to
   the FPSCR (mffs/mtfsf) via fegetround/fesetround/feclearexcept/fetestexcept.
   It deliberately does NOT assert that the FPU then rounds arithmetic per the
   selected mode, nor that inexact/overflow sticky bits get raised: those depend on
   the emulator honouring FPSCR dynamic rounding and exception reporting, which
   xenia does not, and are an emulator-fidelity question separate from whether our
   library links and round-trips the register correctly. */
int main(void) {
    /* default rounding is round-to-nearest */
    CHECK_EQI(fegetround(), FE_TONEAREST, "default rounding is FE_TONEAREST");

    /* set/get round-trip for every mode (writes then reads the FPSCR) */
    CHECK_EQI(fesetround(FE_TOWARDZERO), 0, "fesetround(FE_TOWARDZERO) ok");
    CHECK_EQI(fegetround(), FE_TOWARDZERO, "fegetround reflects FE_TOWARDZERO");
    CHECK_EQI(fesetround(FE_UPWARD), 0, "fesetround(FE_UPWARD) ok");
    CHECK_EQI(fegetround(), FE_UPWARD, "fegetround reflects FE_UPWARD");
    CHECK_EQI(fesetround(FE_DOWNWARD), 0, "fesetround(FE_DOWNWARD) ok");
    CHECK_EQI(fegetround(), FE_DOWNWARD, "fegetround reflects FE_DOWNWARD");

    /* restore default and confirm */
    CHECK_EQI(fesetround(FE_TONEAREST), 0, "restore FE_TONEAREST");
    CHECK_EQI(fegetround(), FE_TONEAREST, "fegetround restored");

    /* exception-flag clear path is exercisable regardless of the FPU: after
       feclearexcept the flags read clear (fetestexcept links + reads the FPSCR) */
    feclearexcept(FE_ALL_EXCEPT);
    CHECK_EQI(fetestexcept(FE_ALL_EXCEPT), 0, "fetestexcept clear after feclearexcept");
    CHECK_EQI(fetestexcept(FE_INEXACT), 0, "FE_INEXACT clear after feclearexcept");

    /* the core of <fenv.h>: whole-environment save/restore. fegetenv snapshots the
       FPSCR; changing the mode and then fesetenv must restore it. feholdexcept +
       feupdateenv and fesetenv(FE_DFL_ENV) are the other environment entry points. */
    fenv_t saved;
    CHECK_EQI(fegetenv(&saved), 0, "fegetenv snapshots the environment");
    CHECK_EQI(fesetround(FE_UPWARD), 0, "perturb rounding to FE_UPWARD");
    CHECK_EQI(fesetenv(&saved), 0, "fesetenv restores the snapshot");
    CHECK_EQI(fegetround(), FE_TONEAREST, "fesetenv restored rounding to default");

    fenv_t held;
    CHECK_EQI(feholdexcept(&held), 0, "feholdexcept saves env and clears flags");
    CHECK_EQI(fetestexcept(FE_ALL_EXCEPT), 0, "flags clear after feholdexcept");
    CHECK_EQI(feupdateenv(&held), 0, "feupdateenv restores the held environment");

    CHECK_EQI(fesetenv(FE_DFL_ENV), 0, "fesetenv(FE_DFL_ENV) installs the default env");
    CHECK_EQI(fegetround(), FE_TONEAREST, "default env is round-to-nearest");

    CHECK_DONE("fenv");
    return 0;
}
