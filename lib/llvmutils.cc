/*
 * llvmutils.{cc,hh} -- LLVM-related functions
 *
 * Copyright (c) 2021, Alireza Farshin, KTH Royal Institute of Technology - All
 * Rights Reserved
 *
 */

#include <click/llvmutils.hh>
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include <algorithm>
#include <cxxabi.h>
#include <errno.h>
#include <fstream>
#include <iostream>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/SourceMgr.h>
#include <unordered_map>


/****************************************************************************
 *
 * Utility Functions
 *
 ***************************************************************************/

/* Demangle a function name */
std::string demangle(const char *name) {
  int status = -1;

  std::unique_ptr<char, void (*)(void *)> res{
      abi::__cxa_demangle(name, NULL, NULL, &status), std::free};
  return (status == 0) ? res.get() : std::string(name);
}

/* The following functions are inspired by online resources */

/* Remove the duplicates from a vector while keeping the order */
template <typename T> size_t RemoveDuplicatesKeepOrder(std::vector<T> &vec) {
  std::set<T> seen;

  auto newEnd = std::remove_if(vec.begin(), vec.end(), [&seen](const T &value) {
    if (seen.find(value) != std::end(seen))
      return true;

    seen.insert(value);
    return false;
  });

  vec.erase(newEnd, vec.end());

  return vec.size();
}

/* Order the vector elements based on the number of repetitions */
template <typename T> void OrderBasedOnTheRepetition(std::vector<T> &vec) {
  std::unordered_map<T, int> count;

  for (auto v : vec)
    count[v]++;
  std::sort(vec.begin(), vec.end(), [&](int a, int b) {
    return std::tie(count[a], a) > std::tie(count[b], b);
  });
  RemoveDuplicatesKeepOrder(vec);
}

template <typename T>
std::pair<bool, int> findInVector(const std::vector<T> &vecOfElements,
                                  const T &element) {
  std::pair<bool, int> result;

  auto it = std::find(vecOfElements.begin(), vecOfElements.end(), element);

  if (it != vecOfElements.end()) {
    result.second = distance(vecOfElements.begin(), it);
    result.first = true;
  } else {
    result.first = false;
    result.second = -1;
  }
  return result;
}

/****************************************************************************
 *
 * LLVM-related Function
 *
 ***************************************************************************/

/*
 * Fixing all references to the AllAnno struct in the Class Packet and its
 * derived classes WritablePacket and PacketBatch classes are derived from the
 * Packet class
 */
