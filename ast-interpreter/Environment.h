//==--- tools/clang-check/ClangInterpreter.cpp - Clang Interpreter tool --------------===//
//===----------------------------------------------------------------------===//
#include <stdio.h>
#include <iostream>
#include <vector>
#include <string>
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/Decl.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/OperationKinds.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/Tooling.h"

using namespace clang;
#define LOCAL

enum {TINVALID, TINT, TARRAY, TREF};
typedef struct VType{
	int type;
	union {
		int64_t val;
		int idx;
		int* ref;
	};
	int ptr_sz;
}Vtype;

class StackFrame {
   /// StackFrame maps Variable Declaration to Value
   /// Which are either integer or addresses (also represented using an Integer value)
   std::map<Decl*, Vtype> mVars;
   std::map<Stmt*, Vtype> mExprs;
	int arrayVals[4096];
	int arridx = 0;
   /// The current stmt
   Stmt * mPC;
	Stmt * ret;
public:
   StackFrame() : mVars(), mExprs(), mPC() {
		memset(arrayVals, 0, sizeof(arrayVals));
		ret = NULL;
   }

   void bindDeclInt(Decl* decl, uintptr_t val) {
      mVars[decl].type = TINT;
		mVars[decl].val  = val;
		mVars[decl].ptr_sz  = 1;
   }
	void bindArrayDecl(Decl* decl, int num) {
		int idx = arridx;
		mVars[decl].type = TARRAY;
		mVars[decl].idx = idx;
		for(int i = 0; i < num; i++) {
			arrayVals[arridx++];
		}
	}
	void bindDeclVtype(Decl* decl, Vtype type) {
		mVars[decl] = type;
	}
	void bindDeclVtypeExceptSz(Decl* decl, Vtype type) {
		if(mVars.find(decl) == mVars.end()) {
			mVars[decl] = type;
		} else {
			int sz = mVars[decl].ptr_sz;
			mVars[decl] = type;
			mVars[decl].ptr_sz = sz;
		}
	}
	uintptr_t getDeclVal(Decl* decl) {
		assert (mVars.find(decl) != mVars.end());
		if(mVars[decl].type == TREF) return *(mVars[decl].ref);
		return mVars[decl].val;
	}
	Vtype getDeclVtype(Decl* decl){
		assert (mVars.find(decl) != mVars.end());
		return mVars[decl];
	}
	int checkDeclValid(Decl* decl) {
		return mVars.find(decl) != mVars.end();
	}
	int getArrayDeclVal(Decl* decl, int idx) {
		return arrayVals[mVars[decl].idx + idx];
	}
   void bindStmtInt(Stmt * stmt, uintptr_t val) {
	   mExprs[stmt].type = TINT;
		mExprs[stmt].val = val;
   }
	void bindStmtVtype(Stmt* stmt, Vtype vtype) {
		mExprs[stmt] = vtype;
	}
	void bindStmtRef(Stmt* stmt, void* addr, int sz=1) {
		mExprs[stmt].type = TREF;
		mExprs[stmt].ref = (int*)addr;
		mExprs[stmt].ptr_sz = sz;
	}
   uintptr_t getStmtVal(Stmt * stmt) {
	   assert (mExprs.find(stmt) != mExprs.end());
		if(mExprs[stmt].type == TREF) return *(mExprs[stmt].ref); // TODO: remove tref
	   return mExprs[stmt].val;
   }
	void* getStmtAddr(Stmt* stmt) {
		assert (mExprs.find(stmt) != mExprs.end());
		return mExprs[stmt].ref;
	}
	Vtype getStmtVtype(Stmt * stmt) {
	   assert (mExprs.find(stmt) != mExprs.end());
	   return mExprs[stmt];
   }
	int* getref(int idx){
		return &arrayVals[idx];
	}
   void setPC(Stmt * stmt) {
	   mPC = stmt;
   }
   Stmt * getPC() {
	   return mPC;
   }
	void setret(Stmt * stmt) {
	   ret = stmt;
   }
   Stmt * getret() {
	   return ret;
   }
};

/// Heap maps address to a value

