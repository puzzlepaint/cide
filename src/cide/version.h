// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <QString>

namespace version_info {
#include "CIDE_git_version.h"
}

// Un-comment this to set the release version.
#define RELEASE_VERSION "2020-02-01"

inline QString GetCIDEVersion() {
  #if defined(RELEASE_VERSION)
    return RELEASE_VERSION;
  #else
    // NOTE: This uses the first 7 characters of the commit hex string only.
    return QString::fromUtf8(version_info::branchName) + QStringLiteral(" - ") + QString::fromUtf8(version_info::commitName, 7);
  #endif
}