uint64_t
fixClassReferencesStructAllAnno(llvm::Module &M, std::string cn,
                                std::vector<uint64_t> &idx_vec, uint64_t cr_idx,
                                std::string dcn_1 = "class.WritablePacket",
                                std::string dcn_2 = "class.PacketBatch") {

  uint64_t fixed_accesses = 0;
  /* Similar to the findAccessedIndicesStructAllAnno function */
  for (auto &F : M) {
    for (auto &BB : F) {
      for (auto &I : BB) {
        if (llvm::GetElementPtrInst *GEPI =
                dyn_cast<llvm::GetElementPtrInst>(&I)) {
          if (GEPI->getSourceElementType()->isStructTy()) {
            if ((GEPI->getSourceElementType()->getStructName() == cn) &&
                (GEPI->getNumIndices() >= 2)) {
              /* We know that accesses to class.Packet are happening in the
               * 2nd index */
              auto idx = GEPI->idx_begin() + 1;

              llvm::Value *val = idx->get();
              if (llvm::ConstantInt *CI = dyn_cast<llvm::ConstantInt>(val)) {
                if (CI->getZExtValue() == cr_idx) {
                  idx++;
                  llvm::Value *val = idx->get();
                  if (llvm::ConstantInt *CI_2 =
                          dyn_cast<llvm::ConstantInt>(val)) {
                    auto NI = GEPI->getNextNonDebugInstruction();
                    /*Change the index*/
                    std::pair<bool, int> result =
                        findInVector<uint64_t>(idx_vec, CI_2->getZExtValue());
                    /* We know that accesses to struct.Packet::AllAnno are
                     * happening in the 3rd index */
                    GEPI->setOperand(3,
                                     llvm::ConstantInt::get(
                                         llvm::Type::getInt32Ty(I.getContext()),
                                         result.second));
                    fixed_accesses++;
                  }
                }
              } else {
                llvm::errs() << "Could not cast as a constant integer!"
                             << "\n";
              }
            } /* dcn_1 should be the class.WritablePacket */
            else if ((GEPI->getSourceElementType()->getStructName() == dcn_1) &&
                     (GEPI->getNumIndices() >= 3)) {
              /* We know that accesses to class.WritablePacket are happening
               * in the 3rd index */
              auto idx = GEPI->idx_begin() + 2;

              llvm::Value *val = idx->get();
              if (llvm::ConstantInt *CI = dyn_cast<llvm::ConstantInt>(val)) {
                if (CI->getZExtValue() == cr_idx) {
                  idx++;
                  llvm::Value *val = idx->get();
                  if (llvm::ConstantInt *CI_2 =
                          dyn_cast<llvm::ConstantInt>(val)) {
                    auto NI = GEPI->getNextNonDebugInstruction();
                    /*Change the index*/
                    std::pair<bool, int> result =
                        findInVector<uint64_t>(idx_vec, CI_2->getZExtValue());
                    /* We know that accesses to class.WritablePacket are
                     * happening in the 4th index */
                    GEPI->setOperand(4,
                                     llvm::ConstantInt::get(
                                         llvm::Type::getInt32Ty(I.getContext()),
                                         result.second));
                    fixed_accesses++;
                  }
                }
              } else {
                llvm::errs() << "Could not cast as a constant integer!"
                             << "\n";
              }
            } /* dcn_2 should be the class.PacketBatch */
            else if ((GEPI->getSourceElementType()->getStructName() == dcn_2) &&
                     (GEPI->getNumIndices() >= 4)) {
              /* We know that accesses to class.PacketBatch are happening in
               * the 4th index */
              auto idx = GEPI->idx_begin() + 3;

              llvm::Value *val = idx->get();
              if (llvm::ConstantInt *CI = dyn_cast<llvm::ConstantInt>(val)) {
                if (CI->getZExtValue() == cr_idx) {
                  idx++;
                  llvm::Value *val = idx->get();
                  if (llvm::ConstantInt *CI_2 =
                          dyn_cast<llvm::ConstantInt>(val)) {
                    auto NI = GEPI->getNextNonDebugInstruction();
                    /*Change the index*/
                    std::pair<bool, int> result =
                        findInVector<uint64_t>(idx_vec, CI_2->getZExtValue());
                    /* We know that accesses to class.PacketBatch are
                     * happening in the 5th index */
                    GEPI->setOperand(5,
                                     llvm::ConstantInt::get(
                                         llvm::Type::getInt32Ty(I.getContext()),
                                         result.second));
                    fixed_accesses++;
                  }
                }
              } else {
                llvm::errs() << "Could not cast as a constant integer!"
                             << "\n";
              }
            }
          }
        }
      }
    }
  }
  return fixed_accesses;
}

/*
 * Find the accesses to different variables of class/struct
 * in all of the functions.
 */
size_t findAccessedIndicesStructAllAnno(llvm::Module &M, std::string cn,
                                        std::string sn,
                                        std::vector<uint64_t> &idx_vec,
                                        uint64_t &cr_idx) {

  /* Find the current index of the AllAnno struct in Packet class */
  for (auto ST : M.getIdentifiedStructTypes()) {
    if (ST->getName() == cn) {
      llvm::errs() << ST->getName() << "\n";
      llvm::ArrayRef<llvm::Type *> elements = ST->elements();
      auto numElements = ST->getNumElements();
      auto name = ST->getName().str();
      auto elements_vec = elements.vec();

      for (auto i = 0; i < elements_vec.size(); i++) {
        if (elements_vec[i]->isStructTy()) {
          if (elements_vec[i]->getStructName() == sn) {
            cr_idx = i;
            llvm::errs() << "Found AllAnno, located at index " << cr_idx
                         << "\n";
          }
        }
      }
    }
  }
  for (auto &F : M) {
    for (auto &BB : F) {
      for (auto &I : BB) {
        /* Find the GetElementPtr Instructions */
        if (llvm::GetElementPtrInst *GEPI =
                dyn_cast<llvm::GetElementPtrInst>(&I)) {
          /* Check whether it is accesing a struct/class called cn (e.g.,
           * class.Packet */
          if (GEPI->getSourceElementType()->isStructTy()) {
            if (GEPI->getSourceElementType()->getStructName() == cn) {
              /*
               * Find the accessed index of AllAnno
               */
              auto idx = GEPI->idx_begin() + 1;
              llvm::Value *val = idx->get();
              /* Check whether the index is a constant integer */
              if (llvm::ConstantInt *CI = dyn_cast<llvm::ConstantInt>(val)) {
                /*
                 * TODO: Check the type of the next instruction
                 * load or store
                 * we can remove the accesses which are not load
                 */
                if (CI->getZExtValue() == cr_idx) {
                  idx++;
                  llvm::Value *val = idx->get();
                  if (llvm::ConstantInt *CI_2 =
                          dyn_cast<llvm::ConstantInt>(val)) {
                    idx_vec.push_back(CI_2->getZExtValue());
                  }
                }
              }
            }
          }
        }
      }
    }
  }

  return idx_vec.size();
}

