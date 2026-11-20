# مرحلهٔ ۲۴ — پیاده‌سازی baselineهای زمانی فصل ۷

## وضعیت

**PASS با محدودیت assurance ثبت‌شده.** چهار روش زمانی به‌عنوان مسیر مستقل
قابل انتخاب‌اند؛ مسیرهای `legacy` و `minsum-adaptive` حفظ شده‌اند. هیچ
benchmark بزرگ اجرا نشد و هیچ عملیات Git/remote انجام نشد.

## baseline و حفاظت از تغییرها

- مخزن: `/Users/amirpartovi/Desktop/Codes/KineticDiskCover`
- شاخه: `main`
- staged changes: خالی؛ commit/push اجرا نشد.
- این turn از یک پیاده‌سازی نیمه‌تمام مرحلهٔ ۲۴ ادامه یافت. خلاصهٔ قبلی،
  checkpoint مرحلهٔ ۲۳ و وضعیت موجود را مرجع baseline گرفت: تغییرهای pipeline
  قبلی و فایل‌های temporal untracked حفظ شدند. snapshot مستقل `git status`
  پیش از نخستین ویرایش این turn در دسترس نبود؛ این محدودیت در ثبت baseline
  پنهان نشده است.
- تست کامل چند فایل زیر `results/` را تغییر داد. چون پیش از اجرای تست پاک
  بودند، فقط فایل‌های دقیق ساخته‌شده با تست حذف و دو فایل tracked به baseline
  پیش از تست برگردانده شدند. در پایان `results/` پاک است.

## فایل‌های خوانده‌شده

- `src/temporal_baselines.cpp`, `include/kdc/temporal_baselines.hpp`,
  `src/temporal_split_policy.cpp`,
  `include/kdc/temporal_split_policy.hpp`,
  `include/kdc/temporal_thesis_solver.hpp`
- `src/benchmark.cpp`, `src/batch_runner.cpp`, `src/main.cpp`,
  `include/kdc/benchmark.hpp`, `include/kdc/batch_runner.hpp`,
  `include/kdc/pipeline_solver.hpp`
- `tests/test_temporal_split_policy.cpp`,
  `tests/test_temporal_pipeline_integration.cpp`,
  `tests/test_cli.cpp`, تست‌های weighted Oracle و geometry
- checkpoint مرحلهٔ ۲۳:
  `/Users/amirpartovi/Desktop/KDC_WorkNotes/23-pipeline-solver-selection.md`
- متن استخراج‌شدهٔ فصل ۷، بخش‌های 7.3.2 و 7.3.3، جدول 7.6،
  صفحات 179–180:
  `/Users/amirpartovi/Desktop/KDC_WorkNotes/thesis-ch7-correct.txt`

## فایل‌های تغییرکرده/افزوده‌شده در این مرحله

- تغییر: `src/main.cpp`, `src/benchmark.cpp`, `src/batch_runner.cpp`,
  `include/kdc/benchmark.hpp`, `include/kdc/batch_runner.hpp`,
  `src/temporal_baselines.cpp`, `tests/test_cli.cpp`,
  `tests/test_temporal_pipeline_integration.cpp`,
  `tests/test_temporal_split_policy.cpp`
- افزوده: `tests/test_temporal_baselines.cpp`,
  `docs/thesis_implementation/chapter7_temporal_baselines.md`

## تصمیم‌ها و رفتار نهایی

- `b-grid` از همان solver زمانی استفاده می‌کند و راهبرد انتخاب برگ را به
  `LONGEST_INTERVAL` تغییر می‌دهد؛ تساوی با زمان شروع کوچک‌تر و سپس leaf ID
  کوچک‌تر حل می‌شود.
- `b-nn` مستقل از static `nn` اجرا می‌شود: ریشه‌های اختلاف فاصلهٔ مربعی
  برای زمان‌های تغییر انتساب اضافه می‌شوند و tie مرزی با ID کوچک‌تر ایستگاه
  حل می‌شود.
