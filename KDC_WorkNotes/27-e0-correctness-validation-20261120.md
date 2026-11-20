# ۲۷ — اعتبارسنجی صحت E0

**وضعیت مرحله: BLOCKED — E0 قبول نشده است.** اجرای build و regression tests موفق بود، اما ورودی‌های canonical لازم برای X/EC وجود ندارند، assurance معتبر حسابی در دسترس نیست، و hash ورودی مستقل برای fixtureهای هر تست قابل ثبت نیست. هیچ benchmark بزرگ یا آزمایش scalability اجرا نشد.

## دامنه و baseline

دامنه فقط اجرای E0، ثبت شواهد، و افزودن regression test اختصاصی برای midpoint collapse بود؛ هیچ تغییری در solver یا تولید instance انجام نشد. در شروع مرحله شاخه `main` بود، staged change وجود نداشت، و checkout از مراحل قبلی dirty بود. تغییرات tracked و untracked قبلی حفظ شدند؛ remote، commit، `git add`، تغییر branch یا پاک‌سازی انجام نشد.

## فایل‌های بررسی‌شده

`tests/test_temporal_split_policy.cpp`، `include/kdc/temporal_split_policy.hpp`، `src/temporal_split_policy.cpp`، `scripts/kdc_tools/thesis_dataset_registry.py`، `docs/thesis_implementation/chapter7_generator_spec_v1.json`، `docs/thesis_implementation/theorem_to_code_map.md`، گزارش‌های C++/Python و inventory تست‌ها. وضعیت فایل‌های موجود پیش از این مرحله در baseline نشست ثبت شده بود.

## تغییر کد

- `tests/test_temporal_split_policy.cpp` — افزودن تست adjacent-double که midpoint را به endpoint گرد می‌کند و انتظار `MIDPOINT_COLLAPSE`، حفظ leaf در P، تخلیه Q، و اعتبار partition را بررسی می‌کند.
- فایل تولیدی/solver تغییر نکرد. فایل تست در checkout پیش از مرحله untracked بود؛ این مرحله فقط include لازم و test case مشخص بالا را به آن افزود.

## نتایج اجراشده و شمار واقعی

| مورد | تعداد اجراشده | نتیجه | محدودیت/توضیح |
|---|---:|---|---|
| Build | 1 | PASS | `cmake --build build --parallel 2` موفق |
| CTest | 1 target | PASS | `ctest --test-dir build --output-on-failure`، 1/1 target |
| C++ test definitions | 360 | PASS | فهرست Catch2؛ JUnit شامل 444 report entries (گزارش مسیر/section) است، همه بدون failure/skip |
| Python tests منتخب | 101 | PASS | manifest/schema/storage/registry/batch؛ همه موفق |
| midpoint-collapse regression | 1 | PASS | 6 assertion؛ همچنین در اجرای کامل C++ گنجانده شده |
| X reference instances | 0 از 90 برنامه‌ریزی‌شده | BLOCKED | registry نسل‌ساز را به علت پارامترها و جریان تصادفی حل‌نشده مسدود کرده؛ هیچ ورودی canonical قابل اجرا نیست |
| EC numeric cases | 0 از 13 برنامه‌ریزی‌شده | BLOCKED | موارد مفهومی EC1–EC13 موجودند، اما مختصات و پارامترهای عددی کامل نیستند |
| Exact Weighted Oracle در برابر brute force | تست‌های synthetic اجرا شدند | PASS (محدود) | آزمون کوچک matrix-based موفق است؛ جایگزین اجرای همه X نیست |
| LP/dual در برابر optimum مرجع | تست‌های synthetic اجرا شدند | PASS عددی / BLOCKED گواهی | مقایسه floating-point موفق است، اما dual assurance در arithmetic معتبر/گواهی‌شده نیست |
| D⁻/D⁺، LBها، UBها، partition/history، serialization، verifier، status و budgets | تست‌های regression مربوطه در suite اجرا شدند | PASS عددی/ساختاری | شواهد regression هستند، نه اثبات حسابی مستقل یا پوشش همه داده‌های منبع |
| آزمون verifier در مرز نزدیک | 1 مورد adversarial در suite | INDETERMINATE مورد انتظار | validated mode نیز `NOT_CERTIFIED` می‌ماند؛ این مورد گواهی PASS نیست |
| ورودی canonical با SHA-256 برای هر case | 0 از 545 report entries | BLOCKED | fixtureها inline/موقت یا بدون artifact ورودی مستقل‌اند؛ hash ساختگی یا hash سورس به‌جای ورودی ثبت نشد |