/*
 * Find the accesses to different variables of class/struct
 * in all of the functions.
 */
size_t findAccessedIndicesClassPacket(llvm::Module &M, std::string cn,
                                      std::vector<uint64_t> &idx_vec) {

  for (auto &F : M) {
    for (auto &BB : F) {
      for (auto &I : BB) {
        /* Find the GetElementPtr Instructions */
        if (llvm::GetElementPtrInst *GEPI =
                dyn_cast<llvm::GetElementPtrInst>(&I)) {
          /* Check whether it is accesing a struct/class called cn (e.g.,
           * class.Packet */
          if (GEPI->getSourceElementType()->isStructTy()) {
            if (GEPI->getSourceElementType()->getStructName() == cn) {
              /*
               * Find the accessed index
               * It is usually the second one
               * e.g., getelementptr inbounds %class.Packet,
               * %class.Packet* %0, i64 0, i32 2 which is accesing the 3rd
               * (starting from 0 -> i32 2) variable
               */
              auto idx = GEPI->idx_begin() + 1;
              llvm::Value *val = idx->get();
              /* Check whether the index is a constant integer */
              if (llvm::ConstantInt *CI = dyn_cast<llvm::ConstantInt>(val)) {
                /*
                 * TODO: Check the type of the next instruction
                 * load or store
                 * we can remove the accesses which are not load
                 */

                idx_vec.push_back(CI->getZExtValue());
              }
            }
          }
        }
      }
    }
  }

  return idx_vec.size();
}

/*
 * Modify the Packet Class based on the input indices.
 */
llvm::StructType *modifyClassStructure(llvm::Module &M, std::string cn,
                                       std::vector<uint64_t> &idx_vec,
                                       std::vector<llvm::Type *> &new_el) {

  llvm::StructType *markedST;
  for (auto ST : M.getIdentifiedStructTypes()) {
    if (ST->getName() == cn) {

      llvm::ArrayRef<llvm::Type *> elements = ST->elements();
      auto numElements = ST->getNumElements();
      auto name = ST->getName().str();

      auto elements_vec = elements.vec();

      auto j = 0;
      for (auto j = 0; j < idx_vec.size(); j++) {
        auto idx = idx_vec[j];
        auto itr = elements.begin();
        while (idx > 0) {
          itr++;
          idx--;
        }
        new_el.push_back(*itr);
      }

      /* Keep the reference to the StructType for later */
      markedST = ST;
      break;
    }
  }
  return markedST;
}

/*
 * Fixing all references to the target class and its derived classes
 * WritablePacket and PacketBatch classes are derived from the target class,
 * i.e., Packet class.
 */
