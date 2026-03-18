// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2019-2024 Second State INC

//===-- wasmedge/runtime/storemgr.h - Store Manager definition ------------===//
//
// Part of the WasmEdge Project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the definition of Store Manager.
///
//===----------------------------------------------------------------------===//
#pragma once

#include "runtime/instance/component/component.h"
#include "runtime/instance/module.h"

#include <map>
#include <mutex>
#include <shared_mutex>
#include <vector>

namespace WasmEdge {

namespace Executor {
class Executor;
}

namespace Runtime {

class StoreManager {
public:
  StoreManager() = default;
  ~StoreManager() {
    // When destroying this store manager, unlink all the registered module
    // instances.
    reset();
  }

  /// Get the length of the list of registered modules.
  uint32_t getModuleListSize() const noexcept {
    std::shared_lock Lock(Mutex);
    return static_cast<uint32_t>(NamedMod.size());
  }

  /// Get list of registered modules.
  template <typename CallbackT> auto getModuleList(CallbackT &&CallBack) const {
    std::shared_lock Lock(Mutex);
    return std::forward<CallbackT>(CallBack)(NamedMod);
  }

  /// Find module by name.
  const Instance::ModuleInstance *findModule(std::string_view Name) const {
    std::shared_lock Lock(Mutex);
    if (auto Iter = NamedMod.find(Name); likely(Iter != NamedMod.cend())) {
      return Iter->second;
    }
    return nullptr;
  }

  /// Find component by name.
  const Instance::ComponentInstance *
  findComponent(std::string_view Name) const {
    std::shared_lock Lock(Mutex);
    if (auto Iter = NamedComp.find(Name); likely(Iter != NamedComp.cend())) {
      return Iter->second;
    }
    return nullptr;
  }

  /// Reset this store manager and unlink all the registered module instances.
  void reset() noexcept {
    std::shared_lock Lock(Mutex);
    for (auto &&[Name, ModInst] : NamedMod) {
      (const_cast<Instance::ModuleInstance *>(ModInst))
          ->unlinkStore(this, Name);
    }
    for (auto &&[ModInst, Node] : DependencyTree) {
      (const_cast<Instance::ModuleInstance *>(ModInst))
          ->unlinkStore(this, ModInst->getModuleName());
    }
    NamedMod.clear();
    DependencyTree.clear();
  }

  /// Register named module into this store.
  Expect<void> registerModule(const Instance::ModuleInstance *ModInst) {
    return registerModule(ModInst, ModInst->getModuleName());
  }

  /// Register a module instance into this store under the given alias name.
  Expect<void> registerModule(const Instance::ModuleInstance *ModInst,
                              std::string_view Name) {
    std::unique_lock Lock(Mutex);
    auto Iter = NamedMod.find(Name);
    if (likely(Iter != NamedMod.cend())) {
      return Unexpect(ErrCode::Value::ModuleNameConflict);
    }
    NamedMod.emplace(std::string(Name), ModInst);
    DependencyTree[ModInst];
    // Link the module instance to this store manager.
    (const_cast<Instance::ModuleInstance *>(ModInst))
        ->linkStore(this, Name,
                    [](const Instance::ModuleInstance::LinkedStoreKey &Key,
                       const Instance::ModuleInstance *Inst) {
                      // The unlink callback: erase the alias name from the
                      // store.
                      std::unique_lock CallbackLock(Key.first->Mutex);
                      (Key.first->NamedMod).erase(Key.second);
                      auto It = (Key.first)->DependencyTree.find(Inst);
                      if (It != (Key.first)->DependencyTree.end()) {
                        (Key.first)->unsafeUnlinkDependency(It);
                      }
                    });
    return {};
  }

  void linkAnonymousModule(const Instance::ModuleInstance *ModInst) {
    std::unique_lock Lock(Mutex);
    DependencyTree[ModInst];

    (const_cast<Instance::ModuleInstance *>(ModInst))
        ->linkStore(this, ModInst->getModuleName(),
                    [](const Instance::ModuleInstance::LinkedStoreKey &Key,
                       const Instance::ModuleInstance *Inst) {
                      std::unique_lock CallbackLock(Key.first->Mutex);
                      auto It = (Key.first)->DependencyTree.find(Inst);
                      if (It != (Key.first)->DependencyTree.end()) {
                        (Key.first)->unsafeUnlinkDependency(It);
                      }
                    });
  }

