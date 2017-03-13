/*
** © Copyright IBM Corporation 2016, 2017
** LICENSE: MIT License (https://opensource.org/licenses/mit-license.html)
** AUTHOR: Mark Stoodley
 */

/*
** Just In Time compiler for the lpeg pattern matching runtime
** Using the Eclipse OMR JitBuilder technology.
 */

//#define DEBUG

#include <limits.h>
#include <string.h>

#include "Jit.hpp"
#include "ilgen/IlBuilder.hpp"
#include "ilgen/MethodBuilder.hpp"
#include "ilgen/BytecodeBuilder.hpp"
#include "ilgen/TypeDictionary.hpp"
#include "ilgen/VirtualMachineState.hpp"

extern "C" {
#include "lpvm.h"
#include "lua.h"
#include "lauxlib.h"
#include "lptypes.h"
#include "lptree.h"
#if defined(DEBUG)
#include "lpprint.h"
#endif
}
#include "lpjit.hpp"


struct lua_State;
namespace TR { class Compilation; }

#define getoffset(p)	(((p) + 1)->offset)

static const char *const opcode_names[] = {
  "any", "char", "set",
  "testany", "testchar", "testset",
  "span", "behind",
  "ret", "end",
  "choice", "jmp", "call", "open_call",
  "commit", "partial_commit", "back_commit", "failtwice", "fail", "giveup",
   "fullcapture", "opencapture", "closecapture", "closeruntime"
};

extern "C" Capture * helperCloseRuntime(lua_State *L, const char *o, const char *s, const char *e,
                                        Capture *capture, int captop, int capsize, int ndyncap, int ptop, int *results);
extern "C" int removedyncap (lua_State *L, Capture *capture, int level, int last);
extern "C" Capture *doublecap (lua_State *L, Capture *cap, int captop, int ptop);

typedef struct Stack {
  TR::IlValue *s;
  TR::BytecodeBuilder *p;
  TR::IlValue *caplevel;
} Stack;

// Ideally, this class would leverage OMR::VirtualMachineOperandStack, but that class
// currently cannot have multiple entries, whereas lpeg needs to have s, p, caplevel
// entries for each stack operand. Should really generalize VirtualMachineOperandStack
// to support that and then extend it here.
class LpState : public OMR::VirtualMachineState {
  public:
  LpState() : _stackTop(0), _stackMax(MAXBACK) {
    _stack = new Stack[_stackMax];  // memory leak!
    memset(_stack, 0, _stackMax * sizeof(Stack));
  }

  LpState(LpState *other) : _stackTop(other->_stackTop), _stackMax(other->_stackMax) {
    _stack = new Stack[_stackMax];  // memory leak!
    for (int32_t e=0;e < _stackTop;e++) {
      _stack[e].s = other->_stack[e].s;
      _stack[e].p = other->_stack[e].p;
      _stack[e].caplevel = other->_stack[e].caplevel;
    }
  }

  //not using Commit or Reload, so just inherit base class empty implementations

  virtual OMR::VirtualMachineState *MakeCopy() {
    return new LpState(this);
  }

  virtual void MergeInto(OMR::VirtualMachineState *otherVMState, TR::IlBuilder *b) {
    LpState *other = (LpState *)otherVMState;
    for (int32_t i=_stackTop-1;i >= 0;i--) {
      b->StoreOver(other->_stack[i].s, _stack[i].s);
    }
  }

  void checkMax() {
    if (_stackTop == _stackMax)
      grow();
  }

  void grow() {
    int32_t origBytes = _stackMax * sizeof(Stack);

    int32_t newMax = _stackMax + (_stackMax >> 1);
    int32_t newBytes = newMax * sizeof(Stack);
    Stack * newStack = new Stack[newMax];   // memory leak!

    memset(newStack, 0, newBytes);
    memcpy(newStack, _stack, origBytes);

    _stack = newStack;
    _stackMax = newMax;
  }

  Stack *_stack;
  int32_t _stackTop;
  int32_t _stackMax;
};


// Stack accessor convenience macros:

#define STACK(b)           (((LpState *)((b)->vmState()))->_stack)
#define STACKTOP(b)        (((LpState *)((b)->vmState()))->_stackTop)

