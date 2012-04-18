//===- unittest/Tooling/RefactoringTest.cpp - Refactoring unit tests ------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclGroup.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Tooling/Refactoring.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/DiagnosticOptions.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"
#include "clang/Rewrite/Rewriter.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_os_ostream.h"
#include "gtest/gtest.h"

namespace clang {
namespace tooling {

class RewriterTestContext {
 public:
  RewriterTestContext()
      : Diagnostics(llvm::IntrusiveRefCntPtr<DiagnosticIDs>()),
        DiagnosticPrinter(llvm::outs(), DiagnosticOptions()),
        Files((FileSystemOptions())),
        Sources(Diagnostics, Files),
        Rewrite(Sources, Options) {
    Diagnostics.setClient(&DiagnosticPrinter, false);
  }

  FileID CreateInMemoryFile(llvm::StringRef Name, llvm::StringRef Content) {
    const llvm::MemoryBuffer *Source =
      llvm::MemoryBuffer::getMemBuffer(Content);
    const FileEntry *Entry =
      Files.getVirtualFile(Name, Source->getBufferSize(), 0);
    Sources.overrideFileContents(Entry, Source, true);
    assert(Entry != NULL);
    return Sources.createFileID(Entry, SourceLocation(), SrcMgr::C_User);
  }

  SourceLocation GetLocation(FileID ID, unsigned Line, unsigned Column) {
    SourceLocation Result = Sources.translateFileLineCol(
        Sources.getFileEntryForID(ID), Line, Column);
    assert(Result.isValid());
    return Result;
  }

  std::string GetRewrittenText(FileID ID) {
    std::string Result;
    llvm::raw_string_ostream OS(Result);
    Rewrite.getEditBuffer(ID).write(OS);
    return Result;
  }

  Replacement CreateReplacement(SourceLocation Start, unsigned Length,
                                llvm::StringRef ReplacementText) {
    return Replacement(Sources, Start, Length, ReplacementText);
  }

