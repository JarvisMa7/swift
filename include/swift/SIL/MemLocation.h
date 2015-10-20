//===------------------------ MemLocation.h ----------------------------- -===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2015 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//
//
// This file defines the class Location. A MemLocation is an abstraction of an
// object field in program. It consists of a base that is the tracked SILValue
// and a projection path to the represented field.
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_MEM_LOCATION_H
#define SWIFT_MEM_LOCATION_H

#include "swift/SILAnalysis/AliasAnalysis.h"
#include "swift/SIL/Projection.h"
#include "swift/SILPasses/Utils/Local.h"
#include "swift/SILAnalysis/ValueTracking.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Debug.h"

namespace swift {

//===----------------------------------------------------------------------===//
//                              Memory Location
//===----------------------------------------------------------------------===//
/// Forward declaration.
class MemLocation;

/// Type declarations.
using MemLocationSet = llvm::DenseSet<MemLocation>;
using MemLocationList = llvm::SmallVector<MemLocation, 8>;

class MemLocation {
public:
  enum KeyKind : uint8_t { EmptyKey = 0, TombstoneKey, NormalKey };

private:
  /// The base of the object.
  SILValue Base;
  /// Empty key, tombstone key or normal key.
  KeyKind Kind;
  /// The path to reach the accessed field of the object.
  Optional<ProjectionPath> Path;

public:
  /// Constructors.
  MemLocation() : Base(), Kind(NormalKey) {}
  MemLocation(SILValue B) : Base(B), Kind(NormalKey) { initialize(B); }
  MemLocation(SILValue B, ProjectionPath &P, KeyKind Kind = NormalKey)
      : Base(B), Kind(Kind), Path(std::move(P)) {}

  /// Copy constructor.
  MemLocation(const MemLocation &RHS) {
    Base = RHS.Base;
    Path.reset();
    Kind = RHS.Kind;
    if (!RHS.Path.hasValue())
      return;
    ProjectionPath X;
    X.append(RHS.Path.getValue());
    Path = std::move(X);
  }

  MemLocation &operator=(const MemLocation &RHS) {
    Base = RHS.Base;
    Path.reset();
    Kind = RHS.Kind;
    if (!RHS.Path.hasValue())
      return *this;
    ProjectionPath X;
    X.append(RHS.Path.getValue());
    Path = std::move(X);
    return *this;
  }

  /// Getters and setters for MemLocation.
  KeyKind getKind() const { return Kind; }
  void setKind(KeyKind K) { Kind = K; }
  SILValue getBase() const { return Base; }
  Optional<ProjectionPath> &getPath() { return Path; }

  /// Returns the hashcode for the location.
  llvm::hash_code getHashCode() const {
    llvm::hash_code HC = llvm::hash_combine(Base.getDef(),
                                            Base.getResultNumber(),
                                            Base.getType());
    if (!Path.hasValue())
      return HC;
    HC = llvm::hash_combine(HC, hash_value(Path.getValue()));
    return HC;
  }

  /// Returns the type of the object the MemLocation represents.
  SILType getType() const {
    // Base might be a address type, e.g. from alloc_stack of struct,
    // enum or tuples.
    if (Path.getValue().empty())
      return Base.getType().getObjectType();
    return Path.getValue().front().getType().getObjectType();
  }

  /// Returns whether the memory location has been initialized properly.
  bool isValid() const {
    return Base && Path.hasValue();
  }

  void subtractPaths(Optional<ProjectionPath> &P) {
    if (!P.hasValue())
      return;
    ProjectionPath::subtractPaths(Path.getValue(), P.getValue());
  }

  /// Return false if one projection path is a prefix of another. false
  /// otherwise.
  bool hasNonEmptySymmetricPathDifference(const MemLocation &RHS) const {
    const ProjectionPath &P = RHS.Path.getValue();
    return Path.getValue().hasNonEmptySymmetricDifference(P);
  }

  /// Return true if the 2 locations have identical projection paths.
  /// If both locations have empty paths, they are treated as having
  /// identical projection path.
  bool hasIdenticalProjectionPath(const MemLocation &RHS) const;