#define PUSH(b,news,newp,newcaplevel)                   \
  do {                                                  \
    ((LpState *)((b)->vmState()))->checkMax();          \
    int32_t t = STACKTOP(b);                            \
    STACK(b)[t].s = (news);                             \
    STACK(b)[t].p = (newp);                             \
    STACK(b)[t].caplevel = (newcaplevel);               \
    STACKTOP(b)++;                                      \
  } while (0)

#define POP_S(b)           (STACK(b)[(--STACKTOP(b))].s)
#define POP_CAPLEVEL(b)    (STACK(b)[(--STACKTOP(b))].caplevel)

#define TOP_S(b)           (STACK(b)[STACKTOP(b)-1].s)
#define TOP_CAPLEVEL(b)    (STACK(b)[STACKTOP(b)-1].caplevel)
#define TOP_BUILDER(b)     (STACK(b)[STACKTOP(b)-1].p)

#define DROP(b)            (STACKTOP(b)--)

#define EMPTY(b)           (STACKTOP(b) == 0)

#if defined(DEBUG)
static void
printBytecode(const Instruction *op, const Instruction *p, char *s, Capture *capture, int ndyncap, int captop) {
  printinst(op, p);
  printf("s: >%s<\n", s);
  printf("ndyncap:%d, captop:%d  ", ndyncap, captop);
  printcaplist(capture, capture + captop);
}
#endif


void
LpMatcher::doEnd(TR::BytecodeBuilder *b) {
  b->Store("cap",
  b->  IndexAt(pCaptureType,
  b->    Load("capture"),
  b->    Load("captop")));

  b->StoreIndirect("Capture", "kind",
  b->  Load("cap"),
  b->  ConstInteger(Byte, Cclose));

  b->StoreIndirect("Capture", "s",
  b->  Load("cap"),
  b->  ConstAddress(0));

  doRet(b);
}

void
LpMatcher::doGiveUp(TR::BytecodeBuilder *b) {
  b->Store("s",
  b->  ConstAddress(0));

  doRet(b);
}

void
LpMatcher::doRet(TR::BytecodeBuilder *b) {
  b->Return(
  b->  Load("s"));
}

void
LpMatcher::anyCommon(TR::BytecodeBuilder *b, TR::BytecodeBuilder *targetBuilder) {
  b->IfCmpGreaterOrEqual(targetBuilder,
  b->  Load("s"),
  b->  Load("e"));
}

void
LpMatcher::doAny(TR::BytecodeBuilder *b, TR::BytecodeBuilder *fallThroughBuilder) {
  TR::BytecodeBuilder *failBuilder = OrphanBytecodeBuilder(b->bcIndex(), b->name());
  anyCommon(b, failBuilder);
  FailBuilder(failBuilder);

  incrementBy1(b, (char *)"s");
  b->AddFallThroughBuilder(fallThroughBuilder);
}

void
LpMatcher::doTestAny(TR::BytecodeBuilder *b, TR::BytecodeBuilder *fallThroughBuilder, TR::BytecodeBuilder *targetBuilder) {
  anyCommon(b, targetBuilder);
  b->AddFallThroughBuilder(fallThroughBuilder);
}

void
LpMatcher::charCommon(TR::BytecodeBuilder *b, char aux, TR::BytecodeBuilder *targetBuilder) {
  b->IfCmpNotEqual(targetBuilder,
  b->  LoadAt(pInt8,
  b->    Load("s")),
  b->  ConstInt8(aux));

  b->IfCmpGreaterOrEqual(targetBuilder,
  b->  Load("s"),
  b->  Load("e"));
}

void
LpMatcher::doChar(TR::BytecodeBuilder *b, char aux, TR::BytecodeBuilder *fallThroughBuilder) {
  TR::BytecodeBuilder *failBuilder = OrphanBytecodeBuilder(b->bcIndex(), b->name());
  charCommon(b, aux, failBuilder);
  incrementBy1(b, (char *)"s");
  FailBuilder(failBuilder);
  b->AddFallThroughBuilder(fallThroughBuilder);
}