typedef struct Htype{
	int idx;
	int length;
}Htype;

class Heap {
	int space[4096];
	int spaceidx = 0;
	std::map<Decl*, Htype> hVars;
public:
	Heap() {
		memset(space, 0, sizeof(space));
		spaceidx = 0;
   }
	void bindVar(Decl* decl, Htype type) {
		hVars[decl] = type;
	}
	void bindInt(Decl* decl, int val) {
		int idx = this->Malloc(1);
		space[idx] = val;
		this->bindVar(decl, {.idx = idx, .length=1});
	}
	int checkValid(Decl* decl){
		return hVars.find(decl) != hVars.end();
	}
	Htype getVar(Decl* decl) {
		assert(hVars.find(decl) != hVars.end());
		return hVars[decl];
	}
	int getVarInt(Decl* decl) {
		assert(hVars.find(decl) != hVars.end());
		return space[hVars[decl].idx];
	}
   int Malloc(int size) {
		int ret = spaceidx;
		for(int i = 0; i < size; i++) space[spaceidx++] = 0;
		return ret;
	}
   void Free (int addr) {
		// do nothing
	}
   void Update(int addr, int val) {
		assert(addr < spaceidx);
		space[addr] = val;
	}
   int get(int addr) {
		assert(addr < spaceidx);
		return space[addr];
	}
};


class Environment {
   std::vector<StackFrame> mStack;
	Heap heap;
	int isfuncRet;
   FunctionDecl * mFree;				/// Declartions to the built-in functions
   FunctionDecl * mMalloc;
   FunctionDecl * mInput;
   FunctionDecl * mOutput;

   FunctionDecl * mEntry;
	std::map<std::string, FunctionDecl*> funcDef;
public:
   /// Get the declartions to the built-in functions
   Environment() : mStack(), mFree(NULL), mMalloc(NULL), mInput(NULL), mOutput(NULL), mEntry(NULL), isfuncRet(0){
   }

	int getDeclVal(Decl* decl) {
		if(mStack.back().checkDeclValid(decl)) {
			return mStack.back().getDeclVal(decl);
		}
		return heap.getVarInt(decl);
	}
	uintptr_t getrefval(Vtype vtype) {
		if(vtype.ptr_sz == 1) return *(uint8_t*)vtype.ref;
		if(vtype.ptr_sz == 4) return *(uint32_t*)vtype.ref;
		if(vtype.ptr_sz == 8) return *(uint64_t*)vtype.ref;
		assert(0 && "invalid vtype size");
	}
	void setrefval(Vtype vtype, uintptr_t value, int sz) {
		if(sz == 1) *(uint8_t*)vtype.ref = value;
		if(sz == 4) *(uint32_t*)vtype.ref = value;
		if(sz == 8) *(uint64_t*)vtype.ref = value;
	}
	void addFunc(std::string str, FunctionDecl* func) {
		funcDef[str] = func;
	}
	FunctionDecl* getFunc(std::string str) {
		assert(funcDef.find(str) != funcDef.end());
		return funcDef[str];
	}
   /// Initialize the Environment
   void init(TranslationUnitDecl * unit) {
	   for (TranslationUnitDecl::decl_iterator i =unit->decls_begin(), e = unit->decls_end(); i != e; ++ i) {
		   if (FunctionDecl * fdecl = dyn_cast<FunctionDecl>(*i) ) {
			   if (fdecl->getName().equals("FREE")) mFree = fdecl;
			   else if (fdecl->getName().equals("MALLOC")) mMalloc = fdecl;
			   else if (fdecl->getName().equals("GET")) mInput = fdecl;
			   else if (fdecl->getName().equals("PRINT")) mOutput = fdecl;
			   else if (fdecl->getName().equals("main")) mEntry = fdecl;
				else {
					if(fdecl->hasBody()) {
						addFunc(fdecl->getNameAsString(), fdecl);
					}
				}
		   } else if (VarDecl* vardecl = dyn_cast<VarDecl>(*i)) {
				outerVarDecl(vardecl);
			}
	   }
	   mStack.push_back(StackFrame());
   }

   FunctionDecl * getEntry() {
	   return mEntry;
   }

