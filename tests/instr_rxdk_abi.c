/* Executed by xenia's PowerPC interpreter via tools/gentests.py.
   Registers are zeroed, REGISTER_IN values are placed, the function runs to
   its blr, and REGISTER_OUT values are checked. These confirm that the Xbox
   360 argument convention the compiler now implements actually computes the
   right answers on a real PowerPC execution engine.

   The runner matches local symbols (" t test_"), so tests are static; "used"
   keeps the optimiser from discarding them as uncalled. */

#define TEST static __attribute__((used))

//#_ REGISTER_IN r3 5
//#_ REGISTER_IN r4 7
TEST int test_add_two(int a, int b) { return a + b; }
//#_ REGISTER_OUT r3 12

/* Six arguments, all integer: straightforward r3-r8. */
//#_ REGISTER_IN r3 1
//#_ REGISTER_IN r4 2
//#_ REGISTER_IN r5 4
//#_ REGISTER_IN r6 8
//#_ REGISTER_IN r7 16
//#_ REGISTER_IN r8 32
TEST int test_six_ints(int a, int b, int c, int d, int e, int f)
{ return a + b + c + d + e + f; }
//#_ REGISTER_OUT r3 63

/* The rule that distinguishes this ABI from the ELF one: a floating-point
   argument consumes the general purpose register sharing its positional slot,
   so c arrives in r5 rather than r4, and e in r7 rather than r5. If the
   convention were wrong these would read whatever the caller left behind. */
//#_ REGISTER_IN r3 100
//#_ REGISTER_IN r5 20
//#_ REGISTER_IN r7 3
//#_ REGISTER_IN f1 0x3FF0000000000000
//#_ REGISTER_IN f2 0x4000000000000000
TEST int test_mixed_slots(int a, float b, int c, double d, int e)
{ return a + c + e; }
//#_ REGISTER_OUT r3 123
