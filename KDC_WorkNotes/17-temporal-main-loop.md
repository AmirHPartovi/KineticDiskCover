# مرحلهٔ ۱۷ — حلقهٔ اصلی temporal

## وضعیت پایه
- مسیر مخزن: `~/Desktop/Codes/KineticDiskCover`
- شاخه: `main`
- commit پایه: `9d0bb0a4 snapshot repair`
- پیش از کار: هیچ تغییر staged یا tracked diff وجود نداشت؛ تمام فایل‌های `weighted_*` و `temporal_*` مراحل قبل untracked بودند. این فایل‌ها حفظ شدند و stage/commit انجام نشد.
- آزمون اولیهٔ `Temporal thesis solver executes a small synthetic split` با SIGSEGV شکست می‌خورد.

## دامنه
فقط رفع crash و تکمیل اتصال main loop در solver temporal، همراه با آزمون‌های synthetic کوچک. بدون اجرای benchmark بزرگ و بدون عملیات remote/Git.

## فایل‌های خوانده‌شده
`src/temporal_thesis_solver.cpp`, `include/kdc/temporal_thesis_solver.hpp`, `tests/test_temporal_thesis_solver.cpp`, `src/temporal_split_policy.cpp`, `include/kdc/temporal_split_policy.hpp`, `src/temporal_partition.cpp`, `include/kdc/temporal_partition.hpp`, `src/interval_evaluator.cpp`, `include/kdc/interval_evaluator.hpp`, `src/global_bound_history.cpp`, `include/kdc/global_bound_history.hpp`, `src/weighted_geometry.cpp`, `include/kdc/weighted_geometry.hpp`, `include/kdc/weighted_static.hpp`, `include/kdc/weighted_oracle.hpp`, `src/weighted_upper_envelope.cpp`, `include/kdc/weighted_upper_envelope.hpp`, `include/kdc/solver_budget.hpp`, `include/kdc/types.hpp`, `CMakeLists.txt` و تست‌های split/interval/history.

## تغییرها
- `src/temporal_thesis_solver.cpp`: snapshot کردن Q پیش از freeze، پیش‌ساخت common partition، نگه‌داشتن leaf ناتمام در P با LB=0 و UB=∞، default heuristic در FAST_DOUBLE، exact شمارش/cap برای Oracleها، budget سراسری و budget جداگانه برای هر Oracle، توقف درست پیش از عبور از حدّ leaves، ساخت program کامل برای D+ fallback، ثبت leafهای خروجی، hash پایدارِ برچسب‌خورده، و گزارش صریحِ نبود snapshot گواه‌شده.
- `include/kdc/temporal_thesis_solver.hpp`: افزودن statusهای per-Oracle timeout و inconsistency، leafهای نهایی و زمان اجرا به نتیجه.
- `src/interval_evaluator.cpp`: انتخاب method و مقدار UB از یک منبع یکسان؛ fallback نمی‌تواند مقدار کوچک‌تری از direct envelope داشته باشد و هم‌زمان با program مستقیم ثبت شود.
- `src/temporal_split_policy.cpp`: ساخت program ثابت D+ برای child در صورت استفاده از fallback.
- `tests/test_temporal_thesis_solver.cpp`: پوشش split synthetic، partition breakpoint مشترک، global/per-Oracle timeout و Oracle-call cap.

## تصمیم‌ها و محدودیت‌ها
- علت SIGSEGV: حذف leaf جاری از `std::set` در حین range-for روی همان Q. iteration اکنون روی snapshot شناسه‌ها انجام می‌شود.
- خروجی‌های محاسبه‌شده با FAST_DOUBLE صرفاً `NUMERICALLY_UNBOUNDED` ثبت می‌شوند؛ به bounds گواه‌شده تبدیل نمی‌شوند. LB بدیهی صفر با دلیل دقیقِ نامنفی‌بودن انتگرال ثبت می‌شود.
- verifier مستقلِ arithmetic معتبر در مخزن/محیط فعلی حاضر نیست؛ بنابراین هیچ snapshot گواه‌شده‌ای تولید و `verified=true` صادر نمی‌شود. نتیجه صریحاً `NO_CERTIFIED_SNAPSHOT` را گزارش می‌کند. بررسی snapshot در صورت وجود فقط ساختاری است و گواه ریاضی محسوب نمی‌شود.
- تناقضی که verifier مستقل نتواند دوباره اعتبارسنجی کند، پنهان یا clamp نمی‌شود؛ خروجی reconciliation/incomplete باقی می‌ماند.
- عمداً هیچ اجرای benchmark بزرگ انجام نشد.

## آزمون‌ها
- بازتولید اولیه: تست synthetic با SIGSEGV.
- پس از اصلاح: `cmake --build build --target kdc-tests -j2` موفق.
- `./build/kdc-tests 'Temporal thesis solver*'`: موفق، ۳۹ assertion در ۵ test case.
- `ctest --test-dir build --output-on-failure`: موفق، ۱/۱ test CTest (مجموع Catch2) در حدود ۱۰.۷ ثانیه.
- بازبینی whitespace و ابزارگذاری موقت: موردی باقی نماند.

## پذیرش
- رفع crash و اجرای loop/resource guards روی مثال‌های synthetic: **PASS**.
- اجرای suite موجود: **PASS**.
- independently verify کردن best snapshot و گواه‌کردن boundهای زمانی: **BLOCKED**؛ backend arithmetic معتبر/verifier وجود ندارد و طبق قرارداد علمی، هیچ ادعای CERTIFIED صادر نشد.

## مسیرهای تغییرکرده
`include/kdc/temporal_thesis_solver.hpp`, `src/temporal_thesis_solver.cpp`, `src/interval_evaluator.cpp`, `src/temporal_split_policy.cpp`, `tests/test_temporal_thesis_solver.cpp`.
