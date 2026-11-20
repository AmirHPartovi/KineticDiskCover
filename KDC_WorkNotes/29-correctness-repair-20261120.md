# یادداشت کاری ۲۹ — تعمیر صحت برنامه زمانی

## وضعیت و محدوده

این مرحله فقط تعمیر صحت پیش از اجرای دوباره E0 بود. هیچ benchmark بزرگ، Stage 28، عملیات remote، تغییر شاخه، stage، commit یا push اجرا نشد. E0 همچنان **BLOCKED** است و این تغییرات به‌تنهایی عبور E0 را اثبات نمی‌کنند.

محدوده فایل‌ها: نمایش و سریال‌سازی برنامه Model A، ساخت برنامه envelope و محاسبه UB، verifier پیوسته، معنای گواهی dual در LP، و شواهد fixtureهای تکمیلی. فایل‌های نتیجه قبلی بازنویسی نشدند؛ fixtureهای جدید شناسه و هش مستقل دارند.

## خط مبنای Git

پیش از تغییرات، وضعیت ذخیره‌شده در
`/Users/amirpartovi/Desktop/KDC_WorkNotes/28-correctness-repair-baseline-20261120.txt`
ثبت می‌کند که شاخه `main` پاک بوده و staged، tracked یا untracked change نداشته است. بازبینی نهایی بر مبنای همین وضعیت انجام شد: ۲۰ فایل tracked تغییر کرده، دو فایل جدید افزوده شده، و هیچ تغییری stage نشده است. شاخه همان `main` باقی ماند.

## نقص و بازتولید

نقص اصلی، ناسازگاری بین UB محاسبه‌شده از envelope زمانی و برنامه‌ای بود که snapshot ذخیره می‌کرد: ارزیاب، انتگرال تابع متغیر `q_i(t)` را محاسبه می‌کرد، اما نمایش V1 فقط شعاع ثابت برای هر قطعه نگه می‌داشت. بنابراین برنامه ذخیره‌شده لزوماً همان تابع و هزینه‌ای را که به‌عنوان UB گزارش شده بود بازسازی نمی‌کرد.

بازتولید کمینه، ایستگاه در `(0,0)` و نقطه `p(t)=(t,0)` روی `[0,1]` است. تابع موردنیاز `q(t)=t²` و انتگرال هزینه `pi/3` است؛ یک رکورد شعاع ثابت در کل قطعه این تابع را بازنمایی نمی‌کند. آزمون‌های `SUP-PROG-001` تا `SUP-PROG-005` اکنون برنامه سریال‌شده، پوشش پیوسته، و تطبیق هزینه برنامه با UB را برای حرکت مستقیم، حرکت معکوس، تغییر تابع فعال، ایستگاه خالی و مرز مشترک بررسی می‌کنند.

در حین رفع، یک نقص مرتبط در تاریخچه نیز آشکار شد: UB برنامه کامل با اعتبار عددی در aggregate جاری وجود داشت، اما شرط completeness snapshot فقط `VALID` را می‌پذیرفت، در نتیجه `ub_best` حذف می‌شد. شرط تکمیل snapshot اکنون برنامه‌های کامل `NUMERICALLY_UNBOUNDED` را نیز نگه می‌دارد؛ این تغییر صرفاً نگهداری incumbent عددی است و گواهی یا CERTIFIED ایجاد نمی‌کند.

## تصمیم‌های طراحی

- V1 بدون تغییر معنایی باقی ماند: رکوردهای قدیمی شعاع ثابت هستند و به‌صورت خاموش به چندجمله‌ای تعبیر نمی‌شوند.
- V2 با `q_i(t)=a(t-t_0)^2+b(t-t_0)+c`، بازه هر قطعه، مبدأ زمان، تخصیص، provenance و نسخه schema ذخیره می‌شود.
- envelope در نقاط break و ریشه‌های تعویض تابع فعال شکسته می‌شود. UB از ضرایب همان برنامه‌ای محاسبه می‌شود که serialize می‌شود؛ verifier ورودی و برنامه ذخیره‌شده را مستقلاً بارگذاری کرده و پوشش را با کمینه چندجمله‌ای روی کل زیربازه می‌سنجد، نه نمونه‌برداری.
- محاسبات envelope/هندسه فعلی عددی‌اند؛ `nextafter`, `long double` و وضعیت solver به‌تنهایی enclosure معتبر نمی‌سازند.
- اعتبارسنجی dual LP فقط تشخیص عددی برمی‌گرداند. حتی درخواست `CONSERVATIVE_INTERVAL`، با محاسبات معمول `double`/`long double`، فیلدهای گواهی مستقل یا LB جهت‌دار تولید نمی‌کند؛ evaluator نمی‌تواند آن را به LB رسمی تبدیل کند.
- پنج descriptor تکمیلی با hash canonical، شناسه مورد پایدار، مرجع قرارداد و خاصیت مورد انتظار افزوده شدند. این‌ها صریحاً synthetic هستند و جای داده X/EC تولیدشده با PRNG یا master seed نامعلوم پایان‌نامه را نمی‌گیرند.