void
LpMatcher::doTestChar(TR::BytecodeBuilder *b, char aux, TR::BytecodeBuilder *fallThroughBuilder, TR::BytecodeBuilder *targetBuilder) {
  charCommon(b, aux, targetBuilder);
  b->AddFallThroughBuilder(fallThroughBuilder);
}

int
LpMatcher::analyzeSetAsRange(uint8_t *buf, uint16_t *left, uint16_t *right) {
  uint16_t c=0;

  if (TraceEnabled_log()) {
    TraceIL_log("analyzeSetAsRange: buf[] is\n");
    for (c=0;c < 256;) {
      TraceIL_log("%3d:	%d", c, testchar(buf, c));
      uint16_t l=c+16;
      for (c++;c < l;c++) {
        TraceIL_log(" %d", (testchar(buf, c) != 0));
      }
      TraceIL_log("\n");
    }
    TraceIL_log("\n");
  }

  // skip zeros to find left edge of character set
  c = 0;
  while (c < 256 && testchar(buf, c) == 0) {
    c++;
  }
  uint16_t leftEdge = c;
  if (TraceEnabled_log()) {
    TraceIL_log("analyze buf %p : leftEdge = %d %d\n", buf, c, leftEdge);
  }

  // skip ones to find right edge of character set
  while (c < 256 && testchar(buf, c) != 0)
    c++;
  uint16_t rightEdge = c;
  if (TraceEnabled_log()) {
    TraceIL_log("analyze buf %p : rightEdge = %d %d\n", buf, c, rightEdge);
  }

  // make sure no more ones after right edge
  while (c < 256 && testchar(buf, c) == 0)
    c++;

  // if we match this pattern, can test set using left and right edge
  if (c == 256) {
    if (TraceEnabled_log()) {
      TraceIL_log("analyze buf %p: found a range!\n");
    }
    *left = leftEdge;
    *right = rightEdge;
    return true;
  }

  if (TraceEnabled_log()) {
    TraceIL_log("analyze buf %p: not a range, found new 1 at %d\n", buf, c);
  }

  // cannot match pattern with left and right edge, use lookup
  return false;
}

TR::IlValue *
LpMatcher::TestChar(TR::IlBuilder *b, uint8_t *buf, TR::IlValue *c) {
  uint16_t left = 0, right=0;
  if (analyzeSetAsRange(buf, &left, &right)) {
    if (left == 256) // nothing will match, probably won't see this case much
      return b->ConstInteger(Word, 0);
    else if (left == 0 && right == 256) // probably won't see this case much either
      return b->ConstInteger(Word, 1);

    // else we can do the range check with an unsigned compare
    TR::IlValue *inRange = b->UnsignedLessThan(
                           b->   Sub(
                                    c,
                           b->      ConstInt8((uint8_t)left)),
                           b->   ConstInt8((uint8_t)(right-left)));
    return inRange;
  }
  else {
    TR::IlValue *intChar = b->UnsignedConvertTo(Int, c);
    TR::IlValue *index = b->ShiftR(intChar, b->ConstInteger(Int, 3));
    TR::IlValue *value = b->ConvertTo(Int,
                         b->  LoadAt(pInt8,
                         b->    IndexAt(pInt8, b->ConstAddress(buf), index)));
        
    TR::IlValue *mask = b->And(intChar, b->ConstInteger(Int, 7));
    TR::IlValue *bit = b->ShiftL(b->ConstInteger(Int, 1), mask);
    return b->And(value, bit);
  }
}

void
LpMatcher::setCommon(TR::BytecodeBuilder *b, uint8_t *buf, TR::BytecodeBuilder *thenBuilder, TR::BytecodeBuilder *elseBuilder) {
  b->Store("c",
  b->  LoadAt(pInt8,
  b->    Load("s")));

  b->IfCmpEqualZero(elseBuilder, TestChar(b, buf, b->Load("c")));

  // assuming usually s < e, branch away to elseBuilder
  b->IfCmpGreaterOrEqual(elseBuilder,
  b->  Load("s"),
  b->  Load("e"));

  if (thenBuilder != NULL)
     b->Goto(thenBuilder);
}

