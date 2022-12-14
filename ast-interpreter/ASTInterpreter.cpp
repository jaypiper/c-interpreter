//==--- tools/clang-check/ClangInterpreter.cpp - Clang Interpreter tool --------------===//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/EvaluatedExprVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/Tooling.h"

using namespace clang;

#include "Environment.h"

class InterpreterVisitor : 
   public EvaluatedExprVisitor<InterpreterVisitor> {
public:
   explicit InterpreterVisitor(const ASTContext &context, Environment * env)
   : EvaluatedExprVisitor(context), mEnv(env) {}
   virtual ~InterpreterVisitor() {}

   virtual void VisitBinaryOperator (BinaryOperator * bop) {
	   VisitStmt(bop);
	   mEnv->binop(bop);
   }
   virtual void VisitUnaryOperator(UnaryOperator * uop){
      VisitStmt(uop);
      mEnv->unaryop(uop);
   }
   virtual void VisitDeclRefExpr(DeclRefExpr * expr) {
	   VisitStmt(expr);
	   mEnv->declref(expr);
   }
   virtual void VisitCastExpr(CastExpr * expr) {
	   VisitStmt(expr);
	   mEnv->cast(expr);
   }
   virtual void VisitCallExpr(CallExpr * call) {
	   VisitStmt(call);
	   FunctionDecl* callee = mEnv->call(call);
      if(callee) {
         if(callee->hasBody()) VisitStmt(callee->getBody());
         mEnv->funcRet(call);
      }
   }
   virtual void VisitDeclStmt(DeclStmt * declstmt) {
	   mEnv->decl(declstmt);
   }
   virtual void VisitIntegerLiteral(IntegerLiteral * intLiteral) {
      mEnv->intLiteral(intLiteral);
   }
   virtual void VisitUnaryExprOrTypeTraitExpr(UnaryExprOrTypeTraitExpr* expr) {
      mEnv->unaryTraitExpr(expr);
   }
   virtual void VisitVarDecl(VarDecl* varDecl) {
      mEnv->innerVarDecl(varDecl);
   }
   virtual void VisitIfStmt(IfStmt * ifStmt) {
      Stmt* stmt = *(ifStmt->children().begin());
      this->Visit(stmt);
      Stmt* nextStmt = mEnv->ifStmt(ifStmt);
      if(nextStmt)
         this->Visit(nextStmt);
   }
   virtual void VisitWhileStmt(WhileStmt* whileStmt){
      int isFinish = mEnv->checkFinish();
      while(!isFinish){
         this->Visit(whileStmt->getCond());
         isFinish = mEnv->getTopStmtVal(whileStmt->getCond()) == 0;
         if(!isFinish) this->Visit(whileStmt->getBody());
      }
   }
   virtual void VisitForStmt(ForStmt* forStmt) {
      if(forStmt->getInit()) this->Visit(forStmt->getInit());
      int isFinish = mEnv->checkFinish();
      while(!isFinish) {
         this->Visit(forStmt->getCond());
         isFinish = mEnv->getTopStmtVal(forStmt->getCond()) == 0;
         if(isFinish) break;
         if(forStmt->getBody()) this->Visit(forStmt->getBody());
         this->Visit(forStmt->getInc());
      }
   }
   virtual void VisitArraySubscriptExpr(ArraySubscriptExpr* expr){
      VisitStmt(expr);
      mEnv->arrayExpr(expr);
   }
   virtual void VisitReturnStmt(ReturnStmt* stmt) {
      VisitStmt(stmt);
      mEnv->returnStmt(stmt);
   }
   virtual void VisitParenExpr(ParenExpr* expr) {
      VisitStmt(expr);
      mEnv->parenExpr(expr);
   }

private:
   Environment * mEnv;
};

class InterpreterConsumer : public ASTConsumer {
public:
   explicit InterpreterConsumer(const ASTContext& context) : mEnv(),
   	   mVisitor(context, &mEnv) {
   }
   virtual ~InterpreterConsumer() {}

   virtual void HandleTranslationUnit(clang::ASTContext &Context) {
	   TranslationUnitDecl * decl = Context.getTranslationUnitDecl();
	   mEnv.init(decl);

	   FunctionDecl * entry = mEnv.getEntry();
	   mVisitor.VisitStmt(entry->getBody());
  }
private:
   Environment mEnv;
   InterpreterVisitor mVisitor;
};

class InterpreterClassAction : public ASTFrontendAction {
public: 
  virtual std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
    clang::CompilerInstance &Compiler, llvm::StringRef InFile) {
    return std::unique_ptr<clang::ASTConsumer>(
        new InterpreterConsumer(Compiler.getASTContext()));
  }
};

int main (int argc, char ** argv) {
   if (argc > 1) {
       clang::tooling::runToolOnCode(std::unique_ptr<clang::FrontendAction>(new InterpreterClassAction), argv[1]);
   }
}
