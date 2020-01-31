// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include <iostream>
#include <memory>

#include <git2.h>

void GetBranchAndCommitName(const char* repoPath, std::string* branchName, std::string* commitName) {
  git_libgit2_init();
  
  // Open git repo
  git_repository* repo = nullptr;
  int result = git_repository_open_ext(&repo, repoPath, GIT_REPOSITORY_OPEN_NO_SEARCH, nullptr);
  std::shared_ptr<git_repository> repo_deleter(repo, [&](git_repository* repo){ git_repository_free(repo); });
  if (result == GIT_ENOTFOUND) {
    // There is no git repository at the project path.
    return;
  } else if (result != 0) {
    std::cout << "Failed to open the git repository at " << repoPath << " (some possible reasons: repo corruption or system errors)\n";
    return;
  }
  
  // Get the branch name for HEAD
  git_reference* head = nullptr;
  result = git_repository_head(&head, repo);
  std::shared_ptr<git_reference> head_deleter(head, [&](git_reference* ref){ git_reference_free(ref); });
  
  if (result == GIT_EUNBORNBRANCH || result == GIT_ENOTFOUND) {
    *branchName = "(not on any branch)";
  } else if (result != 0) {
    std::cout << "There was an error getting the branch for the git repository at " << repoPath << "\n";
  } else {
    *branchName = git_reference_shorthand(head);
  }
  
  // Get the abbreviated name of the last commit
  git_revwalk* walker;
  git_revwalk_new(&walker, repo);
  git_revwalk_sorting(walker, GIT_SORT_TOPOLOGICAL);
  git_revwalk_push_head(walker);
  
  git_oid oid;
  while (git_revwalk_next(&oid, walker) == 0) {
    char buffer[41];
    git_oid_fmt(buffer, &oid);
    buffer[40] = 0;
    *commitName = buffer;
    
    // NOTE: To query more information about the commit:
    // git_commit* commit;
    // if (git_commit_lookup(&commit, repo, &oid) != 0) {
    //   std::cout << "Failed to look up commit\n";
    //   git_revwalk_free(walker);
    //   return;
    // }
    // 
    // // ...
    // 
    // git_commit_free(commit);
    break;
  }
  
  git_revwalk_free(walker);
}

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cout << "Usage: GitVersionTool /path_to_git_directory /path_to/output_file\n";
    return 1;
  }
  
  const char* repoPath = argv[1];
  const char* outputPath = argv[2];
  
  std::string branchName = "";
  std::string commitName = "";
  
  // Get the current branch and commit name for the git repository at the given path.
  GetBranchAndCommitName(repoPath, &branchName, &commitName);
  
  // Create the formatted output.
  std::string output =
      "#pragma once\n"
      "\n"
      "constexpr const char* branchName = \"" + branchName + "\";\n"
      "constexpr const char* commitName = \"" + commitName + "\";\n";
  
  // Check if the output file contains the correct contents already.
  // In this case, we do not overwrite the file to prevent triggering re-compilation.
  std::string existingOutput;
  FILE* outputFile = fopen(outputPath, "rb");
  if (outputFile) {
    fseek(outputFile, 0, SEEK_END);
    int size = ftell(outputFile);
    char* buffer = new char[size + 1];
    existingOutput.resize(size);
    fseek(outputFile, 0, SEEK_SET);
    int numRead = fread(buffer, 1, size, outputFile);
    buffer[numRead] = 0;
    existingOutput = buffer;
    delete[] buffer;
    fclose(outputFile);
  }
  
  if (existingOutput != output) {
    outputFile = fopen(outputPath, "wb");
    if (!outputFile) {
      std::cout << "Failed to write file: " << outputPath << "\n";
      return 1;
    }
    fwrite(output.data(), 1, output.size(), outputFile);
    fclose(outputFile);
  }
  
  return 0;
}
