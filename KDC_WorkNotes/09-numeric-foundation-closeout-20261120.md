# 09 — Numeric Foundation Closeout

**Verdict: `PARTIAL_BLOCKED`**

این closeout فقط لایهٔ پایهٔ حساب MPFR را ارزیابی می‌کند. ساخت کامل پروژه و اجرای آزمون‌های عددیِ همین revision در این محیط تأیید نشد. هیچ ادعای `CERTIFIED` یا موفقیت end-to-end مطرح نیست.

## خط مبنا و وضعیت مخزن

- مخزن: `/Users/amirpartovi/Desktop/Codes/KineticDiskCover`
- شاخه: `main`
- HEAD در آغاز کار: `9c709bf0dc50bdebe0a17110fb7b0e86922779e8` (`9`)
- در آغاز کار، `git status --short --branch` برابر `## main...upstream/main` بود؛ staged، unstaged و untracked وجود نداشت.
- تغییرات Stage 09 در HEAD ثبت شده‌اند. commit بالا شامل ۱۰ مسیر است: `CMakeLists.txt`؛ چهار Work Note baseline/گزارش در `KDC_WorkNotes/`؛ ADR و theorem map؛ `include/kdc/numeric.hpp`؛ `src/numeric.cpp`؛ و `tests/test_numeric.cpp`.
- تغییرات tracked این closeout نسبت به baseline: `docs/thesis_implementation/numeric_arithmetic_adr.md`، `docs/thesis_implementation/theorem_to_code_map.md`، `include/kdc/numeric.hpp` و `tests/test_numeric.cpp`. فایل گزارش حاضر خارج از repository است. هیچ فایل solver، dataset یا نتیجهٔ آزمایش تغییر نکرده است.
- مسیر دقیق گزارش: `/Users/amirpartovi/Desktop/KDC_WorkNotes/09-numeric-foundation-closeout-20261120.md`.
- دو build تلاش‌هایی برای تولید/به‌روزرسانی object fileهای محلی در `build/` انجام دادند؛ خطاها پیش از تکمیل target رخ دادند. build directory، cache یا artifactی حذف نشد.

## ابزار و وابستگی عددی

روش کشف: خواندن `build/CMakeCache.txt` و اجرای `pkg-config --modversion mpfr gmp` و `pkg-config --cflags --libs mpfr`.

| مورد | مقدار مشاهده‌شده |
|---|---|
| MPFR | 4.2.2 |
| GMP | 6.3.0 |
| CMake | 4.5.1 |
| Compiler | Apple clang 17.0.6 |
| Generator / build type | Unix Makefiles / Release |
| CMake compiler / make | `/usr/bin/c++` / `/usr/bin/make` |
| FetchContent | `FETCHCONTENT_FULLY_DISCONNECTED=ON` |
| MPFR link flags | `-L/opt/homebrew/Cellar/mpfr/4.2.2/lib -lmpfr -lgmp` |

هیچ package نصب/به‌روزرسانی یا network access انجام نشد. این closeout روی Linux اجرا نشد؛ سازگاری Linux تأیید نشده است.

## Build و آزمون‌ها

1. `cmake --build build --parallel 1` — **BLOCKED**. ساخت `spdlog` و تعدادی object از `kdc-core` آغاز/انجام شد؛ سپس `/bin/sh: Resource temporarily unavailable` و `make: *** wait: No child processes` رخ داد.
2. `cmake --build build --target kdc-tests --parallel 1` — **BLOCKED** در `src/minsum.cpp.o`: `clang++: error: unable to execute command: posix_spawn failed: Resource temporarily unavailable`. هیچ diagnostic کامپایل یا لینک دربارهٔ کد منبع صادر نشد.
3. بررسی منبع محدودیت محلی: `ulimit -u` برابر `2666` بود و `ps -U amirpartovi -o pid= | wc -l` تعداد `2682` فرایند را گزارش کرد. این مشاهده با شکست `fork`/`posix_spawn` سازگار است. هیچ فرایند دیگری متوقف نشد و هیچ workaround مخربی اعمال نشد.
4. `ctest --test-dir build --output-on-failure` — خروجی موجود `1/1` PASS در 1.27 ثانیه بود، اما executable از cache قدیمی بود. `build/kdc-tests --list-tests` و جست‌وجوی خروجی فهرست نشان دادند که هیچ‌یک از caseهای `NumericInterval` در آن binary وجود ندارد؛ بنابراین این اجرا، آزمون کامل revision فعلی یا foundation نیست.
5. آزمون هدفمند NumericInterval در این closeout: **۰ case اجراشده**. binary cacheشده caseهای عددی را نداشت و build جدید به‌علت process limit متوقف شد. Work Note مرحلهٔ قبلی، اجرای تاریخی ۷ case و ۳۸ assertion را PASS گزارش کرده بود؛ این سابقه جایگزین اجرای مجدد acceptance نمی‌شود.
6. سه case جدید برای signed zero/precision ownership، underflow/overflow و ورودی‌های نامعتبر افزوده شد؛ به‌دلیل build block اجرا یا حتی compile آن‌ها در این closeout تأیید نشد.
7. `git diff --check` — PASS.

پروب standalone MPFR و اجرای اولیهٔ arithmetic tests در Work Note مرحلهٔ پیاده‌سازی ثبت شده‌اند؛ هیچ‌کدام جای build کامل پروژه را نمی‌گیرند.

## Audit حساب بازه‌ای

پیاده‌سازی از MPFR با precision پیش‌فرض 128 بیت و rounding صریح `MPFR_RNDD`/`MPFR_RNDU` استفاده می‌کند. `double` با `mpfr_set_d` وارد می‌شود؛ در precision حداقل 53 بیت، مقدار binary64 واردشده دقیقاً نمایش‌پذیر است. قرارداد فعلی ورودی همان binary64 حاصل از parser JSON فعلی است، نه مقدار حقیقیِ متن ده‌دهی.