uint64_t
fixClassReferencesClassPacket(llvm::Module &M, std::string cn,
                              std::vector<uint64_t> &idx_vec,
                              std::string dcn_1 = "class.WritablePacket",
                              std::string dcn_2 = "class.PacketBatch") {
  uint64_t fixed_accesses = 0;
  /* Similar to the findAccessedIndicesClassPacket function */
  for (auto &F : M) {
    for (auto &BB : F) {
      for (auto &I : BB) {
        if (llvm::GetElementPtrInst *GEPI =
                dyn_cast<llvm::GetElementPtrInst>(&I)) {
          if (GEPI->getSourceElementType()->isStructTy()) {
            if ((GEPI->getSourceElementType()->getStructName() == cn) &&
                (GEPI->getNumIndices() >= 2)) {
              /* We know that accesses to class.Packet are happening in the
               * 2nd index */
              auto idx = GEPI->idx_begin() + 1;

              llvm::Value *val = idx->get();
              if (llvm::ConstantInt *CI = dyn_cast<llvm::ConstantInt>(val)) {
                auto NI = GEPI->getNextNonDebugInstruction();
                /*Change the index*/
                std::pair<bool, int> result =
                    findInVector<uint64_t>(idx_vec, CI->getZExtValue());
                /* We know that accesses to class.Packet are happening in
                 * the 2nd index */
                GEPI->setOperand(2, llvm::ConstantInt::get(
                                        llvm::Type::getInt32Ty(I.getContext()),
                                        result.second));
                fixed_accesses++;
              } else {
                llvm::errs() << "Could not cast as a constant integer!"
                             << "\n";
              }
            } /* dcn_1 should be the class.WritablePacket */
            else if ((GEPI->getSourceElementType()->getStructName() == dcn_1) &&
                     (GEPI->getNumIndices() >= 3)) {
              /* We know that accesses to class.WritablePacket are happening
               * in the 3rd index */
              auto idx = GEPI->idx_begin() + 2;

              llvm::Value *val = idx->get();
              if (llvm::ConstantInt *CI = dyn_cast<llvm::ConstantInt>(val)) {

                auto NI = GEPI->getNextNonDebugInstruction();
                /*Change the index*/
                std::pair<bool, int> result =
                    findInVector<uint64_t>(idx_vec, CI->getZExtValue());
                /* We know that accesses to class.WritablePacket are
                 * happening in the 3rd index */
                GEPI->setOperand(3, llvm::ConstantInt::get(
                                        llvm::Type::getInt32Ty(I.getContext()),
                                        result.second));
                fixed_accesses++;
              } else {
                llvm::errs() << "Could not cast as a constant integer!"
                             << "\n";
              }
            } /* dcn_2 should be the class.PacketBatch */
            else if ((GEPI->getSourceElementType()->getStructName() == dcn_2) &&
                     (GEPI->getNumIndices() >= 4)) {
              /* We know that accesses to class.PacketBatch are happening in
               * the 4th index */
              auto idx = GEPI->idx_begin() + 3;

              llvm::Value *val = idx->get();
              if (llvm::ConstantInt *CI = dyn_cast<llvm::ConstantInt>(val)) {

                auto NI = GEPI->getNextNonDebugInstruction();
                /*Change the index*/
                std::pair<bool, int> result =
                    findInVector<uint64_t>(idx_vec, CI->getZExtValue());
                /* We know that accesses to class.PacketBatch are happening
                 * in the 4th index */
                GEPI->setOperand(4, llvm::ConstantInt::get(
                                        llvm::Type::getInt32Ty(I.getContext()),
                                        result.second));
                fixed_accesses++;
              } else {
                llvm::errs() << "Could not cast as a constant integer!"
                             << "\n";
              }
            }
          }
        }
      }
    }
  }
  return fixed_accesses;
}

/*
 * Reorder the elements of Class Packet based on
 * the number of accesses
 */
bool reorderClassPacket(llvm::Module &M) {
  /* The target class/struct */
  std::string className = "class.Packet";

  std::vector<uint64_t> accessed_indices;

  /* Find the accesses made to the class/struct */
  size_t res = findAccessedIndicesClassPacket(M, className, accessed_indices);

  /* Check whether there is any access */
  if (res == 0) {
    llvm::errs() << "No access to the struct/class!\n";
    /* The module is not changed! */
    return false;
  }

  llvm::errs() << "#References: " << res << "\n";

  /* Order accesses based on the repetition */
  OrderBasedOnTheRepetition(accessed_indices);

  /*
   * Find the maximum number of variables in the class/struct
   */
  uint64_t max_indices;
  for (auto ST : M.getIdentifiedStructTypes()) {
    if (ST->getName() == className) {
      max_indices = ST->getNumElements();
      llvm::errs() << "Before Pass:\n" << *ST << "\n";
      break;
    }
  }

  /*
   * Add the unused indices at the end
   * We do this to avoid removing the unused variables.
   * TODO: Ignore the unused variables.
   */

  for (auto i = 0; i < max_indices; i++) {
    if (!(std::find(accessed_indices.begin(), accessed_indices.end(), i) !=
          accessed_indices.end())) {
      accessed_indices.push_back(i);
    }
  }

  std::pair<bool, int> result = findInVector<uint64_t>(accessed_indices, 0);
  llvm::errs() << "New index of 0 is: " << result.second << "\n";

  /* Print the ordered indices */
  llvm::errs() << "Accesses: ";
  for (auto idx : accessed_indices)
    llvm::errs() << idx << " ";
  llvm::errs() << "\n";

  /* Find the class/struct in the module and handpick the variables */

  std::vector<llvm::Type *> new_elements;
  llvm::StructType *markedST =
      modifyClassStructure(M, className, accessed_indices, new_elements);

  if (markedST == NULL) {
    llvm::errs() << "Could not find the class!!\n";
    /* The module is not changed! */
    return false;
  }

  /* Fixing the references to the class */

  uint64_t fixed =
      fixClassReferencesClassPacket(M, className, accessed_indices);
  llvm::errs() << "Fixed " << fixed << " references!"
               << "\n";

  /* Change the Struct Type */

  markedST->setBody(new_elements);

  llvm::errs() << "After Pass:\n" << *markedST << "\n";

  /*made change*/
  return true;
}