  DiagnosticsEngine Diagnostics;
  TextDiagnosticPrinter DiagnosticPrinter;
  FileManager Files;
  SourceManager Sources;
  LangOptions Options;
  Rewriter Rewrite;
};

class ReplacementTest : public ::testing::Test {
 protected:
  RewriterTestContext Context;
};

TEST_F(ReplacementTest, CanDeleteAllText) {
  FileID ID = Context.CreateInMemoryFile("input.cpp", "text");
  SourceLocation Location = Context.GetLocation(ID, 1, 1);
  Replacement Replace(Context.CreateReplacement(Location, 4, ""));
  EXPECT_TRUE(Replace.Apply(Context.Rewrite));
  EXPECT_EQ("", Context.GetRewrittenText(ID));
}

TEST_F(ReplacementTest, CanDeleteAllTextInTextWithNewlines) {
  FileID ID = Context.CreateInMemoryFile("input.cpp", "line1\nline2\nline3");
  SourceLocation Location = Context.GetLocation(ID, 1, 1);
  Replacement Replace(Context.CreateReplacement(Location, 17, ""));
  EXPECT_TRUE(Replace.Apply(Context.Rewrite));
  EXPECT_EQ("", Context.GetRewrittenText(ID));
}

TEST_F(ReplacementTest, CanAddText) {
  FileID ID = Context.CreateInMemoryFile("input.cpp", "");
  SourceLocation Location = Context.GetLocation(ID, 1, 1);
  Replacement Replace(Context.CreateReplacement(Location, 0, "result"));
  EXPECT_TRUE(Replace.Apply(Context.Rewrite));
  EXPECT_EQ("result", Context.GetRewrittenText(ID));
}

TEST_F(ReplacementTest, CanReplaceTextAtPosition) {
  FileID ID = Context.CreateInMemoryFile("input.cpp",
                                         "line1\nline2\nline3\nline4");
  SourceLocation Location = Context.GetLocation(ID, 2, 3);
  Replacement Replace(Context.CreateReplacement(Location, 12, "x"));
  EXPECT_TRUE(Replace.Apply(Context.Rewrite));
  EXPECT_EQ("line1\nlixne4", Context.GetRewrittenText(ID));
}

TEST_F(ReplacementTest, CanReplaceTextAtPositionMultipleTimes) {
  FileID ID = Context.CreateInMemoryFile("input.cpp",
                                         "line1\nline2\nline3\nline4");
  SourceLocation Location1 = Context.GetLocation(ID, 2, 3);
  Replacement Replace1(Context.CreateReplacement(Location1, 12, "x\ny\n"));
  EXPECT_TRUE(Replace1.Apply(Context.Rewrite));
  EXPECT_EQ("line1\nlix\ny\nne4", Context.GetRewrittenText(ID));

  // Since the original source has not been modified, the (4, 4) points to the
  // 'e' in the original content.
  SourceLocation Location2 = Context.GetLocation(ID, 4, 4);
  Replacement Replace2(Context.CreateReplacement(Location2, 1, "f"));
  EXPECT_TRUE(Replace2.Apply(Context.Rewrite));
  EXPECT_EQ("line1\nlix\ny\nnf4", Context.GetRewrittenText(ID));
}

TEST_F(ReplacementTest, ApplyFailsForNonExistentLocation) {
  Replacement Replace("nonexistent-file.cpp", 0, 1, "");
  EXPECT_FALSE(Replace.Apply(Context.Rewrite));
}

TEST_F(ReplacementTest, CanRetrivePath) {
  Replacement Replace("/path/to/file.cpp", 0, 1, "");
  EXPECT_EQ("/path/to/file.cpp", Replace.GetFilePath());
}

TEST_F(ReplacementTest, ReturnsInvalidPath) {
  Replacement Replace1(Context.Sources, SourceLocation(), 0, "");
  EXPECT_EQ("invalid-location", Replace1.GetFilePath());

  Replacement Replace2;
  EXPECT_EQ("invalid-location", Replace2.GetFilePath());
}

TEST_F(ReplacementTest, CanApplyReplacements) {
  FileID ID = Context.CreateInMemoryFile("input.cpp",
                                         "line1\nline2\nline3\nline4");
  Replacements Replaces;
  Replaces.insert(Replacement(Context.Sources, Context.GetLocation(ID, 2, 1),
                              5, "replaced"));
  Replaces.insert(Replacement(Context.Sources, Context.GetLocation(ID, 3, 1),
                              5, "other"));
  EXPECT_TRUE(ApplyAllReplacements(Replaces, Context.Rewrite));
  EXPECT_EQ("line1\nreplaced\nother\nline4", Context.GetRewrittenText(ID));
}

TEST_F(ReplacementTest, SkipsDuplicateReplacements) {
  FileID ID = Context.CreateInMemoryFile("input.cpp",
                                         "line1\nline2\nline3\nline4");
  Replacements Replaces;
  Replaces.insert(Replacement(Context.Sources, Context.GetLocation(ID, 2, 1),
                              5, "replaced"));
  Replaces.insert(Replacement(Context.Sources, Context.GetLocation(ID, 2, 1),
                              5, "replaced"));
  Replaces.insert(Replacement(Context.Sources, Context.GetLocation(ID, 2, 1),
                              5, "replaced"));
  EXPECT_TRUE(ApplyAllReplacements(Replaces, Context.Rewrite));
  EXPECT_EQ("line1\nreplaced\nline3\nline4", Context.GetRewrittenText(ID));
}

TEST_F(ReplacementTest, ApplyAllFailsIfOneApplyFails) {
  // This test depends on the value of the file name of an invalid source
  // location being in the range ]a, z[.
  FileID IDa = Context.CreateInMemoryFile("a.cpp", "text");
  FileID IDz = Context.CreateInMemoryFile("z.cpp", "text");
  Replacements Replaces;
  Replaces.insert(Replacement(Context.Sources, Context.GetLocation(IDa, 1, 1),
                              4, "a"));
  Replaces.insert(Replacement(Context.Sources, SourceLocation(),
                              5, "2"));
  Replaces.insert(Replacement(Context.Sources, Context.GetLocation(IDz, 1, 1),
                              4, "z"));
  EXPECT_FALSE(ApplyAllReplacements(Replaces, Context.Rewrite));
  EXPECT_EQ("a", Context.GetRewrittenText(IDa));
  EXPECT_EQ("z", Context.GetRewrittenText(IDz));
}

class FlushRewrittenFilesTest : public ::testing::Test {
 public:
  FlushRewrittenFilesTest() {
    std::string ErrorInfo;
    TemporaryDirectory = llvm::sys::Path::GetTemporaryDirectory(&ErrorInfo);
    assert(ErrorInfo.empty());
  }

  ~FlushRewrittenFilesTest() {
    std::string ErrorInfo;
    TemporaryDirectory.eraseFromDisk(true, &ErrorInfo);
    assert(ErrorInfo.empty());
  }

  FileID CreateFile(llvm::StringRef Name, llvm::StringRef Content) {
    llvm::SmallString<1024> Path(TemporaryDirectory.str());
    llvm::sys::path::append(Path, Name);
    std::string ErrorInfo;
    llvm::raw_fd_ostream OutStream(Path.c_str(),
                                   ErrorInfo, llvm::raw_fd_ostream::F_Binary);
    assert(ErrorInfo.empty());
    OutStream << Content;
    OutStream.close();
    const FileEntry *File = Context.Files.getFile(Path);
    assert(File != NULL);
    return Context.Sources.createFileID(File, SourceLocation(), SrcMgr::C_User);
  }

