### CIDE - A C/C++ IDE ###

CIDE aims to be a lightweight and fast IDE for C/C++, focusing on:

1. Making writing code as easy as possible
    * Fast code completion, partly automatic
    * Robustness to mistyping
2. Offering good code browsing capabilities
    * Hovering a variable/function call/etc. gives extensive information
    * On-the-fly member list for classes / structs

See the more comprehensive overview of features below.


### Screenshots ###

CIDE on Linux
![Screenshot on Linux](doc/screenshot.png?raw=true)

CIDE on Windows
![Screenshot on Windows](doc/screenshot_windows.png?raw=true)


### Overview of main features ###

* Syntax highlighting based on libclang
    * Inline error/warning display
    * Inline fix-it display, allowing to apply a fix with a single click
    * Optional per-variable coloring for local variables ("KDevelop-style")
* Fast code completion based on libclang
    * Robust completion matching, accounting for typos
    * "Implement function" completion
* Rich tooltips for hovered words
    * Easy navigation from declaration to definition and vice versa
    * On-the-fly member list for classes / structs
    * Integration of QtHelp files
* Quick navigation with a search bar supporting the following modes:
    * Local context search
    * Project file search
    * Global symbol search
* Editor features
    * Scrollbar minimap
    * Matching-bracket highlighting, for both brackets left and right of the cursor simultaneously
    * Automatic completion of some keyword phrases within code, such as "if ( ... ) { ... }"
    * Function to rename a variable/function/etc., updating all occurrences
* Generating header/source pairs from a configurable template
* Automatic temporary backups that can be restored in case of a crash
* Build integration, displaying only errors/warnings while skipping log noise
* Git integration
    * Live git diff display in the editor sidebar
    * Live git status display in the project tree

Since CIDE is still a new project, at the moment some features may be missing that you may expect.
In particular, be aware of the following:

* At the moment, CIDE has only *very* basic debugging support, which only works on Linux.
* CMake, version 3.14 or later, currently is the only supported build system for projects developed with CIDE.
* There is no support for high-DPI monitors.
* All files are assumed to be in UTF-8 encoding, and line endings are always saved as `\n` (not `\r\n`).


### Precompiled binaries ###

