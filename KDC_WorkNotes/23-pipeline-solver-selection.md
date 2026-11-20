# مرحلهٔ ۲۳ — اتصال انتخاب‌گر حل‌گر به pipeline

## وضعیت

**PASS** برای اتصال و schemaهای pipeline؛ بدون commit یا عملیات remote. اجرای
نمونهٔ temporal به‌درستی به `INCOMPLETE` ختم می‌شود، چون در مسیر فعلی
`FAST_DOUBLE` مدرک معتبر برای ثبت UB تاریخی تولید نمی‌شود. نتیجه به‌عنوان
`CERTIFIED` یا UB معتبر گزارش نشده و snapshot ناموجود به‌صورت `null` مانده است.

## baseline

- مخزن: `/Users/amirpartovi/Desktop/Codes/KineticDiskCover`
- شاخه: `main`، متصل به `upstream/main`
- baseline از checkpoint مرحلهٔ ۲۲: tracked diff و staged changes نداشت؛
  فایل‌های مراحل پیشین untracked بودند و باید حفظ می‌شدند.
- در این مرحله فایل‌های untracked مراحل قبلی دست‌نخورده ماندند. خطوط log
  ساخته‌شده توسط اجرای تست CLI با تطبیق baseline پاک شدند؛
  `results/logs/kdc.log` اکنون diff ندارد.
- در پایان، هیچ فایل staged یا commit نشده است.

## فایل‌های خوانده‌شده

- `src/main.cpp`, `src/benchmark.cpp`, `src/batch_runner.cpp`
- `include/kdc/benchmark.hpp`, `include/kdc/batch_runner.hpp`,
  `include/kdc/pipeline_solver.hpp`, `include/kdc/temporal_pipeline.hpp`
- `src/temporal_pipeline.cpp`
- `tests/test_cli.cpp`, `tests/test_temporal_pipeline_integration.cpp`,
  `tests/test_result_status.cpp`
- `docs/thesis_implementation/pipeline_solver_selection.md`
- قراردادهای solver و serialization temporal در `temporal_thesis_solver.hpp`
  و `temporal_result_v2.hpp`

## فایل‌های تغییریافته یا افزوده‌شده

- تغییر: `include/kdc/benchmark.hpp`, `include/kdc/batch_runner.hpp`,
  `src/main.cpp`, `src/benchmark.cpp`, `src/batch_runner.cpp`,
  `tests/test_cli.cpp`
- افزوده: `include/kdc/pipeline_solver.hpp`,
  `include/kdc/temporal_pipeline.hpp`, `src/temporal_pipeline.cpp`,
  `tests/test_temporal_pipeline_integration.cpp`,
  `docs/thesis_implementation/pipeline_solver_selection.md`
- cleanup آزمون: `results/logs/kdc.log` به baseline پاک بازگردانده شد و
  در diff نهایی نیست.

## تصمیم‌ها و رفتار نهایی

- call graph قدیمی در مستندات ثبت شد: `algorithm_names` و
  `StaticSolverRegistry` مسیر static/MinSum را نگه می‌دارند؛ dispatch temporal
  جداگانه مستقیماً `TemporalThesisSolver::solve_v2` را فراخوانی می‌کند.
- `legacy` پیش‌فرض باقی ماند؛ `minsum-adaptive` همان مسیر MinSum قبلی را با
  `HEURISTIC_ADAPTIVE` انتخاب می‌کند؛ `temporal-interval-bnb` فقط برای
  `minsum` پذیرفته می‌شود. `AlgorithmComparator` همچنان static-only است.
- روش انتخابی و پیکربندی resolveشده در CLI، manifest، benchmark و batch ثبت
  می‌شوند. بودجهٔ global از `--fast-time-limit` و بودجهٔ جداگانهٔ هر Oracle
  از `--oracle-time-limit` گرفته شده و به زمان باقی‌ماندهٔ global محدود است.
- schemaهای قبلی قابل‌خواندن ماندند. benchmark زمانی از schema سطح بالای ۳،
  batch زمانی از schema سطح بالای ۴، و هر دو از `temporal_result_v2` نسخهٔ ۲
  استفاده می‌کنند. JSON مقادیر غیرمتناهی را `null` می‌کند؛ CSV و Markdown
  temporal فیلدهای مخصوص خود را دارند.
- خروجی incomplete به‌دلیل cap/resolution، شکست عملیاتی شمرده نمی‌شود؛
  `failed` برای ورودی نامعتبر، خطای اجرا، شکست verification/reconciliation یا
  ناسازگاری محفوظ است. پایان زمانی جداگانه گزارش می‌شود.
- نمونهٔ FAST_FLOAT واقعاً solver temporal و Oracle را اجرا کرد، اما UB معتبر
  یا snapshot نداشت؛ تست همین نتیجهٔ محتاطانه را الزام می‌کند، نه صدور
  incumbent ساختگی.

## اعتبارسنجی

- `cmake --build build --target kdc-solver kdc-tests -j4`: **PASS**.
- مجموعهٔ متمرکز: **PASS، ۷ test case و ۷۵ assertion**؛ شامل adaptive MinSum،
  schema benchmark زمانی، batch بدون ILP و حالت خطا، CLI help/parse، بارگذاری
  نتیجهٔ legacy و round-trip وضعیت‌ها.
- `git diff --check`: **PASS**.
- benchmark بزرگ و کل CTest اجرا نشدند؛ فقط نمونه‌های synthetic کوچک اجرا
  شدند.
- هیچ وابستگی یا تغییر remote اضافه نشد.

## پذیرش و ریسک باقی‌مانده

- dispatch واقعی، ثبت پیکربندی، سازگاری loader قدیمی، schema مجزا و no-backend
  batch: **PASS**.
- MinSum قدیمی و رفتار پیش‌فرض: **PASS** در آزمون pipeline تطبیقی.
- اجرای B&B زمانی: **PASS** برای dispatch واقعی؛ خروجی این نمونه **INCOMPLETE**
  است، چون backend حساب معتبر/گواهی مورد نیاز در این اجرا موجود نیست. این
  محدودیت علمی پنهان نشده است.
- ریسک: پشتیبانی temporal به‌عنوان الگوریتم `AlgorithmComparator` اضافه نشده؛
  آن مسیر قرارداد static خود را حفظ کرده است.
- پایان کار: **PASS با محدودیت assurance عددیِ ثبت‌شده**.

Checkpoint:
`/Users/amirpartovi/Desktop/KDC_WorkNotes/23-pipeline-solver-selection.md`