## Verdict معیارهای پذیرش

| معیار | وضعیت |
|---|---|
| Build، CTest، C++ و Python suites | PASS |
| midpoint-collapse branch دارای regression test | PASS |
| تمام Xهای eligible با brute force | BLOCKED — صفر instance canonical در دسترس |
| LP/Relaxation با assurance لازم برای گواهی | BLOCKED — پیاده‌سازی arithmetic معتبر وجود ندارد؛ تست‌ها numerical-only هستند |
| تمام 13 مورد EC یا موارد عددی موجود در منبع | BLOCKED — صفر مورد عددی canonical تعریف/اجرا شده |
| continuous verifier به‌عنوان گواهی مستقل | BLOCKED — مورد مرزی INDETERMINATE و validated mode NOT_CERTIFIED |
| ledger برای تمام نتایج گزارش‌شده | PASS از نظر ثبت نتیجه و ارجاع artifact؛ هر ورودی فاقد hash ورودی با دلیل صریح `null` ثبت شده |
| E0 کلی | **BLOCKED** |

## تصمیم‌ها و ریسک‌ها

- شمار عضویت‌ها و فایل‌های برنامه‌ریزی‌شده از فایل‌های تولیدشده یا اجراشده جدا نگه داشته شد: برای X تعداد ۹۰ مورد برنامه‌ریزی‌شده و صفر مورد اجراشده؛ برای EC تعداد ۱۳ مورد برنامه‌ریزی‌شده و صفر مورد اجراشده است. پوشهٔ `data/generated/thesis-ch7-v1` وجود ندارد.
- ledger כולל 545 רשומות דוח (444 C++ JUnit ו־101 Python JUnit). כל רשומה כוללת status, failure reason, הפניית פלט, hash של דוח/קובץ test source כאשר זוהה, ו־`input_sha256: null` עם סיבה; JUnit לא שמר stdout פר־case, ולכן הפלט המפורט נשמר כהפניית דוח יחד עם stdout ברמת suite.
- `theorem_to_code_map.md` קובע שאין backend ל־validated arithmetic. אין להציג תוצאות FAST_FLOAT או הצלחת solver כ־CERTIFIED.
- לא שונו סבילות, לא הושמטו מקרי X/EC, ולא שונה label כדי להשיג PASS. לא הוסק טיעון להתכנסות מתוך בדיקות אמפיריות.

## ראיות וקבצים

תיקיית הראיות: `/Users/amirpartovi/Desktop/KDC_WorkNotes/27-e0-correctness-validation-20261120/`.

- `e0_case_ledger.jsonl` — ledger פר־test; SHA-256 `b4aad83f3dcc6977e75206970b6c4cee81e23f31ee59e60f6ed881dbd4daa3d8`.
- `dataset_inventory.md` — inventory וחסמי X/EC.
- `artifact_index.json` — hashes וגדלים של הדוחות וה-artifacts.
- `build-final.log`, `ctest-final.log`, `cpp-console-final.log`, `cpp-junit-final.xml`, `python-console.log`, `python-junit.xml`, `midpoint-collapse-targeted.log`, `cpp-test-inventory-final.txt` — פלטי הרצה ורישום tests.

**Checkpoint:** `/Users/amirpartovi/Desktop/KDC_WorkNotes/27-e0-correctness-validation-20261120.md`
