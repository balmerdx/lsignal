#pragma once

// Empty in this repository: lsignal is always compiled straight into the
// consumer here (lsignal.cpp added directly to the build), so there is no
// DLL/shared-library boundary to cross. LSIGNAL_DLL exists so this same
// lsignal.h/lsignal.cpp pair can also be dropped into a project that builds
// lsignal as its own shared library (a Windows .dll via MSVC, or the
// visibility-attribute equivalent elsewhere) - there this file becomes
// something like:
//
//   #ifdef LSIGNAL_BUILDING_DLL
//   #define LSIGNAL_DLL __declspec(dllexport)
//   #else
//   #define LSIGNAL_DLL __declspec(dllimport)
//   #endif
//
// (LSIGNAL_BUILDING_DLL defined only when compiling lsignal.cpp itself into
// the DLL; every consumer TU picks up the dllimport branch instead). See
// lsignal.h for which classes it's applied to and why, and the root
// README.md's "lsignal_with_cpp vs lsignal_header_only" section for the
// trade this whole directory exists for. Building or testing an actual
// .so/.dll is not covered by this repository.
#define LSIGNAL_DLL