کد arithmetic شامل جمع، تفریق، ضرب چهار endpoint، تقسیم چهار endpoint، square، sqrt، مقایسهٔ بازه، جمع directed، enclosure عدد `pi`، ارزیابی چندجمله‌ای درجه‌دو و انتگرال antiderivative درجه‌دو است. تقسیم بر بازهٔ شامل صفر، sqrt با کران پایین منفی، endpoint نامتناهی/نامعتبر، و خروجی غیرمتناهی به‌صورت exception رد می‌شوند. بازهٔ خالی type جداگانه ندارد؛ endpointهای وارونه رد می‌شوند. مقادیر خیلی کوچک/بزرگ و رفتار conversion آن‌ها در source/tests بازبینی شدند، اما تست‌های edge افزوده‌شده در این closeout اجرا نشدند.

شیء MPFR دو `mpfr_t` را مالک است؛ copy constructor در precision مبدأ مقدارها را با rounding بیرونی کپی می‌کند، و assignment با copy-and-swap precision کپی را نگه می‌دارد. move constructor مشخصی وجود ندارد، بنابراین `std::move` به copy constructor برمی‌گردد؛ این مورد حافظه را جابه‌جا نمی‌کند اما source را معتبر نگه می‌دارد. تست‌های جدید مالکیت/precision به علت build block تأیید نشده‌اند.

### ذخیره‌سازی endpoint

`lower_decimal()` و `upper_decimal()` endpoint را به صورت رشتهٔ ده‌دهی با rounding بیرونی تولید می‌کنند و `from_decimal_bounds()` آن‌ها را با rounding بیرونی بازمی‌خواند. کلاس فعلی **JSON serializer/deserializer ندارد**. JSON number برای endpointهای arbitrary-precision تضمین enclosure نمی‌دهد؛ تا زمان تعریف schema نسخه‌دار، تنها ذخیرهٔ JSON به صورت string و restore با همین API قابل قبول است. backward-compatible schema correction پیش از persistence واقعی هنوز لازم است.

### Root-cover boundary

`root_cover_quadratic` برای یک چندجمله‌ای درجه‌دو با ضرایب بازه‌ای روی یک بازهٔ ورودی، از centered mean-value enclosure و subdivision استفاده می‌کند. candidate boxes، ریشه‌های ممکن را حذف نمی‌کنند اما می‌توانند نقاط غیرریشه یا چند ریشه را دربرگیرند.

status جدید `CANDIDATE_COVER_ONLY` (با نام legacy سازگار `COMPLETE_COVER`) فقط یعنی هیچ candidate box حل‌نشده‌ای با عرض بیش از حد درخواستی باقی نمانده؛ این status گواهی یکتایی، multiplicity، ترتیب ریشه‌ها، تشخیص قطعی tangency، یا completeness رویدادهای upper-envelope نیست. `BUDGET_EXHAUSTED` و `PRECISION_LIMIT` صریحاً unresolved می‌مانند و نواحی نامطمئن را حفظ می‌کنند. هیچ status این API به‌تنهایی مجوز event completeness نیست. جست‌وجوی callerها هیچ استفاده‌ای از این API در solver یا envelope فعلی نیافت. theorem-to-code map نیز همین مرز را ثبت می‌کند.

## مرز صدور گواهی

- جست‌وجوی callerها نشان داد `NumericInterval` فقط در header/source و تست‌های عددی مصرف می‌شود؛ مسیر bound حل‌گر هنوز آن را مصرف نمی‌کند.
- `validate_weighted_lp_dual()` آرگومان arithmetic را به `DOUBLE` تقلیل می‌دهد، نتیجه را فقط diagnostic ثبت می‌کند و `valid`/گواهی مستقل/directed LB صادر نمی‌کند. `IntervalEvaluator` فقط فیلدهای independently validated جداگانه را می‌پذیرد؛ تست‌های weighted-oracle همین جدایی را پوشش می‌دهند.
- وجود primitiveهای MPFR، وضعیت `OPTIMAL` یک solver، یا اجرای تست‌ها هیچ‌کدام به‌تنهایی `CERTIFIED` ایجاد نمی‌کنند. assurance کلی temporal solver همچنان numerical-only است.

## معیار پذیرش

| معیار | نتیجه |
|---|---|
| Branch و baseline دقیق ثبت شد | PASS |
| نسخه و روش کشف MPFR/GMP ثبت شد | PASS |
| حساب directed و مالکیت/precision بازبینی شد | PASS از نظر audit کد؛ edge tests جدید اجرا نشده |
| serialization arbitrary-precision دارای JSON contract است | FAIL / باقی‌مانده: serializer و schema نسخه‌دار وجود ندارد |
| root-cover به‌عنوان root isolation یا event completeness معرفی نشده | PASS؛ محدودیت‌ها صریح‌اند |
| LP dual یا مسیر solver به LB گواهی‌شده ارتقا نیافته | PASS |
| build کامل و CTest مربوط به revision فعلی | BLOCKED |
| آزمون‌های هدفمند NumericInterval | BLOCKED؛ ۰ case در closeout |
| Linux verification | NOT RUN |

**قبول نهایی: `PARTIAL_BLOCKED`.** علت اصلی: process cap محلی مانع build کامل و اجرای تست‌های NumericInterval همین revision شد؛ علاوه بر آن، JSON persistence برای endpointهای MPFR هنوز قرارداد serializer نسخه‌دار ندارد. این گزارش هیچ ادعای `E0 PASS` یا certification انتهایی ندارد.
