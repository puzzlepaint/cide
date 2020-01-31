// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <memory>
#include <vector>

#include <QString>

class Project;

/// Tries to guess whether the given path represents a C/C++/CUDA header or source file,
/// or instead another type of file (such as a plain text or CMake file, for example).
bool GuessIsCFile(const QString& path);

/// Tries to guess whether the given path represents a C/C++/CUDA header file (or a
/// C/C++ source file). If certain is not null, it will be set to true if the
/// function is certain that the file is a header, or to false if not.
bool GuessIsHeader(const QString& path, bool* certain);

/// Tries to find the corresponding header or source file for the file with the
/// given path. Returns an empty string if no corresponding file was found.
QString FindCorrespondingHeaderOrSource(const QString& path, const std::vector<std::shared_ptr<Project>>& projects);
