; ModuleID = 't1.c'
source_filename = "t1.c"
target datalayout = "E-m:e-p:32:32-Fi64-i64:64-i128:128-n32:64"
target triple = "powerpc64-unknown-xbox360"

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(none) uwtable
define dso_local signext i32 @add(i32 noundef signext %a, i32 noundef signext %b) local_unnamed_addr #0 {
entry:
  %add = add nsw i32 %b, %a
  ret i32 %add
}

attributes #0 = { mustprogress nofree norecurse nosync nounwind willreturn memory(none) uwtable "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="ppc64" "target-features"="+64bit-support,+altivec,+fpu,+fres,+frsqrte,+fsqrt,+hard-float,+mfocrf,+stfiwx" }

!llvm.module.flags = !{!0}
!llvm.ident = !{!1}
!llvm.errno.tbaa = !{!2}

!0 = !{i32 7, !"uwtable", i32 2}
!1 = !{!"clang version 24.0.0git (https://github.com/llvm/llvm-project.git dfd12ff17c42ee9421ca3fa620ef327f3dc70d98)"}
!2 = !{!3, !4, i64 0}
!3 = !{!"__libc_errno", !4, i64 0}
!4 = !{!"int", !5, i64 0}
!5 = !{!"omnipotent char", !6, i64 0}
!6 = !{!"Simple C/C++ TBAA"}