void
LpMatcher::doSet(TR::BytecodeBuilder *b, uint8_t *buf, TR::BytecodeBuilder *targetBuilder) {
  TR::BytecodeBuilder *failBuilder = OrphanBytecodeBuilder(b->bcIndex(), b->name());
  setCommon(b, buf, NULL, failBuilder);
  FailBuilder(failBuilder);

  incrementBy1(b, (char *)"s");
  b->Goto(targetBuilder);
}

void
LpMatcher::doTestSet(TR::BytecodeBuilder *b, uint8_t * buf, TR::BytecodeBuilder *targetBuilder, TR::BytecodeBuilder *elseBuilder) {
  setCommon(b, buf, targetBuilder, elseBuilder);
}

void
LpMatcher::doBehind(TR::BytecodeBuilder *b, int n, TR::BytecodeBuilder *fallThroughBuilder) {
  TR::BytecodeBuilder *fail = OrphanBytecodeBuilder(b->bcIndex(), b->name());
  b->IfCmpGreaterThan(fail,
  b->  ConstInteger(Word, n),
  b->  Sub(
  b->    ConvertTo(Word,
  b->      Load("s")),
  b->    ConvertTo(Word,
  b->      Load("o"))));

  FailBuilder(fail);

  b->Store("s",
  b->  Sub(
  b->    Load("s"),
  b->    ConstInteger(Word, n)));

  b->AddFallThroughBuilder(fallThroughBuilder);
}

void
LpMatcher::doSpan(TR::BytecodeBuilder *b, uint8_t * buf, TR::BytecodeBuilder *fallThroughBuilder) {
  b->Store("keepGoing",
  b->  LessThan(
  b->    Load("s"),
  b->    Load("e")));

  TR::IlBuilder *loop = NULL, *breakBuilder = NULL;
  b->WhileDoLoopWithBreak("keepGoing", &loop, &breakBuilder); {
    loop->Store("c",
    loop->  LoadAt(pInt8,
    loop->    Load("s")));

    loop->IfCmpEqualZero(breakBuilder, TestChar(loop, buf, loop->Load("c")));

    incrementBy1(loop, (char *)"s");

    loop->Store("keepGoing",
    loop->  LessThan(
    loop->    Load("s"),
    loop->    Load("e")));
  }
  b->AddFallThroughBuilder(fallThroughBuilder);
}

void
LpMatcher::FailBuilder(TR::BytecodeBuilder *failBuilder) {
  if (EMPTY(failBuilder)) {
    failBuilder->Store("s",
    failBuilder->  ConstAddress(0));
    doRet(failBuilder);
  }
  else {
    TR::IlValue *top_s = TOP_S(failBuilder);
    TR::IlValue *top_caplevel = TOP_CAPLEVEL(failBuilder);
    TR::BytecodeBuilder *top_p = TOP_BUILDER(failBuilder);

    DROP(failBuilder);

    failBuilder->Store("s", top_s);

    TR::IlBuilder *dynCapHandler = NULL;
    failBuilder->IfThen(&dynCapHandler,
    failBuilder->  GreaterThan(
    failBuilder->    Load("ndyncap"),
    failBuilder->    ConstInteger(Int, 0)));

    dynCapHandler->Store("ndyncap",
    dynCapHandler->  Sub(
    dynCapHandler->    Load("ndyncap"),
    dynCapHandler->    Call("removedyncap", 4,
    dynCapHandler->         Load("L"),
    dynCapHandler->         Load("capture"),
                            top_caplevel,
    dynCapHandler->         Load("captop"))));

    failBuilder->Store("captop", top_caplevel);
    failBuilder->Goto(top_p);
  }
}

// assumes capture[captop] entry has been stored into variable called "cap"
void
LpMatcher::pushCapture(TR::BytecodeBuilder *b, unsigned short key, byte kind, TR::BytecodeBuilder *fallThroughBuilder) {
  b->StoreIndirect("Capture", "idx",
  b->  Load("cap"),
  b->  ConstInteger(Short, key));

  b->StoreIndirect("Capture", "kind",
  b->  Load("cap"),
  b->  ConstInteger(Byte, kind));

  b->Store("captop",
  b->  Add(
  b->    Load("captop"),
  b->    ConstInteger(Int, 1)));

  TR::IlBuilder *growCapture = NULL;
  b->IfThen(&growCapture,
  b->  GreaterOrEqualTo(
  b->    Load("captop"),
  b->    Load("capsize")));

  growCapture->Store("capture",
  growCapture->  Call("doublecap", 4,
  growCapture->       Load("L"),
  growCapture->       Load("capture"),
  growCapture->       Load("captop"),
  growCapture->       Load("ptop")));

  growCapture->Store("capsize",
  growCapture->  Add(
  growCapture->    Load("captop"),
  growCapture->    Load("captop")));

  b->AddFallThroughBuilder(fallThroughBuilder);
}

