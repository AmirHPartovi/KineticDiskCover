# مرحلهٔ ۱۸: توقف و وضعیت نهایی V2

## محدوده

افزودن خروجی temporal نسخهٔ ۲ برای توقف بر اساس شکاف، طبقه‌بندی status و stop reason، متریک‌های شکاف و provenance مستقل کران‌ها. semantics خروجی V1 حفظ شد. حلقهٔ V1 برای زودتر متوقف‌شدن هنگام بسته‌شدن شکاف به‌روزرسانی نشد؛ در اجرای فعلی verifier مستقل، arithmetic معتبر و snapshot گواه‌شده در دسترس نیستند، بنابراین V2 نباید `CERTIFIED` صادر کند.

## baseline

- شاخه: `main`
- پیش از این مرحله، `git status` شامل فایل‌های untracked مراحل قبلی بود؛ staged یا tracked diff وجود نداشت.
- تغییرات این مرحله فقط محلی‌اند؛ commit، stage و دسترسی راه‌دور انجام نشد.

## فایل‌های این مرحله

- `include/kdc/temporal_result_v2.hpp`: schema نسخهٔ ۲ شامل statusها، stop reasonها، متریک‌ها، آمار Oracle و metadata مستقل LB/UB.
- `src/temporal_result_v2.cpp`: finalizer، اعتبارسنجی پیش‌شرط‌های CERTIFIED، شکاف محافظه‌کارانه، طبقه‌بندی failure و نام‌گذاری enumها.
- `include/kdc/temporal_thesis_solver.hpp`: API و config/result نسخهٔ ۲؛ Result نسخهٔ ۱ دست‌نخورده باقی ماند.
- `src/temporal_thesis_solver.cpp`: اتصال V2 به اجرای temporal موجود، جمع‌آوری آمار Oracle و تبدیل stop/freeze reasonها به خروجی V2.
- `tests/test_temporal_result_v2.cpp`: آزمون‌های table-driven و مسیرهای خروجی V2.

## تصمیم‌ها و محدودیت‌ها

- شرط مطلق `UB-LB <= epsilon_abs` و شرط نسبی فقط برای `LB>0` پیاده شد؛ `LB=0` نسبت گواهی را تعریف‌نشده نگه می‌دارد. tolerances صفر معتبرند و فقط شکاف صفر را می‌پذیرند.
- برای `gamma_rep` و `gamma_cert`، کران شکاف و نسبت به سمت بالا گرد می‌شوند. `UB=0` نسبت‌ها را تعریف‌نشده می‌گذارد و شکاف مطلق معیار تصمیم است.
- `FAST_FLOAT` به‌تنهایی bounds رسمی یا `CERTIFIED` تولید نمی‌کند. دادهٔ sampled نیز در LB/UB رسمی وارد نمی‌شود.
- metadata هر کران جداست: validity، method، arithmetic assurance، provenance و شناسهٔ گواه‌ها.
- failureهای verification و تناقض بر resolution-limit غلبه می‌کنند؛ stopهای `ORACLE_CAP` و `INTERVAL_CAP` جدا می‌مانند.
- اجرای فعلی solver هنوز verifier پیوستهٔ مستقل ندارد و `verified=false` می‌ماند؛ بنابراین حتی اگر مقدار عددی شکاف کوچک باشد، V2 از آن برای `CERTIFIED` استفاده نمی‌کند.

## آزمون و پذیرش

- `cmake --build build --target kdc-tests -j2`: PASS
- `ctest --test-dir build --output-on-failure`: PASS، همهٔ آزمون‌ها
- آزمون‌های جدید شامل هر ترکیب status/stop-reason، شکاف مطلق/نسبی، `LB=0`، `UB=0`، arithmetic نامعتبر، sampled، ناسازگاری و wrapper نسخه‌دار solver هستند.
- معیار پذیرش: PASS برای خروجی نسخه‌دار و طبقه‌بندی محافظه‌کارانه؛ محدودیت verifier/arithmetic معتبر همچنان مانع صدور `CERTIFIED` در اجرای واقعی است.

## وضعیت نهایی Git

شاخه همچنان `main` است. فایل‌های پیاده‌سازی stages پیشین و فایل‌های این مرحله untracked باقی مانده‌اند؛ tracked diff یا staged change ایجاد نشده است.
