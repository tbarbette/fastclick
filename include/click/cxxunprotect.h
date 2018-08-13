#ifdef __cplusplus
# undef new
# undef this
# undef delete
# undef class
# undef virtual
# undef typename
# undef protected
# undef public
# undef namespace
# undef false
# undef true
#endif

#ifdef CLICK_CXX_PROTECTED
# undef asmlinkage
# define asmlinkage extern "C"
#endif

#undef CLICK_CXX_PROTECTED