void
LpMatcher::doOpenCapture(TR::BytecodeBuilder *b, unsigned short key, byte kind, TR::BytecodeBuilder *fallThroughBuilder) {
  b->Store("cap",
  b->  IndexAt(pCaptureType,
  b->    Load("capture"),
  b->    Load("captop")));

  b->StoreIndirect("Capture", "siz",
  b->  Load("cap"),
  b->  ConstInteger(Byte, 0));

  b->StoreIndirect("Capture", "s",
  b->  Load("cap"),
  b->  Load("s"));

  pushCapture(b, key, kind, fallThroughBuilder);
}

void
LpMatcher::doFullCapture(TR::BytecodeBuilder *b, unsigned short key, byte kind, byte off, TR::BytecodeBuilder *fallThroughBuilder) {
  b->Store("cap",
  b->  IndexAt(pCaptureType,
  b->    Load("capture"),
  b->    Load("captop")));

  b->StoreIndirect("Capture", "siz",
  b->  Load("cap"),
  b->  ConstInteger(Byte, off+1));

  // use Add rather than Sub because address subtraction not supported by OMR compiler
  b->StoreIndirect("Capture", "s",
  b->  Load("cap"),
  b->  Add(
  b->    Load("s"),
  b->    ConstInteger(Word, -off)));

  pushCapture(b, key, kind, fallThroughBuilder);
}

  
void
LpMatcher::doCloseCapture(TR::BytecodeBuilder *b, unsigned short key, byte kind, TR::BytecodeBuilder *fallThroughBuilder) {
  b->Store("capMinus1",
  b->  IndexAt(pCaptureType,
  b->    Load("capture"),
  b->    Sub(
  b->      Load("captop"),
  b->      ConstInteger(Int, 1))));

  TR::IlBuilder *cond1Builder = OrphanBuilder();
  TR::IlValue *cond1 = cond1Builder->EqualTo(
                       cond1Builder->  LoadIndirect("Capture", "siz",
                       cond1Builder->    Load("capMinus1")),
                       cond1Builder->  ConstInteger(Byte, 0));

  TR::IlBuilder *cond2Builder = OrphanBuilder();
  cond2Builder->Store("capMinus1s",
  cond2Builder->  LoadIndirect("Capture", "s",
  cond2Builder->    Load("capMinus1")));
  // awkward expression because subtracting addresses not yet supported by OMR JIT
  cond2Builder->Store("sdiff",
  cond2Builder->  Sub(
  cond2Builder->    ConvertTo(Word,
  cond2Builder->      Load("s")),
  cond2Builder->    ConvertTo(Word,
  cond2Builder->      Load("capMinus1s"))));
  TR::IlValue *cond2 = cond2Builder->LessThan(
                       cond2Builder->  Load("sdiff"),
                       cond2Builder->  ConstInteger(Word, UCHAR_MAX));

  TR::IlBuilder *makeFull=NULL, *markAsClosed= NULL;
  b->IfAnd(&makeFull, &markAsClosed, 2, cond1Builder, cond1, cond2Builder, cond2);
 
  makeFull->StoreIndirect("Capture", "siz",
  makeFull->  Load("capMinus1"),
  makeFull->  UnsignedConvertTo(Byte,
  makeFull->    Add(
  makeFull->      Load("sdiff"),
  makeFull->      ConstInteger(Word, 1))));

  // The Goto below is risky because it will not propagate any vm state changes along this
  // path to fallThroughBuilder. But since there are no VM state changes internally
  // in this bytecode handler, it is ok for fallThroughBuilder to inherit its vmState
  // only from the fall-through path from b that will be created by pushCapture(), below.
  makeFull->Goto(fallThroughBuilder);

  markAsClosed->Store("cap",
  markAsClosed->  IndexAt(pCaptureType,
  markAsClosed->    Load("capture"),
  markAsClosed->    Load("captop")));
  markAsClosed->StoreIndirect("Capture", "siz",
  markAsClosed->  Load("cap"),
  markAsClosed->  ConstInteger(Byte, 1));
  markAsClosed->StoreIndirect("Capture", "s",
  markAsClosed->  Load("cap"),
  markAsClosed->  Load("s"));
 
  pushCapture(b, key, kind, fallThroughBuilder);
}

