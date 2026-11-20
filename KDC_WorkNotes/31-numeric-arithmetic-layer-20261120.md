# یادداشت کاری ۳۱ — لایهٔ حساب بازه‌ای

## وضعیت و دامنه

این مرحله نیازهای عددی فصل ۵، معنای اعداد ورودی، وابستگی‌های محلی و امکان ساخت یک لایهٔ حساب بازه‌ای را بررسی کرد. یک foundation مبتنی بر MPFR اضافه شد، اما هیچ مسیر کران رسمی به آن متصل نشد؛ بنابراین خروجی حل‌گر همچنان `NUMERICAL_ONLY` است و وضعیت E0 از این کار عبور نمی‌کند.

## خط مبنای Git

پیش از ویرایش، شاخه `main` و درخت کار پاک بود؛ هیچ تغییر staged، unstaged یا untracked وجود نداشت. ثبت baseline:
`/Users/amirpartovi/Desktop/KDC_WorkNotes/30-numeric-adr-baseline-20261120.txt`

در پایان، فقط CMake، جدول ردیابی، و سه فایل جدید مربوط به numeric layer/آزمون‌ها تغییر کردند؛ شاخه عوض نشد و هیچ فایل نتیجه یا dataset تغییر نکرد. فایل‌های موقت `/tmp` حذف شدند.

## مرجع علمی و معنای داده

فایل مرجع فصل ۵:
`/Users/amirpartovi/Desktop/Texts/MScThesisPersian/Chapters/05_Main_Algorithms.tex`

قراردادهای مهم استخراج‌شده:

- §5.1.1: حساب دقیق برای ورودی‌های با نمایش دقیق یا حساب بازه‌ای با گردکردن جهت‌دار؛ حساب ممیزشناور معمولی بدون enclosure، گواهی نیست.
- §5.2.2-§5.2.3: feasibility دوگان باید مستقل بررسی شود؛ مقدار دوگان پس از ضرب در `pi` برای LB رو به پایین گرد شود؛ LP primal به‌تنهایی LB نیست.
- §5.3، Proposition 5.7 و §5.8.5: پوشش پیوستهٔ برنامه و انتگرال UB باید روی کل قطعه و با endpoint بالایی enclosure راستی‌آزمایی شود.
- §5.3، Proposition 5.8: ریشه‌ها باید با بازه‌های جداساز پوشانده شوند، ترتیب و تابع فعال گواهی شود، و مقایسهٔ حل‌نشده باعث `NOT_CERTIFIED` یا محاسبهٔ محافظه‌کارانه شود.
- §5.5: مقادیر LB رو به پایین، مقادیر UB و ثابت‌های کران بالا رو به بالا گرد شوند؛ همین جهت در ضرب، محاسبهٔ `D^- / D^+` و تجمیع سراسری حفظ شود.
- §5.4.2 و §5.7: جمع محلی LB رو به پایین و جمع UB رو به بالا باشد؛ تناقض نباید با clamp پنهان شود.

 loader فعلی در `src/io.cpp` با `nlohmann::json` اعداد مختصات، زمان شکست و `T_end` را به `Value=double` تبدیل می‌کند (`include/kdc/types.hpp`). مشخصات Chapter 7 نیز برای `FLOAT64` ذخیرهٔ IEEE-754 binary64 و JSON round-trip را ذکر می‌کند. بنابراین قرارداد انتخاب‌شده **مقدار دقیق binary64 پس از parse فعلی** است، نه مقدار حقیقیِ ده‌دهی متن JSON. `mpfr_set_d` مقدار binary64 را در دقت دست‌کم ۵۳ بیت دقیق وارد می‌کند. تغییر به مدل ده‌دهی مستلزم schema جدید و parse مستقیم token اصلی JSON به MPFR پیش از تبدیل به `double` خواهد بود.

## backend و سازگاری

بدون نصب/دانلود عمدی، محیط موجود بررسی شد: macOS arm64، Apple Clang 17.0.6، CMake 4.5.1، C++17، MPFR 4.2.2 با LGPL-3.0-or-later، GMP 6.3.0 با LGPL-3.0-or-later یا GPL-2.0-or-later، Boost 1.92 با BSL-1.0، و MPFI غایب. پروب مستقیم MPFR با Clang، `MPFR_RNDD/RNDU` و `sqrt(2)` موفق شد. سازگاری Linux با MPFR انتظار می‌رود اما روی Linux این مرحله آزمایش نشد.

انتخاب MPFR بر Boost.Interval ترجیح داده شد، چون عملیات پایه و توابع ریاضی را با rounding mode صریح و correctly-rounded اجرا می‌کند؛ root-cover درجه‌دو باید جداگانه پیاده شود. CMake از `PkgConfig::MPFR` (حداقل نسخهٔ 4.0) استفاده می‌کند و GMP از طریق pkg-config لینک می‌شود. نصب دستی انجام نشد. پیکربندی اول به FetchContent موجود در پروژه رسید و هنگام populate/build وابستگی با خطای کمبود process متوقف شد؛ پس از دیدن source-cacheهای محلی، پیکربندی مجدد با `FETCHCONTENT_FULLY_DISCONNECTED=ON` موفق شد. رفتار refresh تلاش اول به‌طور مستقل audit نشد؛ بنابراین در این یادداشت ادعای قطعی دربارهٔ عدم refresh شدن cache ندارم.