  /// Unregister a named module from this store.
  Expect<void> unregisterModule(std::string_view Name) {
    std::unique_lock Lock(Mutex);
    auto Iter = NamedMod.find(Name);
    if (Iter == NamedMod.cend()) {
      return Unexpect(ErrCode::Value::UnknownImport);
    }
    (const_cast<Instance::ModuleInstance *>(Iter->second))
        ->unlinkStore(this, Name);
    auto TreeIter = DependencyTree.find(
        (const_cast<Instance::ModuleInstance *>(Iter->second)));
    if (TreeIter != DependencyTree.end()) {
      TreeIter->second.IsRegistered = false;
      if (TreeIter->second.InDegree == 0) {
        unlinkDependency(const_cast<Instance::ModuleInstance *>(Iter->second));
      }
    }

    NamedMod.erase(Iter);
    return {};
  }

  /// Register named component into this store.
  Expect<void> registerComponent(const Instance::ComponentInstance *CompInst) {
    std::unique_lock Lock(Mutex);
    auto Iter = NamedComp.find(CompInst->getComponentName());
    if (likely(Iter != NamedComp.cend())) {
      return Unexpect(ErrCode::Value::ModuleNameConflict);
    }
    NamedComp.emplace(CompInst->getComponentName(), CompInst);
    return {};
  }

  bool unlinkDependency(const Instance::ModuleInstance *Inst) {
    std::unique_lock Lock(Mutex);

    auto It = DependencyTree.find(Inst);
    if (It == DependencyTree.end()) {
      return true;
    }

    It->second.IsRegistered = false;
    if (It->second.InDegree == 0) {
      unsafeUnlinkDependency(It);
      return true;
    }

    return false;
  }

private:
  friend class Executor::Executor;

  /// \name Mutex for thread-safe.
  mutable std::shared_mutex Mutex;

  /// Collect the instantiation failed module.
  void recycleModule(std::unique_ptr<Instance::ModuleInstance> &&Mod) {
    FailedMod = std::move(Mod);
  }

  /// \name Module name mapping.
  std::map<std::string, const Instance::ModuleInstance *, std::less<>> NamedMod;
  /// \name Component name mapping.
  std::map<std::string, const Instance::ComponentInstance *, std::less<>>
      NamedComp;

  /// \name Last instantiation failed module.
  /// According to the current spec, the instances should be able to be
  /// referenced even if instantiation failed. Therefore store the failed module
  /// instance here to keep the instances.
  /// FIXME: Is this necessary to be a vector?
  std::unique_ptr<Instance::ModuleInstance> FailedMod;

  /// \brief Node structure representing a module's dependency state.
  struct ModuleDependency {
    uint32_t InDegree = 0;
    bool IsRegistered = true;
    std::vector<const Instance::ModuleInstance *> OutDegree;
  };

  /// \name Module dependency tree
  std::map<const Instance::ModuleInstance *, ModuleDependency> DependencyTree;

  /// \brief Registers a module instance and its dependency
  /// \param ModInst The module instance being registered.
  /// \param DetectedDeps The set of module instances that ModInst depends on.
  void
  addDependency(const Instance::ModuleInstance *Consumer,
                const std::set<const Instance::ModuleInstance *> &Providers) {
    if (!Consumer)
      return;

    std::unique_lock Lock(Mutex);

    auto &ConsumerNode = DependencyTree[Consumer];

    for (const auto *Provider : Providers) {
      if (!Provider)
        continue;
      ConsumerNode.OutDegree.push_back(Provider);
      DependencyTree[Provider].InDegree++;
    }
  }

  void unsafeUnlinkDependency(std::map<const Instance::ModuleInstance *,
                                       ModuleDependency>::iterator It) {
    std::vector<const Instance::ModuleInstance *> Providers =
        std::move(It->second.OutDegree);

    DependencyTree.erase(It);

    for (auto *Provider : Providers) {
      auto ProvIt = DependencyTree.find(Provider);
      if (ProvIt != DependencyTree.end()) {
        if (ProvIt->second.InDegree > 0) {
          ProvIt->second.InDegree--;
        }

        if (!ProvIt->second.IsRegistered && ProvIt->second.InDegree == 0) {
          const auto *OrphanInst = ProvIt->first;
          unsafeUnlinkDependency(ProvIt);
          delete OrphanInst;
        }
      }
    }
  }
};

} // namespace Runtime
} // namespace WasmEdge