   /// !TODO Support comparison operation
   void binop(BinaryOperator *bop) {
		if(isfuncRet) return;
	   Expr * left = bop->getLHS();
	   Expr * right = bop->getRHS();
		Vtype bop_type = {.val = 0};
	   if (bop->isAssignmentOp()) {
			Vtype val = mStack.back().getStmtVtype(right);
			bop_type.val = val.val;
		   if (DeclRefExpr * declexpr = dyn_cast<DeclRefExpr>(left)) {
				mStack.back().bindStmtVtype(left, val);
			   Decl * decl = declexpr->getFoundDecl();
				if (VarDecl * vardecl = dyn_cast<VarDecl>(decl)) {
					mStack.back().bindDeclVtypeExceptSz(vardecl, val);
		   	}
		   } else if(ArraySubscriptExpr * arrayexpr = dyn_cast<ArraySubscriptExpr>(left)) {
				Vtype type = mStack.back().getStmtVtype(left);
				*(type.ref) = val.val;
			} else if(UnaryOperator* unaryexpr = dyn_cast<UnaryOperator>(left)) {
				Vtype type = mStack.back().getStmtVtype(left);
				if(type.type == TREF) {
					mStack.back().bindStmtInt(left, val.val);
					setrefval(type, val.val, type.ptr_sz);
				} else {
					assert(0);
				}
			} else {
				assert(0);
			}
	   } else {
			Vtype type_l = mStack.back().getStmtVtype(left);
			bop_type = type_l;
			uintptr_t val_r = mStack.back().getStmtVal(right);
			uintptr_t val_l = mStack.back().getStmtVal(left);
			uintptr_t newval = 0;
			switch(bop->getOpcode()){
				case BO_Mul:	newval = val_r * val_l; break;
				case BO_Add:   val_r *= type_l.ptr_sz; newval = val_l + val_r; break;
				case BO_Sub:   val_r *= type_l.ptr_sz; newval = val_l - val_r; break;
				case BO_Shl:   newval = val_l << val_r; break;
				case BO_Shr:   newval = val_l >> val_r; break;
				case BO_EQ:  	newval = val_r == val_l; break;
				case BO_NE:  	newval = val_r != val_l; break;
				case BO_GT:  	newval = val_l > val_r; break;
				case BO_LT:  	newval = val_l < val_r; break;
				case BO_LE:		newval = val_l <= val_r; break;
				case BO_GE:		newval = val_l >= val_r; break;
				case BO_And:  	newval = val_r & val_l; break;
				case BO_Xor:  	newval = val_r ^ val_l; break;
				case BO_Or:  	newval = val_r | val_l; break;
				case BO_LAnd:  newval = val_r && val_l; break;
				case BO_LOr:  	newval = val_r || val_l; break;
				default:
					std::cout << "op = " << bop->getOpcode() << std::endl;
					assert(0 && "implement me!");
			}
			bop_type.val = newval;
		}
		mStack.back().bindStmtVtype(bop, bop_type);
   }

	void unaryop(UnaryOperator* uop) {
		if(isfuncRet) return;
		Vtype val = mStack.back().getStmtVtype(uop->getSubExpr());
		if(uop->getOpcode() == UO_Minus) {
			val.val = -val.val;
			mStack.back().bindStmtVtype(uop, val);
		} else if(uop->getOpcode() == UO_Deref) {
			val.type = TREF;
			mStack.back().bindStmtVtype(uop, val);
		}
	}

	void intLiteral(IntegerLiteral * intLiteral){
		mStack.back().bindStmtInt(intLiteral, intLiteral->getValue().getSExtValue());
	}

	void unaryTraitExpr(UnaryExprOrTypeTraitExpr* expr) {
		assert(expr->getKind() == UETT_SizeOf);
		if (expr->getArgumentType()->isIntegerType()) {
			mStack.back().bindStmtInt(expr, 4);
		} else if (expr->getArgumentType()->isPointerType()) {
			mStack.back().bindStmtInt(expr, 8);
		} else {
			assert(0);
		}
	}

