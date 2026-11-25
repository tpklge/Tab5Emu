// Selected from mgba-emu/suite, MIT; see mgba-suite/LICENSE.
void calibrate(struct Timings*);
void testNop(struct Timings*);
void testNop2(struct Timings*);
void testLdrh(struct Timings*);
void testLdrhNop(struct Timings*);
void testNopLdrh(struct Timings*);
void testLdrhRom(struct Timings*);
void testLdrhRomNop(struct Timings*);
void testLdr(struct Timings*);
void testLdrRom(struct Timings*);
void testLdrStr(struct Timings*);
void testStrh(struct Timings*);
void testStrLdr(struct Timings*);
void testStrStr(struct Timings*);
void testMul0(struct Timings*);
void testMul4(struct Timings*);
void testMul8(struct Timings*);
void testB(struct Timings*);
void testTrivialLoop(struct Timings*);
static void (*const cases[])(struct Timings*)={
calibrate,
testNop,
testNop2,
testLdrh,
testLdrhNop,
testNopLdrh,
testLdrhRom,
testLdrhRomNop,
testLdr,
testLdrRom,
testLdrStr,
testStrh,
testStrLdr,
testStrStr,
testMul0,
testMul4,
testMul8,
testB,
testTrivialLoop
};