void
LpMatcher::doCloseRuntime(TR::BytecodeBuilder *b, TR::BytecodeBuilder *fallThroughBuilder) {
  // CloseRuntime is a pretty complicated bytecode
  // For now, let's just use a helper function to do all the work, rather than inlining all that code here
  // Unfortunately, even that approach is messy because of the number of variables whose value could be
  // changed in helperCloseRuntime() that must be updated in this stack frame. To handle these changes,
  // we create a local array of values (resultsArray) that will be passed to the helper. Each element of
  // this array corresponds to a particular value: res at index 0, ndyncap at index 1, captop at index 2,
  // and capsize at index 3. When we come back from the helper, reload those four variables from the
  // results array.
  // Maybe we should just inline the code and live with the complexity, since this results array stuff
  // is prety ugly, though not very different to how a returned tuple would probably work.

  TR::IlValue *resultsArray = b->CreateLocalArray(4, Int);

  b->Store("capture",
  b->  Call("helperCloseRuntime", 10,
  b->       Load("L"),
  b->       Load("o"),
  b->       Load("s"),
  b->       Load("e"),
  b->       Load("capture"),
  b->       Load("captop"),
  b->       Load("capsize"),
  b->       Load("ndyncap"),
  b->       Load("ptop"),
            resultsArray));

  b->Store("captop",
  b->    LoadAt(pInt, // results[2] == captop
  b->      IndexAt(pInt,
             resultsArray,
  b->        ConstInteger(Word, 2))));

  TR::BytecodeBuilder *failBuilder = OrphanBytecodeBuilder(b->bcIndex(), b->name());
  TR::IlValue *res = b->LoadAt(pInt,  // results[0] == res
                     b->  IndexAt(pInt,
                            resultsArray,
                     b->    ConstInteger(Word, 0)));
  b->IfCmpEqual(failBuilder,
       res,
  b->  ConstInteger(Int, -1));
  FailBuilder(failBuilder);

  // otherwise, need to store new capture, ndyncap, and capsize and update s using res
  b->Store("s",
  b->  Add(
  b->    Load("o"),
         res));

  b->Store("ndyncap",
  b->    LoadAt(pInt, // results[1] == ndyncap
  b->      IndexAt(pInt,
             resultsArray,
  b->        ConstInteger(Word, 1))));

  b->Store("capsize",
  b->    LoadAt(pInt, // results[3] == capsize
  b->      IndexAt(pInt,
             resultsArray,
  b->        ConstInteger(Word, 3))));

  b->AddFallThroughBuilder(fallThroughBuilder);
}

