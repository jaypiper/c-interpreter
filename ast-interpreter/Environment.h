//==--- tools/clang-check/ClangInterpreter.cpp - Clang Interpreter tool --------------===//
//===----------------------------------------------------------------------===//
#include <stdio.h>
#include <iostream>
#include <vector>
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/Decl.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/OperationKinds.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/Tooling.h"

using namespace clang;

enum {TINVALID, TINT, TARRAY, TREF};
typedef struct VType{
	int type;
	union {
		int val;
		int idx;
		int* ref;
	};
}Vtype;

class StackFrame {
   /// StackFrame maps Variable Declaration to Value
   /// Which are either integer or addresses (also represented using an Integer value)
   std::map<Decl*, Vtype> mVars;
   std::map<Stmt*, Vtype> mExprs;
	std::vector<int> arrayVals;
   /// The current stmt
   Stmt * mPC;
public:
   StackFrame() : mVars(), mExprs(), mPC() {
   }

   void bindDeclInt(Decl* decl, int val) {
      mVars[decl].type = TINT;
		mVars[decl].val  = val;
   }
	void bindArrayDecl(Decl* decl, int num) {
		int idx = arrayVals.size();
		mVars[decl].type = TARRAY;
		mVars[decl].idx = idx;
		for(int i = 0; i < num; i++) {
			arrayVals.push_back(0);
		}
	}
	void bindDeclVtype(Decl* decl, Vtype type) {
		mVars[decl] = type;
	}
	int getDeclVal(Decl* decl) {
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
   void bindStmtInt(Stmt * stmt, int val) {
	   mExprs[stmt].type = TINT;
		mExprs[stmt].val = val;
   }
	void bindStmtVtype(Stmt* stmt, Vtype vtype) {
		mExprs[stmt] = vtype;
	}
   int getStmtVal(Stmt * stmt) {
	   assert (mExprs.find(stmt) != mExprs.end());
		if(mExprs[stmt].type == TREF) return *(mExprs[stmt].ref);
	   return mExprs[stmt].val;
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
};

/// Heap maps address to a value

typedef struct Htype{
	int idx;
	int length;
}Htype;

class Heap {
	std::vector<int> space;
	std::map<Decl*, Htype> hVars;
public:
	Heap() {
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
		int ret = space.size();
		for(int i = 0; i < size; i++) space.push_back(0);
		return ret;
	}
   void Free (int addr) {
		// do nothing
	}
   void Update(int addr, int val) {
		assert(addr < space.size());
		space[addr] = val;
	}
   int get(int addr) {
		assert(addr < space.size());
		return space[addr];
	}
};


class Environment {
   std::vector<StackFrame> mStack;
	Heap heap;

   FunctionDecl * mFree;				/// Declartions to the built-in functions
   FunctionDecl * mMalloc;
   FunctionDecl * mInput;
   FunctionDecl * mOutput;

   FunctionDecl * mEntry;
public:
   /// Get the declartions to the built-in functions
   Environment() : mStack(), mFree(NULL), mMalloc(NULL), mInput(NULL), mOutput(NULL), mEntry(NULL) {
   }

	int getDeclVal(Decl* decl) {
		if(mStack.back().checkDeclValid(decl)) {
			return mStack.back().getDeclVal(decl);
		}
		return heap.getVarInt(decl);
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
	   Expr * left = bop->getLHS();
	   Expr * right = bop->getRHS();
		int bop_val = 0;
	   if (bop->isAssignmentOp()) {
			int val = mStack.back().getStmtVal(right);
			bop_val = val;
		   if (DeclRefExpr * declexpr = dyn_cast<DeclRefExpr>(left)) {
		   	mStack.back().bindStmtInt(left, val);
			   Decl * decl = declexpr->getFoundDecl();
				if (VarDecl * vardecl = dyn_cast<VarDecl>(decl)) {
					mStack.back().bindDeclInt(vardecl, val);
		   	}
		   } else if(ArraySubscriptExpr * arrayexpr = dyn_cast<ArraySubscriptExpr>(left)) {
				Vtype type = mStack.back().getStmtVtype(left);
				*(type.ref) = val;
			} else {
				assert(0);
			}
	   } else {
			int val_r = mStack.back().getStmtVal(right);
			int val_l = mStack.back().getStmtVal(left);
			int newval = 0;
			switch(bop->getOpcode()){
				case BO_Mul:	newval = val_r * val_l; break;
				case BO_Add:   newval = val_l + val_r; break;
				case BO_Sub:   newval = val_l - val_r; break;
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
			bop_val = newval;
		}
		mStack.back().bindStmtInt(bop, bop_val);
   }

	void unaryop(UnaryOperator* uop) {
		int val = mStack.back().getStmtVal(uop->getSubExpr());
		if(uop->getOpcode() == UO_Minus) {
			mStack.back().bindStmtInt(uop, -val);
		}
	}

	void intLiteral(IntegerLiteral * intLiteral){
		mStack.back().bindStmtInt(intLiteral, intLiteral->getValue().getSExtValue());
	}

   void decl(DeclStmt * declstmt) {
	   for (DeclStmt::decl_iterator it = declstmt->decl_begin(), ie = declstmt->decl_end();
			   it != ie; ++ it) {
		   Decl * decl = *it;
		   if (VarDecl * vardecl = dyn_cast<VarDecl>(decl)) {
				innerVarDecl(vardecl);
		   }
	   }
   }

	void innerVarDecl(VarDecl* dec){
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
			} else{
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
	   mStack.back().setPC(declref);
	   if (declref->getType()->isIntegerType()) {
		   Decl* decl = declref->getFoundDecl();
		   int val = this->getDeclVal(decl);
		   mStack.back().bindStmtInt(declref, val);
	   } else if(declref->getType()->isArrayType()) {
			Decl* decl = declref->getFoundDecl();
			Vtype vtype = mStack.back().getDeclVtype(decl);
			mStack.back().bindStmtVtype(declref, vtype);
			
		} else {
			Vtype dummyVtype;
			mStack.back().bindStmtVtype(declref, dummyVtype);
			// std::cout << "Invalid Type\n";
		}
   }

   void cast(CastExpr * castexpr) {
	   mStack.back().setPC(castexpr);
		QualType castQType = castexpr->getType();
		const Type* casttype = castQType.getTypePtr();
		Vtype val = mStack.back().getStmtVtype(castexpr->getSubExpr());
		mStack.back().bindStmtVtype(castexpr, val);
		return;
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
   }

   /// !TODO Support Function Call
   void call(CallExpr * callexpr) {
	   mStack.back().setPC(callexpr);
	   int val = 0;
	   FunctionDecl * callee = callexpr->getDirectCallee();
	   if (callee == mInput) {
		  llvm::errs() << "Please Input an Integer Value : ";
		  scanf("%d", &val);

		  mStack.back().bindStmtInt(callexpr, val);
	   } else if (callee == mOutput) {
		   Expr * decl = callexpr->getArg(0);
		   val = mStack.back().getStmtVal(decl);
		   llvm::errs() << val;
	   } else {
		   /// You could add your code here for Function call Return
	   }
   }

	Stmt* ifStmt(IfStmt * ifstmt){
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
		Vtype vidx = mStack.back().getStmtVtype(expr->getIdx());
		VType	vbase = mStack.back().getStmtVtype(expr->getBase());
		Vtype vtype = {.type = TREF, .ref = mStack.back().getref(vbase.idx + vidx.val)};
		mStack.back().bindStmtVtype(expr, vtype);
	}

	int getTopStmtVal(Stmt* stmt){
		return mStack.back().getStmtVal(stmt);
	}
};


