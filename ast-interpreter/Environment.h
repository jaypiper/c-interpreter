//==--- tools/clang-check/ClangInterpreter.cpp - Clang Interpreter tool --------------===//
//===----------------------------------------------------------------------===//
#include <stdio.h>
#include <iostream>
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/Decl.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/OperationKinds.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/Tooling.h"

using namespace clang;

class StackFrame {
   /// StackFrame maps Variable Declaration to Value
   /// Which are either integer or addresses (also represented using an Integer value)
   std::map<Decl*, int> mVars;
   std::map<Stmt*, int> mExprs;
   /// The current stmt
   Stmt * mPC;
public:
   StackFrame() : mVars(), mExprs(), mPC() {
   }

   void bindDecl(Decl* decl, int val) {
      mVars[decl] = val;
   }    
   int getDeclVal(Decl * decl) {
      assert (mVars.find(decl) != mVars.end());
      return mVars.find(decl)->second;
   }
   void bindStmt(Stmt * stmt, int val) {
	   mExprs[stmt] = val;
   }
   int getStmtVal(Stmt * stmt) {
	   assert (mExprs.find(stmt) != mExprs.end());
	   return mExprs[stmt];
   }
   void setPC(Stmt * stmt) {
	   mPC = stmt;
   }
   Stmt * getPC() {
	   return mPC;
   }
};

/// Heap maps address to a value
/*
class Heap {
public:
   int Malloc(int size) ;
   void Free (int addr) ;
   void Update(int addr, int val) ;
   int get(int addr);
};
*/

class Environment {
   std::vector<StackFrame> mStack;

   FunctionDecl * mFree;				/// Declartions to the built-in functions
   FunctionDecl * mMalloc;
   FunctionDecl * mInput;
   FunctionDecl * mOutput;

   FunctionDecl * mEntry;
public:
   /// Get the declartions to the built-in functions
   Environment() : mStack(), mFree(NULL), mMalloc(NULL), mInput(NULL), mOutput(NULL), mEntry(NULL) {
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

	   if (bop->isAssignmentOp()) {
		   int val = mStack.back().getStmtVal(right);
		   mStack.back().bindStmt(left, val);
		   if (DeclRefExpr * declexpr = dyn_cast<DeclRefExpr>(left)) {
			   Decl * decl = declexpr->getFoundDecl();
				if (VarDecl * vardecl = dyn_cast<VarDecl>(decl)) {
					mStack.back().bindDecl(vardecl, val);
		   	}
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
			mStack.back().bindStmt(bop, newval);
		}
   }

	void unaryop(UnaryOperator* uop) {
		int val = mStack.back().getStmtVal(uop->getSubExpr());
		if(uop->getOpcode() == UO_Minus) {
			mStack.back().bindStmt(uop, -val);
		}
	}

	void intLiteral(IntegerLiteral * intLiteral){
		mStack.back().bindStmt(intLiteral, intLiteral->getValue().getSExtValue());
	}

   void decl(DeclStmt * declstmt) {
	   for (DeclStmt::decl_iterator it = declstmt->decl_begin(), ie = declstmt->decl_end();
			   it != ie; ++ it) {
		   Decl * decl = *it;
		   if (VarDecl * vardecl = dyn_cast<VarDecl>(decl)) {
				varDecl(vardecl);
		   }
	   }
   }

	void varDecl(VarDecl* dec){
		if (dec->hasInit()){
			APValue* value = dec->evaluateValue();
			assert(value->isInt());
			mStack.back().bindDecl(dec, value->getInt().getExtValue());
		} else {
			mStack.back().bindDecl(dec, 0);
		}
		return;
	}

   void declref(DeclRefExpr * declref) {
	   mStack.back().setPC(declref);
	   if (declref->getType()->isIntegerType()) {
		   Decl* decl = declref->getFoundDecl();

		   int val = mStack.back().getDeclVal(decl);
		   mStack.back().bindStmt(declref, val);
	   }
   }

   void cast(CastExpr * castexpr) {
	   mStack.back().setPC(castexpr);
	   if (castexpr->getType()->isIntegerType()) {
		   Expr * expr = castexpr->getSubExpr();
		   int val = mStack.back().getStmtVal(expr);
		   mStack.back().bindStmt(castexpr, val );
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

		  mStack.back().bindStmt(callexpr, val);
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

	int whileStmt(WhileStmt* whilestmt){
		int val = mStack.back().getStmtVal(whilestmt->getCond());
		if (val) return 0;  	// not finish
		else return 1;			// is finish
	}
};