  /// Comparisons.
  bool operator!=(const MemLocation &RHS) const { return !(*this == RHS); }
  bool operator==(const MemLocation &RHS) const {
    // If type is not the same, then locations different.
    if (Kind != RHS.Kind)
      return false;
    // If Base is different, then locations different.
    if (Base != RHS.Base)
      return false;

    // If the projection paths are different, then locations are different.
    if (!hasIdenticalProjectionPath(RHS))
      return false;

    // These locations represent the same memory location.
    return true;
  }

  /// Trace the given SILValue till the base of the accessed object. Also
  /// construct the projection path to the field accessed.
  void initialize(SILValue val);

  /// Reset the memory location, i.e. clear base and path. 
  void reset() {
    Base = SILValue();
    Path.reset();
    Kind = NormalKey;
  }

  /// Get the first level locations based on this location's first level
  /// projection.
  void getFirstLevelMemLocations(MemLocationList &Locs, SILModule *Mod);

  /// Check whether the 2 MemLocations may alias each other or not.
  bool isMayAliasMemLocation(const MemLocation &RHS, AliasAnalysis *AA);

  /// Check whether the 2 MemLocations must alias each other or not.
  bool isMustAliasMemLocation(const MemLocation &RHS, AliasAnalysis *AA);

  /// Print MemLocation.
  void print() const;

  ///============================/// 
  ///       static functions.    ///
  ///============================/// 

  /// Given Base and 2 ProjectionPaths, create a MemLocation out of them.
  static MemLocation createMemLocation(SILValue Base, ProjectionPath &P1,
                                       ProjectionPath &P2);

  /// Expand this location to all individual fields it contains.
  ///
  /// In SIL, we can have a store to an aggregate and loads from its individual
  /// fields. Therefore, we expand all the operations on aggregates onto
  /// individual fields and process them separately.
  static void expand(MemLocation &Base, SILModule *Mod, MemLocationList &Locs);

  /// Given a set of locations derived from the same base, try to merge/reduce
  /// them into smallest number of MemLocations possible.
  static void reduce(MemLocation &Base, SILModule *Mod, MemLocationSet &Locs);

  /// Enumerate the given Mem MemLocation.
  static void enumerateMemLocation(SILModule *M, SILValue Mem,
                                   std::vector<MemLocation> &MemLocationVault,
                                   llvm::DenseMap<MemLocation, unsigned> &LocToBit);

  /// Enumerate all the locations in the function.
  static void enumerateMemLocations(SILFunction &F,
                                    std::vector<MemLocation> &MemLocationVault,
                                    llvm::DenseMap<MemLocation, unsigned> &LocToBit);
};

static inline llvm::hash_code hash_value(const MemLocation &L) {
  return llvm::hash_combine(L.getBase().getDef(), L.getBase().getResultNumber(),
                            L.getBase().getType());
}

inline llvm::raw_ostream &operator<<(llvm::raw_ostream &OS, MemLocation V) {
  V.getBase().print(OS);
  OS << V.getPath().getValue();
  return OS;
}

} // end swift namespace


/// MemLocation is used in DenseMap, define functions required by DenseMap.
namespace llvm {

using swift::MemLocation;

template <> struct DenseMapInfo<MemLocation> {
  static inline MemLocation getEmptyKey() {
    MemLocation L;
    L.setKind(MemLocation::EmptyKey);
    return L;
  }
  static inline MemLocation getTombstoneKey() {
    MemLocation L;
    L.setKind(MemLocation::TombstoneKey);
    return L;
  }
  static unsigned getHashValue(const MemLocation &Loc) {
    return hash_value(Loc);
  }
  static bool isEqual(const MemLocation &LHS, const MemLocation &RHS) {
    if (LHS.getKind() == MemLocation::EmptyKey &&
        RHS.getKind() == MemLocation::EmptyKey)
      return true;
    if (LHS.getKind() == MemLocation::TombstoneKey &&
        RHS.getKind() == MemLocation::TombstoneKey)
      return true;
    return LHS == RHS;
  }
};

} // namespace llvm

#endif  // SWIFT_MEM_LOCATION_H