## پیاده‌سازی و اطمینان واقعی

`include/kdc/numeric.hpp` و `src/numeric.cpp` شامل `NumericInterval` هستند: جمع/تفریق/ضرب/تقسیم جهت‌دار؛ square و sqrt؛ ارزیابی چندجمله‌ای درجه‌دو؛ comparison با نتیجهٔ مستقل برای overlap؛ root-cover محافظه‌کارانه با centered mean-value enclosure و وضعیت‌های بودجه/دقت؛ انتگرال درجه‌دو؛ `pi`؛ و جمع بازه‌ای. تقسیم روی بازه‌ای شامل صفر، ورودی نامتناهی و خروجی غیرمتناهی رد می‌شوند. ذخیرهٔ endpoint به صورت ده‌دهی رشته‌ای نیز رو به بیرون گرد می‌شود؛ مقایسهٔ tokenهای ده‌دهی در helper فعلی exponent با قدر مطلق حداکثر یک میلیون را می‌پذیرد.

این primitives **validated arithmetic foundation** هستند، نه گواهی انتهایی. root-cover تضمین می‌کند ریشهٔ ممکن حذف نشود، اما candidate box می‌تواند شامل نقطهٔ بدون ریشه باشد؛ یکتایی ریشه و ترتیب active-envelope را ثابت نمی‌کند. `IntervalEvaluator`، هندسه، verifier dual، ساخت D-/D+، envelope و snapshot هنوز به این لایه متصل نیستند. بنابراین هیچ LB/UB رسمی یا وضعیت `CERTIFIED` از آن صادر نمی‌شود.

## فایل‌های خوانده/تغییریافته

فایل‌های مرجع/کد خوانده‌شده: Chapter 5 TeX، `CMakeLists.txt`، `src/io.cpp`، `include/kdc/types.hpp`، `docs/schemas/dataset_instance.schema.json`، `docs/thesis_implementation/chapter7_generator_spec_v1.json` و theorem map.

فایل‌های تغییرکرده/افزوده‌شده:

- `CMakeLists.txt`
- `include/kdc/numeric.hpp` (جدید)
- `src/numeric.cpp` (جدید)
- `tests/test_numeric.cpp` (جدید)
- `docs/thesis_implementation/numeric_arithmetic_adr.md` (جدید)
- `docs/thesis_implementation/theorem_to_code_map.md`

ADR درون مخزن:
`docs/thesis_implementation/numeric_arithmetic_adr.md`

## آزمون‌ها و نتیجه

- پروب compile/link/run مستقل MPFR و مشاهدهٔ enclosure برای `sqrt(2)`: PASS.
- پیکربندی CMake با `FETCHCONTENT_FULLY_DISCONNECTED=ON`: PASS.
- کامپایل هدفمند `src/numeric.cpp` و `tests/test_numeric.cpp` با Apple Clang و پرچم‌های هشدار پروژه؛ آزمون‌ها: **۷ case، ۳۸ assertion، PASS**. اجرای نهایی binary پس از بازگرداندن exponent parser به منطق آزموده‌شده نیز PASS بود.
- ساخت کامل CMake: BLOCKED. `cmake --build build --parallel 1` با `/bin/sh: Resource temporarily unavailable` در زمان ساخت target موجود spdlog متوقف شد. تلاش compile هدفمند بعدی نیز به علت `posix_spawn failed: Resource temporarily unavailable` اجرا نشد. بنابراین این محیط نتوانست لینک نهایی target کامل پروژه را تأیید کند.
- `git diff --check`: PASS.

## معیار پذیرش و محدودیت

| معیار | نتیجه |
|---|---|
| بررسی نیازهای عددی فصل ۵ و معناشناسی JSON | PASS |
| انتخاب backend حاضر و بررسی license/toolchain/CMake | PASS روی macOS محلی؛ Linux اجرا نشده |
| primitives بازه‌ای و آزمون‌های enclosure | PASS برای ۷ case/۳۸ assertion |
| اجرای build کامل پروژه | BLOCKED به دلیل عدم امکان spawn در محیط |
| صدور bound گواهی‌شدهٔ Model A | NOT IMPLEMENTED؛ عمداً این مرحله به solver وصل نشد |

محدودیت‌های باقیمانده: اتصال تمام مسیرهای هندسه/Oracle/envelope/verifier، گواهی فعال‌بودن و ترتیب ریشه‌ها، انتقال enclosure ضرایب/مختصات در کل pipeline، و آزمون روی Linux. نتیجهٔ کلی solver همچنان numerical-only است؛ ادعای certification یا E0 PASS وجود ندارد.