  std::string GetFileContentFromDisk(llvm::StringRef Name) {
    llvm::SmallString<1024> Path(TemporaryDirectory.str());
    llvm::sys::path::append(Path, Name);
    // We need to read directly from the FileManager without relaying through
    // a FileEntry, as otherwise we'd read through an already opened file
    // descriptor, which might not see the changes made.
    // FIXME: Figure out whether there is a way to get the SourceManger to
    // reopen the file.
    return Context.Files.getBufferForFile(Path, NULL)->getBuffer();
  }

  llvm::sys::Path TemporaryDirectory;
  RewriterTestContext Context;
};

TEST_F(FlushRewrittenFilesTest, StoresChangesOnDisk) {
  FileID ID = CreateFile("input.cpp", "line1\nline2\nline3\nline4");
  Replacements Replaces;
  Replaces.insert(Replacement(Context.Sources, Context.GetLocation(ID, 2, 1),
                              5, "replaced"));
  EXPECT_TRUE(ApplyAllReplacements(Replaces, Context.Rewrite));
  EXPECT_TRUE(SaveRewrittenFiles(Context.Rewrite));
  EXPECT_EQ("line1\nreplaced\nline3\nline4",
            GetFileContentFromDisk("input.cpp"));
}

namespace {
template <typename T>
class TestVisitor : public clang::RecursiveASTVisitor<T> {
public:
  bool runOver(StringRef Code) {
    return runToolOnCode(new TestAction(this), Code);
  }

protected:
  clang::SourceManager *SM;

private:
  class FindConsumer : public clang::ASTConsumer {
  public:
    FindConsumer(TestVisitor *Visitor) : Visitor(Visitor) {}

    virtual void HandleTranslationUnit(clang::ASTContext &Context) {
      Visitor->TraverseDecl(Context.getTranslationUnitDecl());
    }

  private:
    TestVisitor *Visitor;
  };

  class TestAction : public clang::ASTFrontendAction {
  public:
    TestAction(TestVisitor *Visitor) : Visitor(Visitor) {}

    virtual clang::ASTConsumer* CreateASTConsumer(
        clang::CompilerInstance& compiler, llvm::StringRef dummy) {
      Visitor->SM = &compiler.getSourceManager();
      /// TestConsumer will be deleted by the framework calling us.
      return new FindConsumer(Visitor);
    }

  private:
    TestVisitor *Visitor;
  };
};
} // end namespace

void expectReplacementAt(const Replacement &Replace,
                         StringRef File, unsigned Offset, unsigned Length) {
  ASSERT_TRUE(Replace.IsApplicable());
  EXPECT_EQ(File, Replace.GetFilePath());
  EXPECT_EQ(Offset, Replace.GetOffset());
  EXPECT_EQ(Length, Replace.GetLength());
}

class ClassDeclXVisitor : public TestVisitor<ClassDeclXVisitor> {
public:
  bool VisitCXXRecordDecl(CXXRecordDecl *Record) {
    if (Record->getName() == "X") {
      Replace = Replacement(*SM, Record, "");
    }
    return true;
  }
  Replacement Replace;
};

TEST(Replacement, CanBeConstructedFromNode) {
  ClassDeclXVisitor ClassDeclX;
  EXPECT_TRUE(ClassDeclX.runOver("     class X;"));
  expectReplacementAt(ClassDeclX.Replace, "input.cc", 5, 7);
}

TEST(Replacement, ReplacesAtSpellingLocation) {
  ClassDeclXVisitor ClassDeclX;
  EXPECT_TRUE(ClassDeclX.runOver("#define A(Y) Y\nA(class X);"));
  expectReplacementAt(ClassDeclX.Replace, "input.cc", 17, 7);
}

class CallToFVisitor : public TestVisitor<CallToFVisitor> {
public:
  bool VisitCallExpr(CallExpr *Call) {
    llvm::errs() << Call->getDirectCallee()->getName() << "\n";
    if (Call->getDirectCallee()->getName() == "F") {
      Replace = Replacement(*SM, Call, "");
    }
    return true;
  }
  Replacement Replace;
};

TEST(Replacement, FunctionCall) {
  CallToFVisitor CallToF;
  EXPECT_TRUE(CallToF.runOver("void F(); void G() { F(); }"));
  expectReplacementAt(CallToF.Replace, "input.cc", 21, 3);
}

TEST(Replacement, TemplatedFunctionCall) {
  CallToFVisitor CallToF;
  EXPECT_TRUE(CallToF.runOver(
        "template <typename T> void F(); void G() { F<int>(); }"));
  expectReplacementAt(CallToF.Replace, "input.cc", 43, 8);
}

} // end namespace tooling
} // end namespace clang
