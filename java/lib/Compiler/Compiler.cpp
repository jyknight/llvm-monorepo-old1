//===-- Compiler.cpp - Java bytecode compiler -------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains Java bytecode to LLVM bytecode compiler.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "javacompiler"

#include <llvm/Java/Bytecode.h>
#include <llvm/Java/ClassFile.h>
#include <llvm/Java/Compiler.h>
#include <llvm/Constants.h>
#include <llvm/DerivedTypes.h>
#include <llvm/Instructions.h>
#include <llvm/Value.h>
#include <llvm/Type.h>
#include <Support/Debug.h>
#include <Support/StringExtras.h>

using namespace llvm;
using namespace llvm::Java;

namespace {

    inline bool isTwoSlotValue(const Value* v) {
        return v->getType() == Type::LongTy | v->getType() == Type::DoubleTy;
    }

    inline bool isOneSlotValue(const Value* v) {
        return !isTwoSlotValue(v);
    }

} // namespace

void Compiler::compileMethodInit(Function& function,
                                 const CodeAttribute& codeAttr)
{
    while (!opStack_.empty())
        opStack_.pop();

    locals_.clear();
    locals_.assign(codeAttr.getMaxLocals(), NULL);

    bc2bbMap_.clear();
    bc2bbMap_.assign(codeAttr.getCodeSize(), NULL);

    const uint8_t* code = codeAttr.getCode();
    for (unsigned i = 0; i < codeAttr.getCodeSize(); ++i) {
        using namespace llvm::Java::Opcode;

        unsigned bcStart = i;
        bool wide = code[i] == WIDE;
        i += wide;
        switch (code[i]) {
        case BIPUSH:
        case LDC:
        case NEWARRAY:
            ++i;
            break;
        case ILOAD:
        case LLOAD:
        case FLOAD:
        case DLOAD:
        case ALOAD:
        case ISTORE:
        case LSTORE:
        case FSTORE:
        case DSTORE:
        case ASTORE:
        case RET:
            i += 1 + wide;
            break;
        case SIPUSH:
        case LDC_W:
        case LDC2_W:
        case GOTO:
        case JSR:
        case GETSTATIC:
        case PUTSTATIC:
        case GETFIELD:
        case PUTFIELD:
        case INVOKEVIRTUAL:
        case INVOKESPECIAL:
        case INVOKESTATIC:
        case INVOKEINTERFACE:
        case NEW:
        case ANEWARRAY:
        case ARRAYLENGTH:
        case ATHROW:
        case CHECKCAST:
        case INSTANCEOF:
            i += 2;
            break;
        case IINC:
            i += 2 * (1 + wide);
            break;
        case IFEQ:
        case IFNE:
        case IFLT:
        case IFGE:
        case IFGT:
        case IFLE:
        case IF_ICMPEQ:
        case IF_ICMPNE:
        case IF_ICMPLT:
        case IF_ICMPGE:
        case IF_ICMPGT:
        case IF_ICMPLE:
        case IF_IACMPEQ:
        case IF_IACMPNE:
        case IFNULL:
        case IFNONNULL: {
            unsigned index = readUShort(code, i);
            bc2bbMap_[bcStart] = new BasicBlock(
                std::string("bb@bc") + utostr(bcStart), &function);
            break;
        }
        case TABLESWITCH: {
            skipPadBytes(code, i);
            readSInt(code, i);
            int low = readSInt(code, i);
            int high = readSInt(code, i);
            while (low++ <= high) {
                unsigned bcIndex = bcStart + readSInt(code, i);
                bc2bbMap_[bcIndex] = new BasicBlock(
                    std::string("bb@bc") + utostr(bcIndex), &function);
            }
            break;
        }
        case LOOKUPSWITCH: {
            skipPadBytes(code, i);
            readSInt(code, i);
            unsigned pairCount = readUInt(code, i);
            while (pairCount--) {
                readSInt(code, i);
                unsigned bcIndex = bcStart + readSInt(code, i);
                bc2bbMap_[bcIndex] = new BasicBlock(
                    std::string("bb@bc") + utostr(bcIndex), &function);
            }
            break;
        }
        case XXXUNUSEDXXX:
            throw "FIXME: create new exception class";
        case MULTIANEWARRAY:
            i += 3;
            break;
        case GOTO_W:
        case JSR_W:
            i+= 4;
            break;
        default:
            break;
        }
    }

    BasicBlock* bb = new BasicBlock("entry", &function);
    for (unsigned i = 0; i < codeAttr.getCodeSize(); ++i) {
        if (bc2bbMap_[i])
            bb = bc2bbMap_[i];
        else
            bc2bbMap_[i] = bb;
    }
}