/*
 * Reorder the elements of Struct AllAnno based on
 * the number of accesses
 */
bool reorderStructAllAnno(llvm::Module &M) {
  /* The target class/struct */
  std::string className = "class.Packet";
  std::string structName = "struct.Packet::AllAnno";
  uint64_t cur_index;

  std::vector<uint64_t> accessed_indices;
  bool found_AllAnno = false;

  /* Check whether AllAnno exsits or not */
  for (auto ST : M.getIdentifiedStructTypes()) {
    if (ST->getName() == structName) {
      found_AllAnno = true;
    }
  }

  if (!found_AllAnno) {
    llvm::errs() << "IR does not have AllAnno struct!\n"
                 << "Skipping AllAnno reordering!\n";
    return false;
  }


  /* Find the accesses made to the class/struct */
  size_t res = findAccessedIndicesStructAllAnno(M, className, structName,
                                                accessed_indices, cur_index);

  /* Check whether there is any access */
  if (res == 0) {
    llvm::errs() << "No access to the struct/class!\n";
    /* The module is not changed! */
    return false;
  }

  llvm::errs() << "#References: " << res << "\n";

  /* Order accesses based on the repetition */
  OrderBasedOnTheRepetition(accessed_indices);

  /*
   * Find the maximum number of variables in the class/struct
   */
  uint64_t max_indices;
  for (auto ST : M.getIdentifiedStructTypes()) {
    if (ST->getName() == structName) {
      max_indices = ST->getNumElements();
      llvm::errs() << "Before Pass:\n" << *ST << "\n";
      break;
    }
  }

  /*
   * Add the unused indices at the end
   * We do this to avoid removing the unused variables.
   * TODO: Ignore the unused variables.
   */

  for (auto i = 0; i < max_indices; i++) {
    if (!(std::find(accessed_indices.begin(), accessed_indices.end(), i) !=
          accessed_indices.end())) {
      accessed_indices.push_back(i);
    }
  }

  /* Print the ordered indices */
  llvm::errs() << "Accesses: ";
  for (auto idx : accessed_indices)
    llvm::errs() << idx << " ";
  llvm::errs() << "\n";

  /* Find the class/struct in the module and handpick the variables */

  std::vector<llvm::Type *> new_elements;
  llvm::StructType *markedST =
      modifyClassStructure(M, structName, accessed_indices, new_elements);

  if (markedST == NULL) {
    llvm::errs() << "Could not find the class!!\n";
    /* The module is not changed! */
    return false;
  }

  /* Fixing the references to the class */

  uint64_t fixed = fixClassReferencesStructAllAnno(M, className,
                                                   accessed_indices, cur_index);
  llvm::errs() << "Fixed " << fixed << " references!"
               << "\n";

  /* Change the Struct Type */

  markedST->setBody(new_elements);

  llvm::errs() << "After Pass:\n" << *markedST << "\n";

  return true;
}

/****************************************************************************
 *
 * LLVM Passes
 *
 ***************************************************************************/

/*
 * This function reoders the variables in the Packet class of FastClick.
 */