   void decl(DeclStmt * declstmt) {
		if(isfuncRet) return;
	   for (DeclStmt::decl_iterator it = declstmt->decl_begin(), ie = declstmt->decl_end();
			   it != ie; ++ it) {
		   Decl * decl = *it;
		   if (VarDecl * vardecl = dyn_cast<VarDecl>(decl)) {
				innerVarDecl(vardecl);
		   }
	   }
   }

	void innerVarDecl(VarDecl* dec){
		if(isfuncRet) return;
		if (dec->hasInit()){
			APValue* value = dec->evaluateValue();
			assert(value->isInt());
			mStack.back().bindDeclInt(dec, value->getInt().getExtValue());
		} else {
			const Type* dectype = dec->getType().getTypePtr();
			if (dectype->isConstantArrayType()) {
				const ConstantArrayType* arrtype = dyn_cast<ConstantArrayType>(dectype);
				int entry_num = arrtype->getSize().getZExtValue();

				mStack.back().bindArrayDecl(dec, entry_num); //array num
			} else if (dectype->isArrayType()){
				assert(0);
			} else if(dectype->isPointerType()) {
				const PointerType* ptype = dectype->getAs<PointerType>();
				int ptr_sz = 1;
				if(ptype->getPointeeType()->isCharType()) {
					ptr_sz = 1;
				} else if (ptype->getPointeeType()->isIntegerType()) {
					ptr_sz = 4;
				} else if (ptype->getPointeeType()->isPointerType()) {
					ptr_sz = 8;
				} else {
					std::cout << "invalid pointer type " << dec->getType().getAsString() << std::endl;
					assert(0);
				}
				mStack.back().bindDeclVtype(dec, {.type=TINT, .val=0, .ptr_sz=ptr_sz});
			} else {
				mStack.back().bindDeclInt(dec, 0);

			}
		}
		return;
	}

	void outerVarDecl(VarDecl* dec) {
		if (dec->hasInit()) {
			APValue* value = dec->evaluateValue();
			assert(value->isInt());
			heap.bindInt(dec, value->getInt().getExtValue());
		} else {
			const Type* dectype = dec->getType().getTypePtr();
			if (dectype->isConstantArrayType()) {
				assert(0);
			} else if (dectype->isArrayType()){
				assert(0);
			} else{
				heap.bindInt(dec, 0);
			}
		}
	}

   void declref(DeclRefExpr * declref) {
		if(isfuncRet) return;
	   mStack.back().setPC(declref);
	   if (declref->getType()->isIntegerType()) {
		   Decl* decl = declref->getFoundDecl();
		   int val = this->getDeclVal(decl);
		   mStack.back().bindStmtInt(declref, val);
	   } else if(declref->getType()->isArrayType()) {
			Decl* decl = declref->getFoundDecl();
			Vtype vtype = mStack.back().getDeclVtype(decl);
			mStack.back().bindStmtVtype(declref, vtype);
		} else if (declref->getType()->isPointerType()) {
			Decl* decl = declref->getFoundDecl();
			Vtype vtype = mStack.back().getDeclVtype(decl);
			vtype.type = TINT;
			// TODO: all declref is TINT
			mStack.back().bindStmtVtype(declref, vtype);
		} else {
			Vtype dummyVtype;
			mStack.back().bindStmtVtype(declref, dummyVtype);
			// std::cout << "Invalid Type\n";
		}
   }

   void cast(CastExpr * castexpr) {
		if(isfuncRet) return;
	   mStack.back().setPC(castexpr);
		QualType castQType = castexpr->getType();
		const Type* casttype = castQType.getTypePtr();
		Vtype val = mStack.back().getStmtVtype(castexpr->getSubExpr());
		if(castexpr->getCastKind() == CK_LValueToRValue) {
			if (val.type == TREF) {
				val.val = getrefval(val);
				val.type = TINT;
			}
		} else if(castexpr->getCastKind() == CK_BitCast) {
			//TODO
		}
		mStack.back().bindStmtVtype(castexpr, val);
		return;
#if 0
	   if (castexpr->getType()->isIntegerType()) {
		   Expr * expr = castexpr->getSubExpr();
		   int val = mStack.back().getStmtVal(expr);
		   mStack.back().bindStmtInt(castexpr, val);
	   } else if (casttype->isPointerType()) {
			Vtype val = mStack.back().getStmtVtype(castexpr->getSubExpr());
			mStack.back().bindStmtVtype(castexpr, val);
		} else{
			std::cout << "Invalid cast type " << castexpr->getType().getAsString() << std::endl;
		}
#endif
   }