void Compiler::compileMethod(Module& module, const Java::Method& method) {
    using namespace llvm::Java::Opcode;

    DEBUG(std::cerr << "compiling method: " << method.getName()->str() << '\n');

    Function* function =
        module.getOrInsertFunction(method.getName()->str(), Type::VoidTy, 0);

    const Java::CodeAttribute* codeAttr =
        Java::getCodeAttribute(method.getAttributes());

    compileMethodInit(*function, *codeAttr);

    const uint8_t* code = codeAttr->getCode();
    for (unsigned i = 0; i < codeAttr->getCodeSize(); ++i) {
        unsigned bcStart = i;
        bool wide = code[i] == WIDE;
        i += wide;
        switch (code[i]) {
        case ACONST_NULL:
            // FIXME: should push a null pointer of type Object*
            opStack_.push(
                ConstantPointerNull::get(PointerType::get(Type::VoidTy)));
            break;
        case ICONST_M1:
        case ICONST_0:
        case ICONST_1:
        case ICONST_2:
        case ICONST_3:
        case ICONST_4:
        case ICONST_5:
            opStack_.push(ConstantSInt::get(Type::IntTy, code[i]-ICONST_0));
            break;
        case LCONST_0:
        case LCONST_1:
            opStack_.push(ConstantSInt::get(Type::LongTy, code[i]-LCONST_0));
            break;
        case FCONST_0:
        case FCONST_1:
        case FCONST_2:
            opStack_.push(ConstantFP::get(Type::FloatTy, code[i]-FCONST_0));
            break;
        case DCONST_0:
        case DCONST_1:
            opStack_.push(ConstantFP::get(Type::DoubleTy, code[i]-DCONST_0));
            break;
        case BIPUSH: {
            int imm = readSByte(code, i);
            opStack_.push(ConstantSInt::get(Type::IntTy, imm));
            break;
        }
        case SIPUSH: {
            int imm = readSShort(code, i);
            opStack_.push(ConstantSInt::get(Type::IntTy, imm));
            break;
        }
        case LDC: {
            unsigned index = readUByte(code, i);
            // FIXME: load constant from constant pool
        }
        case LDC_W: {
            unsigned index = readUShort(code, i);
            // FIXME: load constant from constant pool
        }
        case LDC2_W: {
            unsigned index = readUShort(code, i);
            // FIXME: load constant from constant pool
        }
        case ILOAD:
        case LLOAD:
        case FLOAD:
        case DLOAD:
        case ALOAD: {
            // FIXME: use opcodes to perform type checking
            unsigned index = readUByte(code, i);
            Instruction* in = new LoadInst(locals_[index]);
            opStack_.push(in);
            bc2bbMap_[bcStart]->getInstList().push_back(in);
            break;
        }
        case ILOAD_0:
        case ILOAD_1:
        case ILOAD_2:
        case ILOAD_3: {
            Instruction* in = new LoadInst(locals_[code[i]-ILOAD_0]);
            opStack_.push(in);
            bc2bbMap_[bcStart]->getInstList().push_back(in);
            break;
        }
        case LLOAD_0:
        case LLOAD_1:
        case LLOAD_2:
        case LLOAD_3: {
            Instruction* in = new LoadInst(locals_[code[i]-LLOAD_0]);
            opStack_.push(in);
            bc2bbMap_[bcStart]->getInstList().push_back(in);
            break;
        }
        case FLOAD_0:
        case FLOAD_1:
        case FLOAD_2:
        case FLOAD_3: {
            Instruction* in = new LoadInst(locals_[code[i]-FLOAD_0]);
            opStack_.push(in);
            bc2bbMap_[bcStart]->getInstList().push_back(in);
            break;
        }
        case DLOAD_0:
        case DLOAD_1:
        case DLOAD_2:
        case DLOAD_3: {
            Instruction* in = new LoadInst(locals_[code[i]-DLOAD_0]);
            opStack_.push(in);
            bc2bbMap_[bcStart]->getInstList().push_back(in);
            break;
        }
        case ALOAD_0:
        case ALOAD_1:
        case ALOAD_2:
        case ALOAD_3: {
            Instruction* in = new LoadInst(locals_[code[i]-ALOAD_0]);
            opStack_.push(in);
            bc2bbMap_[bcStart]->getInstList().push_back(in);
            break;
        }
        case IALOAD:
        case LALOAD:
        case FALOAD:
        case DALOAD:
        case AALOAD:
        case BALOAD:
        case CALOAD:
        case SALOAD:
            assert(0 && "not implemented");
        case ISTORE:
        case LSTORE:
        case FSTORE:
        case DSTORE:
        case ASTORE: {
            // FIXME: use opcodes to perform type checking
            unsigned index = readUByte(code, i);
            Value* v1 = opStack_.top(); opStack_.pop();
            Instruction* in = new StoreInst(v1, locals_[index]);
            opStack_.push(in);
            bc2bbMap_[bcStart]->getInstList().push_back(in);
            break;
        }
        case ISTORE_0:
        case ISTORE_1:
        case ISTORE_2:
        case ISTORE_3: {
            Value* v1 = opStack_.top(); opStack_.pop();
            Instruction* in =
                new StoreInst(v1, locals_[code[i]-ISTORE_0]);
            opStack_.push(in);
            bc2bbMap_[bcStart]->getInstList().push_back(in);
            break;
        }
        case LSTORE_0:
        case LSTORE_1:
        case LSTORE_2:
        case LSTORE_3: {
            Value* v1 = opStack_.top(); opStack_.pop();
            Instruction* in =
                new StoreInst(v1, locals_[code[i]-LSTORE_0]);
            opStack_.push(in);
            bc2bbMap_[bcStart]->getInstList().push_back(in);
            break;
        }
        case FSTORE_0:
        case FSTORE_1:
        case FSTORE_2:
        case FSTORE_3: {
            Value* v1 = opStack_.top(); opStack_.pop();
            Instruction* in =
                new StoreInst(v1, locals_[code[i]-FSTORE_0]);
            opStack_.push(in);
            bc2bbMap_[bcStart]->getInstList().push_back(in);
            break;
        }
        case DSTORE_0:
        case DSTORE_1:
        case DSTORE_2:
        case DSTORE_3: {
            Value* v1 = opStack_.top(); opStack_.pop();
            Instruction* in =
                new StoreInst(v1, locals_[code[i]-DSTORE_0]);
            opStack_.push(in);
            bc2bbMap_[bcStart]->getInstList().push_back(in);
            break;
        }
        case ASTORE_0:
        case ASTORE_1:
        case ASTORE_2:
        case ASTORE_3: {
            Value* v1 = opStack_.top(); opStack_.pop();
            Instruction* in =
                new StoreInst(v1, locals_[code[i]-ASTORE_0]);
            opStack_.push(in);
            bc2bbMap_[bcStart]->getInstList().push_back(in);
            break;
        }
        case IASTORE:
        case LASTORE:
        case FASTORE:
        case DASTORE:
        case AASTORE:
        case BASTORE:
        case CASTORE:
        case SASTORE:
            assert(0 && "not implemented");
        case POP:
            opStack_.pop();
            break;
        case POP2: {
            Value* v1 = opStack_.top(); opStack_.pop();
            if (isOneSlotValue(v1))
                opStack_.pop();
            break;
        }
        case DUP:
            opStack_.push(opStack_.top());
            break;
        case DUP_X1: {
            Value* v1 = opStack_.top(); opStack_.pop();
            Value* v2 = opStack_.top(); opStack_.pop();
            opStack_.push(v1);
            opStack_.push(v2);
            opStack_.push(v1);
            break;
        }
        case DUP_X2: {
            Value* v1 = opStack_.top(); opStack_.pop();
            Value* v2 = opStack_.top(); opStack_.pop();
            if (isOneSlotValue(v2)) {
                Value* v3 = opStack_.top(); opStack_.pop();
                opStack_.push(v1);
                opStack_.push(v3);
                opStack_.push(v2);
                opStack_.push(v1);
            }
            else {
                opStack_.push(v1);
                opStack_.push(v2);
                opStack_.push(v1);
            }
            break;
        }
        case DUP2: {
            Value* v1 = opStack_.top(); opStack_.pop();
            if (isOneSlotValue(v1)) {
                Value* v2 = opStack_.top(); opStack_.pop();
                opStack_.push(v2);
                opStack_.push(v1);
                opStack_.push(v2);
                opStack_.push(v1);
            }
            else {
                opStack_.push(v1);
                opStack_.push(v1);
            }
            break;
        }
        case DUP2_X1: {
            Value* v1 = opStack_.top(); opStack_.pop();
            Value* v2 = opStack_.top(); opStack_.pop();
            if (isOneSlotValue(v1)) {
                Value* v3 = opStack_.top(); opStack_.pop();
                opStack_.push(v2);
                opStack_.push(v1);
                opStack_.push(v3);
                opStack_.push(v2);
                opStack_.push(v1);
            }
            else {
                opStack_.push(v1);
                opStack_.push(v2);
                opStack_.push(v1);
            }
            break;
        }
        case DUP2_X2: {
            Value* v1 = opStack_.top(); opStack_.pop();
            Value* v2 = opStack_.top(); opStack_.pop();
            if (isOneSlotValue(v1)) {
                Value* v3 = opStack_.top(); opStack_.pop();
                if (isOneSlotValue(v3)) {
                    Value* v4 = opStack_.top(); opStack_.pop();
                    opStack_.push(v2);
                    opStack_.push(v1);
                    opStack_.push(v4);
                    opStack_.push(v3);
                    opStack_.push(v2);
                    opStack_.push(v1);
                }
                else {
                    opStack_.push(v2);
                    opStack_.push(v1);
                    opStack_.push(v3);
                    opStack_.push(v2);
                    opStack_.push(v1);
                }
            }
            else {
                if (isOneSlotValue(v2)) {
                    Value* v3 = opStack_.top(); opStack_.pop();
                    opStack_.push(v1);
                    opStack_.push(v3);
                    opStack_.push(v2);
                    opStack_.push(v1);
                }
                else {
                    opStack_.push(v1);
                    opStack_.push(v2);
                    opStack_.push(v1);
                }
            }
            break;
        }
        case SWAP: {
            Value* v1 = opStack_.top(); opStack_.pop();
            Value* v2 = opStack_.top(); opStack_.pop();
            opStack_.push(v1);
            opStack_.push(v2);
            break;
        }
        case IADD:
        case LADD:
        case FADD:
        case DADD: {
            Value* v1 = opStack_.top(); opStack_.pop();
            Value* v2 = opStack_.top(); opStack_.pop();
            Instruction* in = BinaryOperator::create(Instruction::Add, v1, v2);
            bc2bbMap_[bcStart]->getInstList().push_back(in);
            break;
        }
        case ISUB:
        case LSUB:
        case FSUB:
        case DSUB: {
            Value* v1 = opStack_.top(); opStack_.pop();
            Value* v2 = opStack_.top(); opStack_.pop();
            Instruction* in = BinaryOperator::create(Instruction::Sub, v1, v2);
            bc2bbMap_[bcStart]->getInstList().push_back(in);
            break;
        }
        case IMUL:
        case LMUL:
        case FMUL:
        case DMUL: {
            Value* v1 = opStack_.top(); opStack_.pop();
            Value* v2 = opStack_.top(); opStack_.pop();
            Instruction* in = BinaryOperator::create(Instruction::Mul, v1, v2);
            bc2bbMap_[bcStart]->getInstList().push_back(in);
            break;
        }
        case IDIV:
        case LDIV:
        case FDIV:
        case DDIV: {
            Value* v1 = opStack_.top(); opStack_.pop();
            Value* v2 = opStack_.top(); opStack_.pop();
            Instruction* in = BinaryOperator::create(Instruction::Div, v1, v2);
            bc2bbMap_[bcStart]->getInstList().push_back(in);
            break;
        }
        case IREM:
        case LREM:
        case FREM:
        case DREM: {
            Value* v1 = opStack_.top(); opStack_.pop();
            Value* v2 = opStack_.top(); opStack_.pop();
            Instruction* in = BinaryOperator::create(Instruction::Rem, v1, v2);
            bc2bbMap_[bcStart]->getInstList().push_back(in);
            break;
        }
        case INEG:
        case LNEG:
        case FNEG:
        case DNEG:
        case ISHL:
        case LSHL: {
            Value* v1 = opStack_.top(); opStack_.pop();
            Value* v2 = opStack_.top(); opStack_.pop();
            Instruction* in = new ShiftInst(Instruction::Shl, v1, v2);
            bc2bbMap_[bcStart]->getInstList().push_back(in);
            break;
        }
        case ISHR:
        case LSHR: {
            Value* v1 = opStack_.top(); opStack_.pop();
            Value* v2 = opStack_.top(); opStack_.pop();
            Instruction* in = new ShiftInst(Instruction::Shr, v1, v2);
            bc2bbMap_[bcStart]->getInstList().push_back(in);
            break;
        }
        case IUSHR:
        case LUSHR: {
            Value* v1 = opStack_.top(); opStack_.pop();
            Instruction* in =
                new CastInst(v1, v1->getType()->getUnsignedVersion());
            bc2bbMap_[bcStart]->getInstList().push_back(in);
            Value* v2 = opStack_.top(); opStack_.pop();
            in = new ShiftInst(Instruction::Shr, in, v2);
            bc2bbMap_[bcStart]->getInstList().push_back(in);
            break;
        }
        case IAND:
        case LAND: {
            Value* v1 = opStack_.top(); opStack_.pop();
            Value* v2 = opStack_.top(); opStack_.pop();
            Instruction* in = BinaryOperator::create(Instruction::And, v1, v2);
            bc2bbMap_[bcStart]->getInstList().push_back(in);
            break;
        }
        case IOR:
        case LOR: {
            Value* v1 = opStack_.top(); opStack_.pop();
            Value* v2 = opStack_.top(); opStack_.pop();
            Instruction* in = BinaryOperator::create(Instruction::Or, v1, v2);
            bc2bbMap_[bcStart]->getInstList().push_back(in);
            break;
        }
        case IXOR:
        case LXOR: {
            Value* v1 = opStack_.top(); opStack_.pop();
            Value* v2 = opStack_.top(); opStack_.pop();
            Instruction* in = BinaryOperator::create(Instruction::Xor, v1, v2);
            bc2bbMap_[bcStart]->getInstList().push_back(in);
            break;
        }
        case IINC:
        case I2L:
        case I2F:
        case I2D:
        case L2I:
        case L2F:
        case L2D:
        case F2I:
        case F2L:
        case F2D:
        case D2I:
        case D2L:
        case D2F:
        case I2B:
        case I2C:
        case I2S:
        case LCMP:
        case FCMPL:
        case FCMPG:
        case DCMPL:
        case DCMPG:
            assert(0 && "not implemented");
        case IFEQ:
        case IFNE:
        case IFLT:
        case IFGE:
        case IFGT:
        case IFLE: {
            static Instruction::BinaryOps java2llvm[] = {
                Instruction::SetEQ,
                Instruction::SetNE,
                Instruction::SetLT,
                Instruction::SetGE,
                Instruction::SetGT,
                Instruction::SetLE,
            };
            Value* v1 = opStack_.top(); opStack_.pop();
            Value* v2 = ConstantSInt::get(Type::IntTy, 0);
            Instruction* in = new SetCondInst(java2llvm[i-IFEQ], v1, v2);
            bc2bbMap_[bcStart]->getInstList().push_back(in);
            new BranchInst(bc2bbMap_[bcStart + readSShort(code, i)],
                           bc2bbMap_[i + 1],
                           bc2bbMap_[bcStart]);
            break;
        }
        case IF_ICMPEQ:
        case IF_ICMPNE:
        case IF_ICMPLT:
        case IF_ICMPGE:
        case IF_ICMPGT:
        case IF_ICMPLE: {
            static Instruction::BinaryOps java2llvm[] = {
                Instruction::SetEQ,
                Instruction::SetNE,
                Instruction::SetLT,
                Instruction::SetGE,
                Instruction::SetGT,
                Instruction::SetLE,
            };
            Value* v1 = opStack_.top(); opStack_.pop();
            Value* v2 = opStack_.top(); opStack_.pop();
            Instruction* in = new SetCondInst(java2llvm[i-IF_ICMPEQ], v1, v2);
            bc2bbMap_[bcStart]->getInstList().push_back(in);
            new BranchInst(bc2bbMap_[bcStart + readSShort(code, i)],
                           bc2bbMap_[i + 1],
                           bc2bbMap_[bcStart]);
            break;
        }
        case IF_IACMPEQ:
        case IF_IACMPNE: {
            static Instruction::BinaryOps java2llvm[] = {
                Instruction::SetEQ,
                Instruction::SetNE,
            };
            Value* v1 = opStack_.top(); opStack_.pop();
            Value* v2 = opStack_.top(); opStack_.pop();
            Instruction* in = new SetCondInst(java2llvm[i-IF_IACMPEQ], v1, v2);
            bc2bbMap_[bcStart]->getInstList().push_back(in);
            new BranchInst(bc2bbMap_[bcStart + readSShort(code, i)],
                           bc2bbMap_[i + 1],
                           bc2bbMap_[bcStart]);
            break;
        }
        case GOTO:
            new BranchInst(bc2bbMap_[bcStart + readSShort(code, i)]);
            break;
        case JSR:
        case RET:
            assert(0 && "not implemented");
        case TABLESWITCH: {
            Value* v1 = opStack_.top(); opStack_.pop();
            skipPadBytes(code, i);
            int def = readSInt(code, i);
            int low = readSInt(code, i);
            int high = readSInt(code, i);
            SwitchInst* in =
                new SwitchInst(v1, bc2bbMap_[bcStart + def],bc2bbMap_[bcStart]);
            while (low <= high)
                in->addCase(ConstantSInt::get(Type::IntTy, low++),
                            bc2bbMap_[bcStart + readSInt(code, i)]);
            break;
        }
        case LOOKUPSWITCH: {
            Value* v1 = opStack_.top(); opStack_.pop();
            skipPadBytes(code, i);
            int def = readSInt(code, i);
            unsigned pairCount = readUInt(code, i);
            SwitchInst* in =
                new SwitchInst(v1, bc2bbMap_[bcStart + def],bc2bbMap_[bcStart]);
            while (pairCount--)
                in->addCase(ConstantSInt::get(Type::IntTy, readSInt(code, i)),
                           bc2bbMap_[bcStart + readSInt(code, i)]);
            break;
        }
        case IRETURN:
        case LRETURN:
        case FRETURN:
        case DRETURN:
        case ARETURN: {
            Value* v1 = opStack_.top(); opStack_.pop();
            new ReturnInst(v1, bc2bbMap_[bcStart]);
            break;
        }
        case RETURN:
            new ReturnInst(NULL, bc2bbMap_[bcStart]);
            break;
        case GETSTATIC:
        case PUTSTATIC:
        case GETFIELD:
        case PUTFIELD:
        case INVOKEVIRTUAL:
        case INVOKESPECIAL:
        case INVOKESTATIC:
        case INVOKEINTERFACE:
        case XXXUNUSEDXXX:
        case NEW:
        case NEWARRAY:
        case ANEWARRAY:
        case ARRAYLENGTH:
        case ATHROW:
        case CHECKCAST:
        case INSTANCEOF:
        case MONITORENTER:
        case MONITOREXIT:
//      case WIDE:
        case MULTIANEWARRAY:
            assert(0 && "not implemented");
        case IFNULL:
        case IFNONNULL: {
            static Instruction::BinaryOps java2llvm[] = {
                Instruction::SetEQ,
                Instruction::SetNE,
            };
            Value* v1 = opStack_.top(); opStack_.pop();
            // FIXME: should compare to a null pointer of type Object*
            Value* v2 =ConstantPointerNull::get(PointerType::get(Type::VoidTy));
            Instruction* in = new SetCondInst(java2llvm[i-IFNULL], v1, v2);
            bc2bbMap_[bcStart]->getInstList().push_back(in);
            new BranchInst(bc2bbMap_[bcStart + readSShort(code, i)],
                           bc2bbMap_[i + 1],
                           bc2bbMap_[bcStart]);
            break;
        }
        case GOTO_W:
            new BranchInst(bc2bbMap_[bcStart + readSInt(code, i)]);
            break;
        case JSR_W:
            assert(0 && "not implemented");
        case BREAKPOINT:
        case IMPDEP1:
        case IMPDEP2:
        case NOP:
            break;
        }
    }
}

Module* Compiler::compile(const ClassFile& cf)
{
    DEBUG(std::cerr << "compiling class: "
          << cf.getThisClass()->getName()->str() << '\n');

    Module* module = new Module(cf.getThisClass()->getName()->str());

    const Java::Methods& methods = cf.getMethods();
    for (Java::Methods::const_iterator
             i = methods.begin(), e = methods.end(); i != e; ++i)
        compileMethod(*module, **i);

    return module;
}