Binary releases are available on [GitHub releases](https://github.com/puzzlepaint/cide/releases).
Before using them, please also read the recommended setup steps below:

1. [Windows-specific setup](#windows-specific-setup)
2. [Initial setup](#initial-setup)


### Building ###

The following build dependencies must be present:

| Dependency                          | Version(s) known to work |
| ----------------------------------- | ------------------------ |
| [libclang](https://clang.llvm.org/) | 9.0.1, 10.0.0            |
| [Qt5](https://www.qt.io/)           | 5.12.0, 5.12.3, 5.15.0   |
| [libgit2](https://libgit2.org/)     | 0.28.4, 1.0.1            |

The following runtime dependencies should be present:

| Dependency                                                                       | Necessity                           | Purpose                                                                 |
| -------------------------------------------------------------------------------- | ----------------------------------- |  ---------------------------------------------------------------------- |
| [CMake](https://cmake.org/)                                                      | **Required**                        | Getting the parse settings. Version 3.14 or later is required           |
| [clang](https://clang.llvm.org/)                                                 | **Required**                        | Getting the default include and resource paths for configuring libclang |
| [make](https://www.gnu.org/software/make/) or [ninja](https://ninja-build.org/)  | Required for building applications  | Build integration                                                       |
| [gdb](https://www.gnu.org/software/gdb/)                                         | Required for debugging applications | Debugger                                                                |
| [konsole](https://konsole.kde.org/)                                              | Required for debugging applications | Terminal I/O for debugged applications                                  |


The application can be built via CMake, generating ninja build files (make does not work).

#### Building on Linux ####

Building on Linux was tested with gcc.

```bash
mkdir build
cd build
cmake -GNinja -DCMAKE_BUILD_TYPE=Release ..
ninja
```

#### Building on Windows ####

Building on Windows was tested with clang. Since clang alone does not come with all
required components, the "Build Tools for Visual Studio" must also be installed
(or Visual Studio itself). With clang, cmake, and ninja all in the PATH environment
variable, one can then create a `build` directory as in the Linux case and run the
following in a Command Prompt window in this directory:

```
REM Adjust this path to where you installed Visual Studio or its build tools.
"C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat"

REM Adjust the paths and LLVM version to your installation.
REM The LLVM paths and version are given manually here since the LLVM installer seems to come without llvm-config.
cmake ^
  -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_C_COMPILER:PATH="C:\Program Files\LLVM\bin\clang-cl.exe" ^
  -DCMAKE_CXX_COMPILER:PATH="C:\Program Files\LLVM\bin\clang-cl.exe" ^
  -DLLVM_ROOT="C:\Program Files\LLVM" ^
  -DLLVM_INCLUDE_DIRS="C:\Program Files\LLVM\include" ^
  -DLLVM_LIBRARY_DIRS="C:\Program Files\LLVM\lib" ^
  -DLLVM_VERSION="9.0.1" ^
  -DQt5_DIR=C:\...\Qt\5.12.3\msvc2017_64\lib\cmake\Qt5 ^
  -DLIBGIT2_INCLUDE_DIR="C:\...\libgit2-0.28.4\include" ^
  -DLIBGIT2_LIBRARIES="C:\...\libgit2-0.28.4\build\git2.lib" ^
  -DYAML_CPP_BUILD_TESTS=FALSE ^
  -DYAML_CPP_BUILD_TOOLS=FALSE ^
  ..

REM Before running ninja, copy git2.dll into the build directory. Otherwise, building will fail.

ninja
```

After building, copy all remaining required DLLs into the build directory, including `yaml-cpp.dll`
from within the `third_party` subfolder (trying to execute `CIDE.exe` should display which DLLs are missing).
Once all DLLs are present, the application should run.

Building with Visual Studio instead of ninja/clang should also work with a little manual effort.


### Windows-specific setup ###

On Windows, please ensure that the environment is set up for building when
starting CIDE. This likely means:

1. To run the batch script which sets the Visual Studio environment
   (see "Building on Windows" above) prior to running CIDE, and
2. Ensuring that the cmake, ninja, and clang binaries are in the PATH environment variable
   (in case you did not do this while installing them).

It might be easiest to write a .bat file to do the necessary setup and
then start CIDE, for example like this (if the PATH is already set correctly):

```
call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat"
start C:/Users/Thomas/Projects/cide/build/CIDE.exe
```

On Linux, the environment is likely already set up, so probably nothing
similar needs to be done.

### Initial setup ###

On the first start, CIDE should ask for the program to be configured. Here, in
particular the path to a clang binary should be set that will be used for
locating default include paths and clang's resource path. Ideally, this path
should be to a binary with the same version as the libclang that CIDE runs with.
This libclang version is printed to the terminal on startup. On Windows, choose
the `clang++.exe` binary variant.

Another useful optional setup step is to configure QtHelp files. CIDE normally uses
documentation comments in header files (e.g., comments starting with `///`)
for documentation display in code info tooltips. However, if such comments are
not available, QtHelp files can be used as an alternative. This is for example
useful to get documentation for standard C/C++ functionality or for the Qt5 library.

To set up these help files, go to Program settings -> Documentation files.
Add any downloaded .pch files to this list. For example, you may want to add:

* [The Qt help book from cppreference.com for standard C/C++ documentation](https://en.cppreference.com/w/Cppreference:Archives)
* The qch files that come with Qt5, in case you use this library

Note: It appears that adding such a file can take a long time on Windows. The
application will not react during this time.


### Default shortcuts reference ###

The list below only shows non-standard shortcuts.
Standard editor shortcuts (for example, Ctrl-C, Ctrl-V, ...) should work as usual.
Note that the shortcuts are configurable in the program settings.

&nbsp;

- **Ctrl - Space** : Manually (re)trigger code completion
- **Ctrl - Shift - A** : Fix all visible problems having a single fix-it
- **F1** : Open the currently visible QtHelp tooltip in a side dock (does not apply to standard tooltips)
- **F2** : Rename the item (variable, function, etc.) at the cursor

&nbsp;

- **Ctrl - D** : Comment current line / selection
- **Ctrl - Shift - D** : Uncomment current line / selection

&nbsp;

- **Ctrl - F** : Search
- **Ctrl - R** : Replace
- **F3** : Continue search
- **Shift - F3** : Continue search backwards
- **Ctrl - G** : Go to line
- **Ctrl - Alt - F** : Find in files

&nbsp;

- **F4** : Search for a project file
- **F5** : Search for a local context (i.e., a function or class/struct in the current file)
- **F6** : Search for a symbol globally

&nbsp;

- **Ctrl - Tab** : Switch between header and source file
- **Shift - Alt - Left** : Go to left tab
- **Shift - Alt - Right** : Go to right tab

&nbsp;

- **Ctrl - B**: Toggle bookmark at current line
- **Alt - PageUp** : Go to next bookmark above
- **Alt - PageDown** : Go to next bookmark below
- **Ctrl - Shift - B** : Remove all bookmarks

&nbsp;

- **F7** : Compile current build target (selectable in the main window toolbar)
- **F9** : Debug project


### Tips and tricks ###

* The program can be configured in the program settings, however, do not forget
  to also check the project settings for additional per-project options.
* Code completion works best if the setting for the key to accept the current completion item
  is set to the Tab key only, not the Return key, since the latter may easily cause code
  completion to be triggered accidentally. However, the default setting is to use both the
  Tab and Return keys, since it may be too confusing for new users otherwise.
* To rename a variable/function/etc., either right-click its name and choose
  the corresponding menu option, or set the cursor to its name and press F2.
* Hovering something in the code highlights all of its occurrences in the
  current file in green (if no text is selected).
* Selecting a word or series of words highlights all occurrences of this
  text in the current file in yellow.
* To quickly jump between declaration/definition, click the item in the code while
  holding the Ctrl key.
* To jump to the corresponding bracket for a given bracket, double-click it.
* Hover a `break` or `continue` statement to highlight the `for`, `do`, `while`,
  or `switch` statement that this breaks out of, respectively continues.
* To generate the definition syntax of a function automatically, first write the function's
  declaration, then press **Ctrl - Space** either in the same file, or in the
  corresponding source file if the declaration is in a header, and with the cursor
  outside of any function body. This should show a completion item suggesting
  to implement the new function.
* To auto-complete an override for a virtual function, set the cursor to the
  place where the override's declaration should be added, press **Ctrl - Space**
  to invoke code completion, and start typing the name of the function to
  override. This should select the completion for the override's declaration.
* The automatic expansions of, for example, "if" to "if () {}" and "else" to
  "else {}" can be configured / removed in the project settings, if desired.
* Tabs can be closed by middle-clicking them, via their right-click menu,
  or by pressing Ctrl - W.
* Good to know: The initial parse, and the reparse after the first edit to a
  newly opened file, will be slow. All further edits should however be
  *significantly* faster as long as the preamble (the initial section of a file,
  containing the includes) is not modified.
* The libclang mechanism which caches preambles and the crash backup mechanism
  of CIDE may write a lot of data to the temporary directory (usually /tmp on
  Linux and C:\Users&#92;[username]\AppData\Local\Temp on Windows). You might want
  to mount this directory in RAM.


### Contributing ###

Contributions to the project are welcome. Please try to follow the existing coding
style when adding code. Also, suggestions for new features, and ideas for how the
IDE could make the process of writing correct code even easier, are very much welcome
(although one should generally not expect them to get implemented).
Feel free to open GitHub issues for discussion.