- `b-static-hold` انتساب زمان صفر را در کل افق نگه می‌دارد. فقط
  `optimality_certified` از Exact Oracle وضعیت `OPTIMAL_AT_ZERO` می‌دهد؛
  خروجی heuristic یا FEASIBLE صرفاً candidate است.
- `b-minmax` برای هر بازه روی `D^+(I)` زیرمسئلهٔ MinMax را حل می‌کند و از
  شعاع ثابت ساخته‌شده هزینهٔ برنامهٔ MinSum را گزارش می‌دهد؛ MinMax به‌عنوان
  MinSum optimum/LB معرفی نمی‌شود.
- هر baseline نوع خروجی نسخه‌دار و فیلدهای مخصوص خود دارد؛ CSV/Markdown
  فیلدهای هر روش را جدا می‌کنند. JSONهای غیرمتناهی به‌صورت null/وضعیت صریح
  باقی می‌مانند. Loader نتیجهٔ قدیمی حفظ شد.
- `program_valid` پیش‌تر هنگام ساخت برنامه، مقدار `complete=false` را
  پیش‌شرط می‌گرفت و در نتیجه همهٔ برنامه‌های تازه‌ساخته را رد می‌کرد. این
  پیش‌شرط حذف شد تا اعتبارسنجی ساختاری مستقل از پرچم نتیجه انجام شود.
- مقدار `temporal_result_v2` و `temporal_baseline_result_v1` اکنون واقعاً
  با JSON null آغاز می‌شود؛ brace-init قبلی null واقعی تولید نمی‌کرد.
- روش‌های baseline در CLI، manifest، تنظیمات resolveشده، benchmark و batch
  dispatch می‌شوند؛ baselineها به `StaticSolverRegistry` واگذار نشده‌اند.
- وضعیت baseline `COMPLETE` به معنی تکمیل ساختاری برنامه است، نه اثبات
  پیوسته. خروجی‌های baseline در حال حاضر `FAST_FLOAT_ONLY` هستند و هیچ‌یک
  MinSum LB یا UB رسمی صادر نمی‌کنند.

## اعتبارسنجی

- `cmake --build build --target kdc-tests kdc-solver -j4`: **PASS**.
- تست‌های متمرکز baseline و dispatch: **PASS، ۸ test case و ۸۳ assertion**.
- کل `build/kdc-tests --reporter compact`: **PASS، ۳۵۹ test case و
  ۴۰۲۵۷ assertion**.
- `git diff --check`: **PASS**.
- اجرای B-nn از CLI با dataset synthetic کوچک، round-trip JSON، benchmark
  B-grid/B-minmax/B-static-hold، batch B-nn و batch B-static-hold بدون
  backend: **PASS**.
- benchmark بزرگ/تجربی اجرا نشد.

## پذیرش و ریسک

- B-grid, B-nn, B-static-hold, B-minmax: **PASS** برای dispatch, output
  schema, بودجه‌ها, ساخت برنامه و semantics تفکیک‌شده.
- تست تحلیلی B-nn، انتساب مرزی بر اساس شناسهٔ ایستگاه، هزینهٔ انتگرالی دستی،
  B-static-hold با Exact/heuristic، B-minmax در برابر enumeration، و tie key
  B-grid: **PASS**.
- **ریسک/محدودیت:** هزینه و برنامه‌های baseline هنوز assurance عددی
  `FAST_FLOAT_ONLY` دارند؛ به‌همین دلیل به‌عنوان گواهی MinSum گزارش نمی‌شوند.
  خروجی MinMax فقط هزینهٔ برنامهٔ ساخته‌شده است.
- پذیرش نهایی مرحله: **PASS با محدودیت assurance عددی؛ بدون ادعای
  certification یا اجرای benchmark بزرگ.**

Checkpoint:
`/Users/amirpartovi/Desktop/KDC_WorkNotes/24-chapter7-temporal-baselines.md`
