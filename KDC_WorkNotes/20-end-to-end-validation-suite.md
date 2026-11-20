# مرحلهٔ ۲۰ — مجموعهٔ اعتبارسنجی سرتاسری

## محدوده و baseline

دامنهٔ این مرحله افزودن آزمون‌های یکپارچهٔ کوچک برای مسیر Model A و ایجاد
نقشهٔ ردیابی پیاده‌سازی به گزاره‌ها و فرض‌های فصل‌های ۵ و ۶ بود. benchmark
بزرگ، commit، stage، و عملیات GitHub/remote اجرا نشد.

در baseline ثبت‌شدهٔ این مرحله، شاخه `main` بود و staged/tracked diff وجود
نداشت. فایل‌های پیاده‌سازی temporal و verifier از مراحل قبل untracked بودند؛
آن‌ها حفظ شدند و در این مرحله ویرایش نشدند. پس از کار نیز تنها دو فایل جدید
این مرحله به آن مجموعه اضافه شده‌اند.

## فایل‌های خوانده‌شده

- `CMakeLists.txt` و `scripts/local_validation.sh`
- APIهای `include/kdc/interval_evaluator.hpp`,
  `weighted_oracle.hpp`, `weighted_geometry.hpp`,
  `temporal_partition.hpp`, `global_bound_history.hpp`, و
  `model_a_snapshot_verifier.hpp`
- آزمون‌های `test_interval_evaluator.cpp`,
  `test_weighted_oracle.cpp`,
  `test_temporal_partition.cpp`,
  `test_temporal_split_policy.cpp`,
  `test_temporal_thesis_solver.cpp`,
  `test_temporal_result_v2.cpp`,
  `test_global_bound_history.cpp`, و
  `test_model_a_snapshot_verifier.cpp`
- منابع موجود در `KDC_WorkNotes`: `02-thesis-spec.md` و checkpointهای
  مراحل ۱۱، ۱۲، ۱۵، ۱۶، ۱۷، ۱۸ و ۱۹؛ همچنین استخراج‌های محلی فصل‌های ۵ و ۶.

## فایل‌های افزوده‌شده

- `tests/test_thesis_end_to_end.cpp`
- `docs/thesis_implementation/theorem_to_code_map.md`

آزمون جدید مسیرهای زیر را به هم متصل می‌کند:

- نمونهٔ تحلیلی تک‌نقطه‌ای: `D^- <= D(t) <= D^+` در چند زمان، کران پایین
  بدیهی و معتبر صفر، و کران بالای انتگرال مستقیم.
- Oracle ابتکاری فقط برای assignment/UB استفاده می‌شود؛ assignment موروثی
  خارج از دامنه رد می‌شود.
- کاندیدای دوآل LP با مقدار مثبت با optimum مرجع brute force مقایسه می‌شود؛
  آزمون صرفاً عددی است و ادعای arithmetic معتبر نمی‌کند.
- validator حذف یک leaf از پوشش افق را رد می‌کند.
- snapshot کامل serialize و reload می‌شود، سپس verifier مستقل با FAST_FLOAT
  آن را بررسی می‌کند؛ درخواست VALIDATED به‌درستی `NOT_CERTIFIED` می‌ماند.
- verifier کران UB را وقتی اندکی پایین‌تر از کران بالای enclosure هزینه است
  رد می‌کند.

نقشهٔ ردیابی، قراردادها و فرمول‌های عملیاتی قابل ارجاع را به implementation،
آزمون متناظر و assurance level پیوند می‌دهد. هرجا شمارهٔ قضیه/زیرعنوان از
استخراج محلی قابل تأیید نبود، شماره‌ای حدس زده نشده و محدودیت صریح شده است.
آزمون‌ها شواهد regression هستند، نه اثبات قضیه یا همگرایی.

## تصمیم‌ها و محدودیت‌ها

- آزمون‌ها از fixtureهای synthetic کوچک استفاده می‌کنند و اجرای benchmark
  ممنوع‌شده‌ای انجام نشد.
- تست دوآل و همهٔ دیگر نتایج `FAST_DOUBLE`/`long double` در طبقهٔ
  `numerical-only` باقی می‌مانند. backend حساب بازه‌ای معتبر همچنان موجود
  نیست؛ هیچ نتیجه‌ای به `CERTIFIED` ارتقا داده نشد.
- verifier مستقل artifactها را از دیسک بارگذاری و UB ذخیره‌شده را با endpoint
  بالای enclosure مقایسه می‌کند.
- آزمون‌های component مربوط به Oracle دقیق، relaxation، split/rollback،
  history، budget و status از قبل در مخزن موجود بودند؛ آزمون جدید آن‌ها را
  در یک fixture سرتاسری تکمیل می‌کند، نه اینکه ادعای پوشش همهٔ تزریق‌های
  budget در هر دستور داخلی را داشته باشد.
- هیچ وابستگی جدیدی اضافه نشد.

## اعتبارسنجی

- `bash scripts/local_validation.sh configure-build`: PASS.
- آزمون‌های هدفمند در `build/kdc-tests`: PASS؛ شش مسیر انتخاب‌شده شامل
  ۴، ۱۷، ۹، ۱، ۱۰ و ۳ assertion.
- `bash scripts/local_validation.sh ctest`: PASS، همهٔ تست‌های ثبت‌شده
  (CTest: 1/1، حدود 12.5 ثانیه).
- هشدارهای CMake دربارهٔ FetchContent timestamp policy و حداقل نسخهٔ CMake
  در Eigen ظاهر شدند؛ build موفق بود و هشدارها به تغییرهای این مرحله مربوط
  نبودند.
- artifactهای موقت آزمون از `/tmp` پاک شدند.
- بازبینی نهایی: tracked/staged diff خالی؛ فایل‌های قبلی untracked دست‌نخورده؛
  build زیر `build/` در `.gitignore` است.

## پذیرش و checkpoint

- آزمون سرتاسری، fault detection و serialization/reload: **PASS**.
- نقشهٔ ردیابی فصل‌های ۵ و ۶ با assurance و محدودیت‌های صریح: **PASS**.
- ادعای `CERTIFIED`، حساب بازه‌ای معتبر، یا اثبات همگرایی: **ارائه نشد**؛
  نبود backend معتبر محدودیت باقی‌مانده است.
- نتیجهٔ مرحله: **PASS با محدودیت assurance عددی**.
- checkpoint: `/Users/amirpartovi/Desktop/KDC_WorkNotes/20-end-to-end-validation-suite.md`