bool
LpMatcher::buildIL() {
  TR::BytecodeBuilder **builders = (TR::BytecodeBuilder **) malloc(_ncode * sizeof(TR::BytecodeBuilder *));
  
  int32_t bci = 0;
  for (bci = 0;bci < _ncode;bci++) {
    Instruction *p = _op+bci;
    builders[bci] = OrphanBytecodeBuilder(bci, (char *)opcode_names[p->i.code]);
    }

  LpState initialState;
  setVMState(&initialState);

  Store("capsize",
    ConstInteger(Int, INITCAPSIZE));

  Store("captop",
    ConstInteger(Int, 0));

  Store("ndyncap",
    ConstInteger(Int, 0));

  AllLocalsHaveBeenDefined();

  AppendBuilder(builders[0]);

  bci = GetNextBytecodeFromWorklist();
  while (bci != -1) {
    TR::BytecodeBuilder *b = builders[bci];
    b->SetCurrentIlGenerator(); // helps get correct bytecode indices

    if (TraceEnabled_log()) {
      TraceIL_log("BC%d (%d) %s [MB %p]\n", b->bcIndex(), b->currentByteCodeIndex(), b->name(), this);
    }

    Instruction *p = _op+bci;

    #if defined(DEBUG)
       // NOTE this call will affect how much optimization happens and will generate a LOT of output
       b->Call("printBytecode", 6,
       b->   ConstAddress(_op),
       b->   ConstAddress(p),
       b->   Load("s"),
       b->   Load("capture"),
       b->   Load("ndyncap"),
       b->   Load("captop"));
    #endif

    switch ((Opcode)(p->i.code)) {
      case IEnd: {
        doEnd(b);
        break;
      }

      case IGiveup: {
        doGiveUp(b);
        break;
      }

      case IRet: {
        return false; // do not yet handle call/ret internal patterns yet
      }

      case IAny: {
        doAny(b, builders[bci+1]);
        break;
      }

      case ITestAny: {
        doTestAny(b, builders[bci+2], builders[bci+getoffset(p)]);
        break;
      }

      case IChar: {
        doChar(b, (char)p->i.aux, builders[bci+1]);
        break;
      }

      case ITestChar: {
        doTestChar(b, (char)p->i.aux, builders[bci+2], builders[bci+getoffset(p)]);
        break;
      }

      case ISet: {
        doSet(b, (p+1)->buff, builders[bci+CHARSETINSTSIZE]);
        break;
      }

      case ITestSet: {
        doTestSet(b, (p+2)->buff, builders[bci+1+CHARSETINSTSIZE], builders[bci+getoffset(p)]);
        break;
      }

      case IBehind: {
        doBehind(b, p->i.aux, builders[bci+1]);
        break;
      }

      case ISpan: {
        doSpan(b, (p+1)->buff, builders[bci+CHARSETINSTSIZE]);
        break;
        }

      case IJmp: {
        b->Goto(builders[bci+getoffset(p)]);
        break;
      }

      case ICall: {
        return false; // do not handle call/ret internal patterns yet
      }

      case IChoice: {
        PUSH(b, (b->Load("s")), (builders[bci+getoffset(p)]), (b->Load("captop")));
        b->AddFallThroughBuilder(builders[bci+2]);
        break;
      }

      case ICommit: {
        DROP(b);
        b->Goto(builders[bci+getoffset(p)]);
        break;
      }

      case IPartialCommit: {
        b->StoreOver(TOP_S(b),
        b->  Load("s"));
        b->StoreOver(TOP_CAPLEVEL(b),
        b->  Load("captop"));
        b->Goto(builders[bci+getoffset(p)]);
        break;
      }

      case IBackCommit: {
        b->Store("s", TOP_S(b));
        b->Store("captop", TOP_CAPLEVEL(b));
        DROP(b);
        b->Goto(builders[bci+getoffset(p)]);
        break;
      }

      case IFailTwice: {
        DROP(b);
        TR::BytecodeBuilder *failBuilder = OrphanBytecodeBuilder(b->bcIndex(), b->name());
        b->Goto(failBuilder);
        FailBuilder(failBuilder);
        break;
      }

      case IFail: {
        TR::BytecodeBuilder *failBuilder = OrphanBytecodeBuilder(b->bcIndex(), b->name());
        b->Goto(failBuilder);
        FailBuilder(failBuilder);
        break;
      }

      case ICloseRunTime: {
        doCloseRuntime(b, builders[bci+1]);
        break;
      }

      case ICloseCapture: {
        doCloseCapture(b, p->i.key, getkind(p), builders[bci+1]);
        break;
      }

      case IOpenCapture: {
        doOpenCapture(b, p->i.key, getkind(p), builders[bci+1]);
        break;
      }

      case IFullCapture: {
        doFullCapture(b, p->i.key, getkind(p), getoff(p), builders[bci+1]);
        break;
      }

      case IHalt: {
        b->Store("cap",
        b->  IndexAt(pCaptureType,
        b->    Load("capture"),
        b->    Load("captop")));
        b->StoreIndirect("Capture", "kind",
        b->  Load("cap"),
        b->  ConstInteger(Byte, Cfinal));
        b->StoreIndirect("Capture", "s",
        b->  Load("cap"),
        b->  Load("s"));
        b->Return(
        b->  Load("s"));
        break;
      }

      default: {
        assert(0);
        free(builders);
        return false;
      }
    }

    bci = GetNextBytecodeFromWorklist();
  }

  free(builders);
  return true;
}