## فایل‌های تغییرکرده

۲۰ فایل موجود:

- `include/kdc/temporal_partition.hpp`
- `include/kdc/interval_evaluator.hpp`
- `include/kdc/weighted_upper_envelope.hpp`
- `include/kdc/global_bound_history.hpp`
- `include/kdc/weighted_oracle.hpp`
- `src/temporal_partition.cpp`
- `src/weighted_upper_envelope.cpp`
- `src/interval_evaluator.cpp`
- `src/temporal_thesis_solver.cpp`
- `src/temporal_split_policy.cpp`
- `src/global_bound_history.cpp`
- `src/temporal_pipeline.cpp`
- `src/model_a_snapshot_verifier.cpp`
- `src/weighted_oracle.cpp`
- `tests/test_model_a_snapshot_verifier.cpp`
- `tests/test_temporal_thesis_solver.cpp`
- `tests/test_temporal_pipeline_integration.cpp`
- `tests/test_thesis_end_to_end.cpp`
- `tests/test_weighted_oracle.cpp`
- `docs/thesis_implementation/theorem_to_code_map.md`

دو فایل افزوده:

- `tests/fixtures/e0_supplemental_program_v2_v1.json`
- `tests/python/test_e0_supplemental_fixtures.py`

## اعتبارسنجی و شمارش اجرا

| بررسی | نتیجه واقعی |
|---|---:|
| ساخت C++ (`cmake --build build --parallel 2`) | PASS |
| آزمون‌های هدفمند solver و مسیر pipeline | ۲ case، ۴۴ assertion، PASS |
| کل آزمون‌های Catch2 (`build/kdc-tests --reporter compact`) | ۳۶۷ case، ۴۰٬۳۶۰ assertion، PASS |
| CTest (`bash scripts/local_validation.sh ctest`) | ۱ تست ثبت‌شده، PASS؛ این تست suite کامل Catch2 را اجرا می‌کند |
| Python (`bash scripts/local_validation.sh python-tests`) | ۱۹۱ passed، صفر failed |
| fixtureهای synthetic برنامه V2 | ۵ مورد تعریف‌شده در fixture و پوشش آزمونی |
| whitespace (`git diff --check`) | PASS |

هشدارهای Python شامل deprecation/future warning از Matplotlib، PyParsing، Pandas و گزارش‌سازی موجود بود؛ تست‌ها موفق شدند.

## محدودیت اطمینان و معیار پذیرش

- **تعمیر نمایش برنامه و حفظ سازگاری V1: PASS**؛ مسیرهای V2 و loader قدیمی آزموده شدند.
- **جلوگیری از ارتقای dual عددی به گواهی: PASS**؛ آزمون‌های dual و جلوگیری از مصرف آن به‌عنوان LB اجرا شدند.
- **تطبیق cost برنامه سریال‌شده با UB برای پنج مثال تکمیلی: PASS عددی**؛ این اثبات enclosure معتبر یا پوشش همه ورودی‌ها نیست.
- **آمادگی اجرای مجدد/عبور E0: BLOCKED**. طبق وضعیت قبلی، داده‌های اصلی X: صفر از ۹۰، EC اصلی: صفر از ۱۳ در دسترس بودند؛ backend حساب معتبر نیز موجود نیست و یک مورد نزدیک مرز verifier هنوز `INDETERMINATE` است. این تعمیر آن محدودیت‌ها را حل نکرده و E0 را اجرا یا پاس اعلام نمی‌کند.
- assurance کلی محاسبات هندسی و envelope همچنان `numerical-only` است. هیچ ادعای همگرایی اثبات‌شده یا CERTIFIED ارائه نمی‌شود.

## مسیر این یادداشت

`/Users/amirpartovi/Desktop/KDC_WorkNotes/29-correctness-repair-20261120.md`
