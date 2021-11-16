#ifndef CLICK_LLVMUTILS_HH
#define CLICK_LLVMUTILS_HH
#include <string>
#include <vector>
#include <memory>
#include <click/config.h>

namespace llvm {
    class Module;
    class StructType;
    class Type;
};

std::string demangle(const char *name);
template <typename T> size_t RemoveDuplicatesKeepOrder(std::vector<T> &vec);
template <typename T> void OrderBasedOnTheRepetition(std::vector<T> &vec);
template <typename T>
std::pair<bool, int> findInVector(const std::vector<T> &vecOfElements,
                                  const T &element);

uint64_t fixClassReferencesStructAllAnno(llvm::Module &M, std::string cn,
                                         std::vector<uint64_t> &idx_vec,
                                         uint64_t cr_idx, std::string dcn_1,
                                         std::string dcn_2);

size_t findAccessedIndicesStructAllAnno(llvm::Module &M, std::string cn,
                                        std::string sn,
                                        std::vector<uint64_t> &idx_vec,
                                        uint64_t &cr_idx);

size_t findAccessedIndicesClassPacket(llvm::Module &M, std::string cn,
                                      std::vector<uint64_t> &idx_vec);

llvm::StructType *modifyClassStructure(llvm::Module &M, std::string cn,
                                       std::vector<uint64_t> &idx_vec,
                                       std::vector<llvm::Type *> &new_el);

uint64_t fixClassReferencesClassPacket(llvm::Module &M, std::string cn,
                                       std::vector<uint64_t> &idx_vec,
                                       std::string dcn_1, std::string dcn_2);

bool reorderClassPacket(llvm::Module &M);
bool reorderStructAllAnno(llvm::Module &M);

bool handPick(std::unique_ptr<llvm::Module> &M);
bool poolInline(std::unique_ptr<llvm::Module> &M);
bool stripModuleFlags(std::unique_ptr<llvm::Module> &M);
bool optimizeIR(std::string input);
#endif