   /// !TODO Support Function Call
   FunctionDecl* call(CallExpr * callexpr) {
		if(isfuncRet) return 0;
	   mStack.back().setPC(callexpr);
	   int val = 0;
	   FunctionDecl * callee = callexpr->getDirectCallee();
	   if (callee == mInput) {
		  llvm::errs() << "Please Input an Integer Value : ";
		  scanf("%d", &val);
		  mStack.back().bindStmtInt(callexpr, val);
		  return 0;
	   } else if (callee == mOutput) {
		   Expr * decl = callexpr->getArg(0);
		   val = mStack.back().getStmtVal(decl);
		   llvm::errs() << val;
			#ifdef LOCAL
			llvm::errs() << "\n";
			#endif
			return 0;
	   } else if(callee == mMalloc) {
			Expr * decl = callexpr->getArg(0);
		   val = mStack.back().getStmtVal(decl);
			mStack.back().bindStmtRef(callexpr, malloc(val));
			return 0;
		} else if(callee == mFree) {
			Expr * decl = callexpr->getArg(0);
			free(mStack.back().getStmtAddr(decl));
			return 0;
		} else {
			std::string calleeName = callexpr->getDirectCallee()->getNameAsString();
			FunctionDecl* funcdecl = getFunc(calleeName);
			std::vector<uintptr_t> args(callexpr->getNumArgs(), 0);
			for(int i = 0; i < callexpr->getNumArgs(); i++) {
				Expr * decl = callexpr->getArg(i);
				args[i] = mStack.back().getStmtVal(decl);
			}
			mStack.push_back(StackFrame()); // new frame
			for(int i = 0; i < funcdecl->getNumParams(); i++) {
				mStack.back().bindDeclInt(funcdecl->getParamDecl(i), args[i]);
			}
		   /// You could add your code here for Function call Return
			return funcdecl;
	   }
		return 0;
   }

	void funcRet(CallExpr* call) {
		Stmt* ret = mStack.back().getret();
		Vtype val;
		if(ret) val = mStack.back().getStmtVtype(mStack.back().getret());
		mStack.pop_back();
		if (ret) mStack.back().bindStmtVtype(call, val);
		isfuncRet = 0;
	}

	void returnStmt(ReturnStmt* stmt) {
		if(isfuncRet) return;
		Vtype val = mStack.back().getStmtVtype(stmt->getRetValue());
		mStack.back().bindStmtVtype(stmt, val);
		mStack.back().setret(stmt);
		isfuncRet = 1;
	}

	Stmt* ifStmt(IfStmt * ifstmt){
		if(isfuncRet) return 0;
		Expr *cond = ifstmt->getCond();
		int val = mStack.back().getStmtVal(cond);
		if(val) {
			Stmt* thenstmt = ifstmt->getThen();
			return thenstmt;
		} else {
			return ifstmt->getElse();
		}
	}

	void arrayExpr(ArraySubscriptExpr* expr) {
		if(isfuncRet) return;
		Vtype vidx = mStack.back().getStmtVtype(expr->getIdx());
		VType	vbase = mStack.back().getStmtVtype(expr->getBase());
		Vtype vtype = {.type = TREF, .ref = mStack.back().getref(vbase.idx + vidx.val)};
		mStack.back().bindStmtVtype(expr, vtype);
	}

	void parenExpr(ParenExpr* expr) {
		Vtype val = mStack.back().getStmtVtype(expr->getSubExpr());
		mStack.back().bindStmtVtype(expr, val);
	}

	int getTopStmtVal(Stmt* stmt){
		return mStack.back().getStmtVal(stmt);
	}

	int checkFinish() {
		return isfuncRet;
	}
};