bool handPick(std::unique_ptr<llvm::Module> &M) {

  /* Reoder AllAnno struct */
  llvm::errs() << "Reordering AllAnno Struct"
               << "\n";
  auto res_1 = reorderStructAllAnno(*M);

  /* Reorder class.Packet */
  llvm::errs() << "Reordering Class Packet"
               << "\n";
  auto res_2 = reorderClassPacket(*M);

  /* return true if changed */
  return (res_1 || res_2);
}

/*
 * This function replaces optnone with alwaysinline attribute for the input functions
 * E.g., WritablePacket::pool_prepare_data_burst and Anno *xanno()
 */
bool funcInline(std::unique_ptr<llvm::Module> &M, std::string inputFuncName) {

  bool change = false;

  llvm::errs() << "Searching for the " << inputFuncName << " function ...\n";
  for (auto &F : *M) {
    std::string funcName = demangle(F.getName().data());
    auto found_func = funcName.find(inputFuncName);
    if ((found_func != std::string::npos)) {
      if (F.hasFnAttribute(llvm::Attribute::NoInline)) {
        llvm::errs() << "Removing noinline attribute...\n";
        F.removeFnAttr(llvm::Attribute::NoInline);
        change = true;
      }
      if (F.hasFnAttribute(llvm::Attribute::OptimizeNone)) {
        llvm::errs() << "Removing optnone attribute...\n";
        F.removeFnAttr(llvm::Attribute::OptimizeNone);
        change = true;
      }
      if (!F.hasFnAttribute(llvm::Attribute::AlwaysInline)) {
        llvm::errs() << "Adding alwaysinline attribute...\n";
        F.addFnAttr(llvm::Attribute::AlwaysInline);
        change = true;
      }
    }
  }
  return change;
}

/*
 * This function removes llvm.module.flags,
 * which contains ThinLTO, EnableSplitLTOUnit, and LTOPostLink.
 * We do this to avoid linker error while building the binary.
 */
bool stripModuleFlags(std::unique_ptr<llvm::Module> &M) {
  auto flags = M->getModuleFlagsMetadata();
  if (flags) {
    llvm::errs() << "Removing " << flags->getName() << " ... "
                 << "\n";

    for (auto f = flags->op_begin(); f != flags->op_end(); f++) {
      std::string type_str;
      llvm::raw_string_ostream rso(type_str);
      (*f)->print(rso);
      if ((*f)->getNumOperands() == 3) {
        llvm::errs() << "RM " << *(*f)->getOperand(1) << "\n";
      } else {
        llvm::errs() << "RM " << rso.str() << "\n";
      }
    }
    /* Erasing */
    flags->eraseFromParent();
    /*made change*/
    return true;
  } else {
    llvm::errs() << "No flags found!"
                 << "\n";
    return false;
  }
}

bool optimizeIR(std::string input) {
  if (!std::fstream{input + ".0.5.precodegen.bc"}) {
    llvm::errs() << "Couldn't find " << input << ".0.5.precodegen.bc"
                 << "\n";
    llvm::errs() << "Skipping IR optimimzation"
                 << "\n";
    return false;
  }

#if HAVE_DPDK_XCHG
# if POOL_INLINING
  llvm::errs()
      << "Cannot run LLVM passes when pool_prepare_data_burst might get inlined"
      << "\n";
  llvm::errs() << "Compile FastClick with --disable-pool-inlining"
               << "\n";
  llvm::errs() << "Skipping IR optimimzation"
               << "\n";
  return false;
# endif
#endif

  llvm::LLVMContext context;
  llvm::SMDiagnostic error;
  std::unique_ptr<llvm::Module> M =
  llvm::parseIRFile(input + ".0.5.precodegen.bc", error, context);
  bool changed = false;
  if (M) {
    /* Removing module flags to avoid linking error */
    changed |= stripModuleFlags(M);
    /* Reorder Packet data structure */
    changed |= handPick(M);
    /* Inline pool preparation function after reordering */
    changed |= funcInline(M,"pool_prepare_data_burst");
#if INLINED_ALLANNO
    /* Inline xanno functions after reordering */
    changed |= funcInline(M,"xanno");
    /* Inline all_anno functions after reordering */
    changed |= funcInline(M,"all_anno");
#endif

    /* Write the output */
    if (changed) {
      std::error_code EC;
      llvm::raw_fd_ostream OS(input + ".ll", EC);
      OS << *M;
      return true;
    }
  } else {
    llvm::errs() << "No LLVM module found!\n";
    return false;
  }
}
