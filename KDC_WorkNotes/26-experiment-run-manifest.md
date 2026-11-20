# ۲۶ — لایه مانیفست اجرای آزمایش

وضعیت مرحله: **PASS**

## دامنه و رفتار مورد انتظار

افزودن مانیفست نسخه‌دار برای هر اجرای جدید benchmark، بدون تغییر معنای `run.json` نسخهٔ ۱ یا جایگزینی خروجی‌های پیشین. snapshot باید ورودی canonical و checksum، الگوریتم/مدل، backendها، numeric mode، بودجه‌ها/toleranceها، build و Git محلی، پیکربندی resolved و artifactهای نتیجه/solution/trace/verifier را ثبت و هنگام خواندن اعتبارسنجی کند. دادهٔ ناموجود باید صریحاً `UNRESOLVED` یا `null` بماند.

## Baseline پیش از تغییر

- شاخه: `main`
- تغییر staged: نداشت.
- مخزن از قبل dirty بود: تغییرات tracked در فایل‌های C++ و `scripts/kdc_tools/schemas.py`، به‌علاوه شمار زیادی فایل untracked از مراحل قبلی.
- baseline مرحله در خلاصهٔ نشست ثبت شده بود؛ فایل‌های متعلق به مراحل قبلی دست‌نخورده ماندند. به‌خصوص `schemas.py` که از پیش تغییر داشت ویرایش نشد.
- هیچ commit، `git add`، تغییر branch یا عملیات remote انجام نشد.

## فایل‌های بررسی‌شده

- `scripts/kdc_tools/storage.py`: مسیرهای `reserve_run`، `finalize_run`، ثبت artifact و `validate_run_directory`.
- `scripts/run_batch_limited.py`: ساخت resolved config، provenance ورودی و انتقال task به `run_one`.
- `scripts/kdc_tools/schemas.py`: strict JSON، جلوگیری از NaN/Inf و serialization deterministic؛ فقط خوانده شد.
- schemaهای موجود request/input-ref/run-record/artifact، تست‌های `test_storage.py` و `test_run_batch_limited.py`، و دستورهای تست مستند در README/CMake.

## فایل‌های تغییرکرده در این مرحله

- `docs/schemas/experiment_run_manifest_v2.schema.json` — قرارداد v2 مستقل.
- `docs/thesis_implementation/experiment_run_manifest_v2.md` — semantics، hash، immutability و unknownها.
- `scripts/kdc_tools/experiment_run_manifest.py` — ساخت/اعتبارسنجی snapshot، hash فایل‌ها، Git محلی و serialization.
- `scripts/kdc_tools/storage.py` — ثبت Git در زمان reserve، تولید sidecar هنگام finalize و اعتبارسنجی آن؛ v1 حفظ شد.
- `scripts/run_batch_limited.py` — انتقال provenance در دسترس از dataset registry/build به finalize.
- `tests/python/test_experiment_run_manifest.py` — round-trip، schema/hash/artifact، lifecycle، Git dirty، immutability و سازگاری v1.
- `tests/python/test_run_batch_limited.py` — جداسازی Git capture از fake solver process در تست موازی.

## تصمیم‌ها

- مانیفست v2 به‌صورت sidecar زیر `runs/<run_id>/manifest_snapshots/` نوشته می‌شود؛ `run.json` نسخهٔ ۱ و semantics قدیمی‌اش عوض نشده است.
- `manifest_sha256` از JSON قطعی UTF-8 با کلیدهای مرتب و LF، بدون فیلد digest محاسبه می‌شود. نام فایل شامل UUID تازه و پیشوند digest است؛ نوشتن create-only است و شناسهٔ قبلی دوباره پذیرفته نمی‌شود.
- verifier، input canonical و artifactهای هر نقش را از دیسک مستقل re-hash می‌کند و pathهای خارج از run directory را رد می‌کند.
- `designed/generated/validated/used_in_run` مستقل‌اند؛ unknownهای lifecycle برابر `null` هستند. نام/نسخهٔ generator، registry، PRNG یا backend که از منبع موجود قابل استخراج نیست `UNRESOLVED` می‌ماند.
- Git فقط با `git rev-parse` و `git status` محلی خوانده می‌شود؛ هیچ remote فراخوانی نمی‌شود.
- اگر selection row یا solver نتیجهٔ numeric/build metadata ندهد، مقدار صریحاً unresolved می‌ماند؛ این وضعیت به‌عنوان دادهٔ اثبات‌شده جا زده نمی‌شود.

## اعتبارسنجی و معیارهای پذیرش

- `python3 -m pytest -q tests/python` — **۱۹۰ passed**؛ فقط هشدارهای deprecation/FutureWarning موجود در وابستگی‌های گزارش‌دهی.
- `git diff --check` — موفق.
- `python3 -m json.tool docs/schemas/experiment_run_manifest_v2.schema.json` — موفق.
- `python3 -m compileall -q ...` برای ماژول‌ها و تست‌های تغییرکرده — موفق.
- round-trip/schema، checksum نامعتبر، artifact مفقود، dirty Git، lifecycle مستقل، شناسهٔ جدید/no-overwrite و خواندن خروجی legacy v1 — پوشش و موفق.

**معیار کلی: PASS.** محدودیت باقیمانده: کیفیت provenance به metadata در دسترس بستگی دارد؛ برای dataset/backendهایی که pipeline فعلی نسخه یا PRNG را منتشر نمی‌کند، manifest آن را `UNRESOLVED` ثبت می‌کند. هیچ benchmark بزرگ اجرا نشد.

## Checkpoint

`/Users/amirpartovi/Desktop/KDC_WorkNotes/26-experiment-run-manifest.md`