LpMatcher::LpMatcher(TR::TypeDictionary *types, Pattern *p, Instruction *op, int ncode)
  : TR::MethodBuilder(types), _p(p), _op(op), _ncode(ncode) {
  init();
}

void
LpMatcher::init() {
  CaptureType = _types->LookupStruct("Capture");
  pCaptureType = _types->PointerTo(CaptureType);
  Byte = _types->toIlType<byte>();
  Short = _types->toIlType<unsigned short>();
  pInt8 = _types->PointerTo(Int8);
  pChar = _types->toIlType<const char *>();
  pVoid = _types->toIlType<void *>();
  Int = _types->toIlType<int>();
  pInt = _types->PointerTo(Int);

  // signature for compiled code is the same as the interpreter's "match" function:
  // const char *match (lua_State *L, const char *o, const char *s, const char *e,
  //                    Instruction *op, Capture *capture, int ptop, int ncode);

  DefineReturnType(pChar);

  DefineParameter("L", Address); // won't dereference this parameter, at least initially
  DefineParameter("o", pChar);
  DefineParameter("s", pChar);
  DefineParameter("e", pChar);
  DefineParameter("capture", pCaptureType);
  DefineParameter("ptop", Int);
  DefineParameter("ncode", Int);

  // need a better solution for identifying patterns and where they come from
  // for now, just give every function the name "compiled_match" and make up
  // a filename and line number
  char *name = (char *) malloc(15 * sizeof(char));
  sprintf(name, "compiled_match");
  DefineName(name);
  DefineFile("lpjit");
  DefineLine("1");

  DefineFunction("helperCloseRuntime", "lpvm.c", "363", (void *)&helperCloseRuntime, pCaptureType, 10,
                 pVoid,
                 pChar,
                 pChar,
                 pChar,
                 pVoid,
                 Int,
                 Int,
                 Int,
                 Int,
                 pInt);
  DefineFunction("removedyncap", "lpvm.c", "135", (void *)&removedyncap, Int, 4,
                 pVoid,
                 pCaptureType,
                 Int,
                 Int);
  DefineFunction("doublecap", "lpvm.c", "50", (void *)&doublecap, pCaptureType, 4,
                 pVoid,
                 pVoid,
                 Int,
                 Int);
  #if defined(DEBUG)
    DefineFunction("printBytecode", "lpjit.cpp", "143", (void *)printBytecode, NoType, 6,
                   pVoid,
                   pVoid,
                   pChar,
                   pCaptureType,
                   Int,
                   Int);
  #endif
}

void
LpMatcher::incrementBy1(TR::IlBuilder *b, char *sym) {
  b->Store(sym,
  b->  Add(
  b->    Load(sym),
  b->    ConstInteger(Word, 1)));
}




extern "C" int32_t
compileMatcher(Pattern *p, Instruction *op, int ncode, uint8_t **entry) {
  static bool initialized = false;

  // should punt this to rosie itself as well as calling shutdownJit() but
  // for now, this works.
  if (!initialized) {
    initialized = true;

    bool rc = initializeJit();
    if (!rc) {
      printf("JIT initialization failed\n");
      return -1;
    }
  }

  TR::TypeDictionary types;

  // the only special type needed is the Capture struct
  types.DEFINE_STRUCT(Capture);
  types.DEFINE_FIELD(Capture, s, types.toIlType<char *>());
  types.DEFINE_FIELD(Capture, idx, types.toIlType<unsigned short>());
  types.DEFINE_FIELD(Capture, kind, types.toIlType<byte>());
  types.DEFINE_FIELD(Capture, siz, types.toIlType<byte>());
  types.CLOSE_STRUCT(Capture);

  // compile main entry point
  LpMatcher matcher(&types, p, op, ncode);
  int32_t rc = compileMethodBuilder(&matcher, entry);
  return rc;
}
