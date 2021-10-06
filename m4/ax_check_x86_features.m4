AC_DEFUN_ONCE([_AX_GCC_X86_CPU_INIT],
 [AC_LANG_PUSH([C])
  AC_CACHE_CHECK([for gcc __builtin_cpu_init function],
    [ax_cv_gcc_check_x86_cpu_init],
    [AC_RUN_IFELSE(
      [AC_LANG_PROGRAM([#include <stdlib.h>],
        [__builtin_cpu_init ();])
      ],
      [ax_cv_gcc_check_x86_cpu_init=yes],
      [ax_cv_gcc_check_x86_cpu_init=no])])
  AS_IF([test "X$ax_cv_gcc_check_x86_cpu_init" = "Xno"],
    [AC_MSG_ERROR([Need GCC to support X86 CPU features tests])])
])

AC_DEFUN([AX_GCC_X86_CPU_SUPPORTS],
  [AC_REQUIRE([AC_PROG_CC])
   AC_REQUIRE([_AX_GCC_X86_CPU_INIT])
   AC_LANG_PUSH([C])
   AS_VAR_PUSHDEF([gcc_x86_feature], [AS_TR_SH([ax_cv_gcc_x86_cpu_supports_$1])])
   AC_CACHE_CHECK([for x86 $1 instruction support], 
     [gcc_x86_feature],
     [AC_RUN_IFELSE(
       [AC_LANG_PROGRAM( [#include <stdlib.h> ], 
       [ __builtin_cpu_init ();
         if (__builtin_cpu_supports("$1"))
           return 0;
         return 1;
        ])],
        [gcc_x86_feature=yes],
        [gcc_x86_feature=no]
     )]
   )
   AC_LANG_POP([C])
   AS_VAR_IF([gcc_x86_feature],[yes],
         [AC_DEFINE(
           AS_TR_CPP([HAVE_$1_INSTRUCTIONS]),
           [1],
           [Define if $1 instructions are supported])
          $2],
          [$3]
         )
   AS_VAR_POPDEF([gcc_x86_feature])
])

AC_DEFUN([AX_CHECK_X86_FEATURES],
 [m4_foreach_w(
   [ax_x86_feature],
   [mmx popcnt sse sse2 sse3 sse4.1 sse4.2 sse4a avx avx2 avx512f fma fma4 bmi bmi2],
   [AX_GCC_X86_CPU_SUPPORTS(ax_x86_feature,
     [X86_FEATURE_CFLAGS="$X86_FEATURE_CFLAGS -m[]ax_x86_feature"],
     [])
  ])
  m4_ifval([$1],[$1],
    [CFLAGS="$CFLAGS $X86_FEATURE_CFLAGS"
     CXXFLAGS="$CXXFLAGS $X86_FEATURE_CFLAGS"])
  $2
])

