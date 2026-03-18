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
    for (auto &&Pair : NamedMod) {
      (const_cast<Instance::ModuleInstance *>(Pair.second))->unlinkStore(this);
    }
    NamedMod.clear();
  }

  /// Register named module into this store.
  Expect<void> registerModule(const Instance::ModuleInstance *ModInst) {
    std::unique_lock Lock(Mutex);
    auto Iter = NamedMod.find(ModInst->getModuleName());
    if (likely(Iter != NamedMod.cend())) {
      return Unexpect(ErrCode::Value::ModuleNameConflict);
    }
    NamedMod.emplace(ModInst->getModuleName(), ModInst);
    // Link the module instance to this store manager.
    (const_cast<Instance::ModuleInstance *>(ModInst))
        ->linkStore(this, [](StoreManager *Store,
                             const Instance::ModuleInstance *Inst) {
          // The unlink callback.
          std::unique_lock CallbackLock(Store->Mutex);
          (Store->NamedMod).erase(std::string(Inst->getModuleName()));
        });
    return {};
  }

  /// Unregister a named module from this store.
  Expect<void> unregisterModule(std::string_view Name) {
    std::unique_lock Lock(Mutex);
    auto Iter = NamedMod.find(Name);
    if (Iter == NamedMod.cend()) {
      return Unexpect(ErrCode::Value::UnknownImport);
    }
    (const_cast<Instance::ModuleInstance *>(Iter->second))->unlinkStore(this);
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
  void registerDependency(
      const Instance::ModuleInstance *Consumer,
      const std::set<const Instance::ModuleInstance *> &Providers) {
    if (!Consumer)
      return;

    std::unique_lock Lock(Mutex);

    auto &ConsumerNode = DependencyTree[Consumer];
    ConsumerNode.IsRegistered = true;

    for (const auto *Provider : Providers) {
      if (!Provider)
        continue;
      ConsumerNode.OutDegree.push_back(Provider);
      DependencyTree[Provider].InDegree++;
    }
  }

  /// \brief Unregisters a module instance from the dependency system.
  /// \param Inst The pointer to the module instance to be unregistered.
  void unregisterDependency(const Instance::ModuleInstance *Inst) {
    std::unique_lock Lock(Mutex);
    auto It = DependencyTree.find(Inst);
    if (It == DependencyTree.end()) {
      return;
    }
    It->second.IsRegistered = false;
    tryCleanupInternal(Inst);
  }

  /// \brief Internal gatekeeper to check if a module can be safely deleted.
  /// \param Inst The module instance to check for cleanup conditions.
  void tryCleanupInternal(const Instance::ModuleInstance *Inst) {
    auto It = DependencyTree.find(Inst);
    if (It == DependencyTree.end()) {
      return;
    }

    if (It->second.InDegree == 0 && !It->second.IsRegistered) {
      unlinkInternal(Inst);
    }
  }

  /// \brief Internal recursive function to unlink dependencies and delete
  /// module.
  /// \param Inst The pointer to the module instance to be processed.
  void unlinkInternal(const Instance::ModuleInstance *Inst) {
    auto It = DependencyTree.find(Inst);
    if (It == DependencyTree.end()) {
      return;
    }

    std::vector<const Instance::ModuleInstance *> Providers =
        std::move(It->second.OutDegree);

    DependencyTree.erase(It);
    delete Inst;

    for (auto *Provider : Providers) {
      auto ProvIt = DependencyTree.find(Provider);
      if (ProvIt != DependencyTree.end()) {
        // Decrement provider's reference count.
        if (ProvIt->second.InDegree > 0) {
          ProvIt->second.InDegree--;
        }

        tryCleanupInternal(Provider);
      }
    }
  }
};

} // namespace Runtime
} // namespace WasmEdge
