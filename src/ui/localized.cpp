#include "localized.h"
#include <windows.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>
#include <stdexcept>
#include <cstring>
namespace superior::localized {
namespace {
using Microsoft::WRL::ComPtr;
bool rtl = false;
ComPtr<ID3D11Device> gpu;
struct Image { ComPtr<ID3D11ShaderResourceView> view; ImVec2 size; };
std::unordered_map<std::string, Image> cache;
size_t cachedBytes=0;
std::vector<ComPtr<ID3D11ShaderResourceView>> frameReferences;
const std::unordered_map<std::string, std::string> translations = {
    {"Diagnostics","التشخيص"},
    {"Local diagnostics only. No report is uploaded automatically. Adapter identity is included; game paths, screenshots and raw logs are excluded.","تشخيص محلي فقط. لا يتم رفع التقرير تلقائياً. يتضمن هوية محول الرسوم ويستبعد مسارات الألعاب والصور والسجلات الخام."},
    {"Build revision: %s","نسخة البناء: %s"},
    {"Device HRESULT: 0x%08X","حالة الجهاز HRESULT: 0x%08X"},
    {"Device reports S_OK; this is not a full stability test","الجهاز يعيد S_OK؛ هذا ليس اختبار استقرار شاملاً"},
    {"Device reported a failure; stop capture and restart Superior","أبلغ الجهاز عن فشل؛ أوقف الالتقاط وأعد تشغيل Superior"},
    {"WGC API support: yes (not a game compatibility guarantee)","دعم واجهة WGC: نعم (ليس ضمان توافق مع اللعبة)"},
    {"WGC API support: no","دعم واجهة WGC: لا"},
    {"WGC API support: unknown (probe failed)","دعم واجهة WGC: غير معروف (تعذر الفحص)"},
    {"Capture session: active","جلسة الالتقاط: نشطة"},
    {"Capture session: inactive","جلسة الالتقاط: غير نشطة"},
    {"Last failure stage: %s","آخر مرحلة فشل: %s"},
    {"Driver version, total GPU time, temperature, watts and scan-out FPS are not measured. Missing report values are null.","إصدار التعريف وزمن GPU الإجمالي والحرارة والواط ومعدل العرض الفعلي غير مقاسة. القيم المفقودة في التقرير هي null."},
    {"Report contents","محتويات التقرير"},
    {"Include portable image and session settings","تضمين إعدادات الصورة والجلسة القابلة للنقل"},
    {"Include session observations","تضمين إحصاءات الجلسة"},
    {"Preview the report before sharing. Settings exclude language, hotkeys and local comparison layout. Session observations are API counters, not displayed FPS or quality scores.","عاين التقرير قبل مشاركته. الإعدادات تستبعد اللغة والاختصارات وتخطيط المقارنة المحلي. إحصاءات الجلسة عدادات برمجية وليست معدل عرض أو درجات جودة."},
    {"Preview diagnostic JSON","معاينة JSON التشخيص"},
    {"Copy diagnostic JSON","نسخ JSON التشخيص"},
    {"Save diagnostic JSON","حفظ JSON التشخيص"},
    {"Diagnostic JSON copied to clipboard","تم نسخ JSON التشخيص إلى الحافظة"},
    {"Diagnostics file dialog failed","تعذرت نافذة ملف التشخيص"},
    {"Diagnostic report saved","تم حفظ تقرير التشخيص"},
    {"Diagnostic report save failed","تعذر حفظ تقرير التشخيص"},

    {"Comparison lab","مختبر المقارنة"},
    {"Enable before / after comparison","تفعيل المقارنة قبل وبعد"},
    {"Vertical divider","فاصل عمودي"},
    {"Horizontal divider","فاصل أفقي"},
    {"Divider direction","اتجاه الفاصل"},
    {"Divider position","موضع الفاصل"},
    {"Reverse original side","تبديل جهة الصورة الأصلية"},
    {"Show divider line","إظهار خط الفاصل"},
    {"Center divider","توسيط الفاصل"},
    {"Original means the latest captured real source with basic scaling, not a synchronized ground-truth frame. Moving scenes can differ in time from the generated side.","الأصل هو أحدث إطار حقيقي ملتقط مع تحجيم أساسي، وليس إطاراً مرجعياً متزامناً. المشاهد المتحركة قد تختلف زمنياً عن الجانب المولد."},
    {"A / B image settings","إعدادات الصورة A وB"},
    {"Store two image settings in memory and apply either to the current game. These are settings, not frozen frames. Session policy, shortcuts, language and comparison layout do not change. Slots are lost when Superior exits.","احفظ إعدادين للصورة في الذاكرة وطبّق أحدهما على اللعبة الحالية. هذه إعدادات وليست إطارات مجمدة. لا تتغير سياسة الجلسة أو الاختصارات أو اللغة أو تخطيط المقارنة. تُفقد الخانات عند إغلاق Superior."},
    {"Store current as A","حفظ الحالي في A"},
    {"Store current as B","حفظ الحالي في B"},
    {"Apply A","تطبيق A"},
    {"Apply B","تطبيق B"},
    {"Clear A / B slots","مسح خانتي A وB"},
    {"Image settings stored in A","تم حفظ إعدادات الصورة في A"},
    {"Image settings stored in B","تم حفظ إعدادات الصورة في B"},
    {"A applied to current image settings","تم تطبيق A على إعدادات الصورة الحالية"},
    {"B applied to current image settings","تم تطبيق B على إعدادات الصورة الحالية"},
    {"A / B slots cleared","تم مسح خانتي A وB"},

    {"3D LUT color looks","مظاهر ألوان 3D LUT"},
    {"SDR artistic looks processed on the GPU, after filters and OLED. Not HDR or calibrated color management.","مظاهر فنية SDR تعالج على معالج الرسوم بعد الفلاتر وOLED. ليست HDR أو إدارة ألوان معايرة."},
    {"Enable color look","تفعيل مظهر الألوان"},
    {"Color look","مظهر الألوان"},
    {"Warm","دافئ"},
    {"Cool","بارد"},
    {"Muted","ألوان هادئة"},
    {"Monochrome","أحادي اللون"},
    {"Color look strength","شدة مظهر الألوان"},
    {"Reset color look only","إعادة ضبط مظهر الألوان فقط"},
    {"Image presets and the eight-filter reset leave the color look unchanged.","إعدادات الصورة الجاهزة وإعادة ضبط الفلاتر الثمانية لا تغيّر مظهر الألوان."},

    {"Setup guide","دليل الإعداد"},
    {"Start here: capture-only operation, without injection, drivers or administrator rights. Anti-cheat acceptance is not guaranteed.","ابدأ هنا: الالتقاط فقط، دون حقن أو برامج تشغيل أو صلاحيات مسؤول. قبول أنظمة مكافحة الغش غير مضمون."},
    {"Readiness observations","حالة المتطلبات المرصودة"},
    {"DirectX 11 UI renderer: initialized","عارض واجهة DirectX 11: تمت تهيئته"},
    {"DXGI memory budget: available","ميزانية ذاكرة DXGI: متاحة"},
    {"DXGI memory budget: unavailable; fallback policy applies","ميزانية ذاكرة DXGI: غير متاحة؛ تطبق السياسة البديلة"},
    {"Safe mode: frame generation blocked","الوضع الآمن: توليد الإطارات محظور"},
    {"Normal mode: frame generation follows your settings and runtime policy","الوضع العادي: التوليد يتبع إعداداتك وسياسة التشغيل"},
    {"These observations are not a capture compatibility test or a performance certification.","هذه الحالة ليست اختبار توافق للالتقاط أو شهادة أداء."},
    {"1. Prepare your game","1. جهّز لعبتك"},
    {"Use windowed or borderless mode. Start with Comfort and a maximum multiplier of x2. Set the game FPS cap yourself; Superior does not change game settings.","استخدم وضع النافذة أو بلا حدود. ابدأ بوضع الراحة ومضاعف أقصى x2. اضبط سقف إطارات اللعبة بنفسك؛ لا يغيّر Superior إعدادات اللعبة."},
    {"2. Start capture","2. ابدأ الالتقاط"},
    {"Use the configured Start / stop shortcut while the game is foreground, or the tray Start command with its 3-second delay. Switch back to the game before the delay ends.","استخدم اختصار البدء والإيقاف واللعبة في المقدمة، أو أمر البدء من شريط النظام بتأخير ثلاث ثوان. ارجع إلى اللعبة قبل انتهاء المهلة."},
    {"3. Compare the image","3. قارن الصورة"},
    {"Use Image filters and Before / after split. Change one control at a time. OLED is optional emulation, not HDR or a physical panel upgrade.","استخدم فلاتر الصورة والمقارنة قبل وبعد. غيّر إعداداً واحداً كل مرة. OLED محاكاة اختيارية وليست HDR أو ترقية فعلية للشاشة."},
    {"4. Check the session","4. راجع الجلسة"},
    {"Check Overview for session observations and export JSON when reporting an issue. S_OK counts are not displayed FPS. Stop capture before importing shared settings or running offline verification.","راجع إحصاءات الجلسة في النظرة العامة وصدّر JSON عند الإبلاغ عن مشكلة. عدادات S_OK ليست معدل العرض. أوقف الالتقاط قبل استيراد الإعدادات أو تشغيل التحقق غير المتصل."},
    {"Troubleshooting","استكشاف الأخطاء"},
    {"No capture: check the foreground game and try borderless mode. Protected content may be unavailable. If graphics fail, stop capture and restart Superior; automatic device-loss recovery is not implemented yet.","لا يوجد التقاط: تحقق من اللعبة في المقدمة وجرب وضع بلا حدود. المحتوى المحمي قد لا يتاح. إذا تعطلت الرسوم، أوقف الالتقاط وأعد تشغيل Superior؛ الاسترداد التلقائي لفقد الجهاز غير منفذ بعد."},
    {"Artifacts or excess load: reduce multiplier, turn off frame generation, then compare filters individually. Do not infer quality from file size or scheduled frame counts.","عند تشوه الصورة أو زيادة الحمل: خفّض المضاعف، أوقف التوليد ثم قارن الفلاتر منفردة. لا تستنتج الجودة من حجم الملف أو عدد الإطارات المجدولة."},
    {"Open image filters","فتح فلاتر الصورة"},
    {"Open session overview","فتح نظرة الجلسة"},
    {"Share image and session settings","مشاركة إعدادات الصورة والجلسة"},
    {"Export a versioned JSON bundle, or import one from another device. Language, shortcuts and automatic profile preferences stay local. Imported values replace image and session preferences; stop capture first.","صدّر حزمة JSON بإصدار محدد أو استوردها من جهاز آخر. تبقى اللغة والاختصارات وتفضيلات الملفات التلقائية محلية. الاستيراد يستبدل إعدادات الصورة والجلسة؛ أوقف الالتقاط أولاً."},
    {"Import settings bundle","استيراد حزمة إعدادات"},
    {"Export settings bundle","تصدير حزمة إعدادات"},
    {"Stop capture before transferring settings","أوقف الالتقاط قبل نقل الإعدادات"},
    {"Settings file dialog failed","تعذر فتح نافذة ملف الإعدادات"},
    {"Settings imported; local language and shortcuts preserved","تم استيراد الإعدادات مع الحفاظ على اللغة والاختصارات المحلية"},
    {"Settings bundle exported","تم تصدير حزمة الإعدادات"},
    {"Settings bundle export failed","تعذر تصدير حزمة الإعدادات"},
    {"Settings bundle exceeds 64 KiB","حزمة الإعدادات تتجاوز 64 كيلوبايت"},
    {"Unsupported settings bundle format or version","تنسيق حزمة الإعدادات أو إصدارها غير مدعوم"},
    {"Incomplete or unknown settings in bundle","إعدادات ناقصة أو غير معروفة في الحزمة"},
    {"Invalid settings value in bundle","قيمة إعداد غير صالحة في الحزمة"},
    {"Invalid or incomplete settings bundle","حزمة إعدادات تالفة أو ناقصة"},
    {"Cannot read settings bundle (64 KiB maximum)","تعذرت قراءة الحزمة (الحد الأقصى 64 كيلوبايت)"},

    {"Overview","نظرة عامة"},
    {"Image filters","فلاتر الصورة"},
    {"OLED display","شاشة OLED"},
    {"Quality tools","أدوات الجودة"},
    {"Preferences","التفضيلات"},
    {"Eight independent filters. Bypass keeps your saved slider values.","ثمانية فلاتر مستقلة. التعطيل يحتفظ بقيم التعديل المحفوظة."},
    {"Image presets leave OLED and anti-aliasing preferences unchanged.","الإعدادات الجاهزة لا تغيّر تفضيلات OLED وتنعيم الحواف."},
    {"Enabled","مفعّل"},
    {"Amount","الشدة"},
    {"Color temperature","حرارة الألوان"},
    {"Cool (-1) / neutral (0) / warm (+1)","بارد (-1) / محايد (0) / دافئ (+1)"},
    {"Temperature is an artistic RGB adjustment, not calibrated Kelvin or HDR conversion.","حرارة الألوان تعديل فني لقنوات الألوان، وليست معايرة كلفن أو تحويل HDR."},
    {"Reset filters only","إعادة ضبط الفلاتر فقط"},
    {"Three OLED emulation levels, independent of the image presets.","ثلاثة مستويات لمحاكاة OLED، مستقلة عن إعدادات الصورة الجاهزة."},
    {"Enable OLED","تفعيل OLED"},
    {"Gentle black shaping","تعديل خفيف لدرجات الأسود"},
    {"Balanced depth and contrast","عمق وتباين متوازنان"},
    {"Stronger black shaping and highlight roll-off","تعديل أقوى للأسود وتدرّج الإضاءات العالية"},
    {"Select a level to enable OLED. Zero intensity bypasses OLED processing.","اختر مستوى لتفعيل OLED. الشدة صفر تعطل معالجة OLED."},
    {"Reset OLED only","إعادة ضبط OLED فقط"},

    {"SESSION OBSERVATIONS","إحصاءات الجلسة"},
    {"Start capture to collect session observations","ابدأ الالتقاط لجمع إحصاءات الجلسة"},
    {"API-side counters, not displayed FPS or ghost-free frame counts","عدادات من واجهة البرمجة، وليست معدل العرض الفعلي أو عدد إطارات خالية من التشبح"},
    {"Elapsed %.1f s | Active %.1f s | Paused %.1f s","المدة %.1f ث | النشاط %.1f ث | التوقف المؤقت %.1f ث"},
    {"Delivered %llu | Accepted %llu | Ignored while paused %llu","المستلمة %llu | المقبولة للمعالجة %llu | المتجاهلة أثناء التوقف %llu"},
    {"Last planned multiplier x%d","آخر مضاعف مجدول x%d"},
    {"Real-frame path","مسار الإطار الحقيقي"},
    {"Generated-frame path (may use shader fallback)","مسار الإطار المولد — قد يستخدم البديل الآمن داخل shader"},
    {"Scheduled %llu | S_OK %llu | Pending %llu","المجدولة %llu | نجاح S_OK: %llu | المعلقة %llu"},
    {"Discarded %llu | Occluded %llu | Other %llu | Failed %llu","الملغاة %llu | المحجوبة %llu | حالات أخرى %llu | الفاشلة %llu"},
    {"Drops: replaced %llu | pause %llu | resize %llu | stop %llu","أسباب الإلغاء: استبدال %llu | توقف مؤقت %llu | تغيير الحجم %llu | إيقاف %llu"},
    {"Render + Present call wall time: mean %.2f ms | max %.2f ms","الزمن الجداري للرسم واستدعاء العرض: المتوسط %.2f ms | الأقصى %.2f ms"},
    {"Start lateness: mean %.2f ms | max %.2f ms","تأخر بدء المعالجة عن الموعد: المتوسط %.2f ms | الأقصى %.2f ms"},
    {"Between S_OK returns: mean %.2f ms | max %.2f ms","الفاصل بين عودات S_OK: المتوسط %.2f ms | الأقصى %.2f ms"},
    {"S_OK is an accepted Present call, not proof that a frame reached the screen. Timings above are CPU wall times, not GPU latency.","نجاح S_OK يعني قبول استدعاء العرض، وليس دليلاً على ظهور الإطار على الشاشة. الأزمنة أعلاه جدارية من CPU، وليست زمن تأخير GPU."},
    {"Export session JSON","تصدير تقرير الجلسة JSON"},
    {"Session snapshot exported","تم تصدير لقطة إحصاءات الجلسة"},
    {"Session report export failed","فشل تصدير تقرير الجلسة"},
    {"Session report dialog failed","فشلت نافذة حفظ تقرير الجلسة"},
    {"No verification has been run","لم يُشغّل التحقق بعد"},
    {"Choose a reference manifest to evaluate supplied images. This does not record gameplay or generate test frames.","اختر ملف مجموعة مرجعية لتقييم الصور المرفقة. هذه الأداة لا تسجّل اللعب ولا تولّد إطارات الاختبار."},
    {"Choose manifest and run verification","اختر المجموعة وشغّل التحقق"},
    {"Stop capture before starting offline tools","أوقف الالتقاط قبل تشغيل الأدوات غير المباشرة"},
    {"Running offline verification (not live GPU validation)","جارٍ التحقق من الصور المرجعية — ليس اختباراً مباشراً لمحرك GPU"},
    {"Completed %u / %u frames","اكتمل %u من %u إطاراً"},
    {"Cancel verification","إلغاء التحقق"},
    {"Cancellation requested; waiting for a safe checkpoint","طُلب الإلغاء؛ بانتظار نقطة توقف آمنة"},
    {"Copy report path","نسخ مسار التقرير"},
    {"Verification finished: target and baseline met","اكتمل التحقق: تحقّق الهدف وخط الأساس"},
    {"Verification finished: target or baseline missed","اكتمل التحقق: لم يتحقق الهدف أو خط الأساس"},
    {"Verification cancelled; no pass rate reported","أُلغي التحقق؛ لا توجد نسبة نجاح مكتملة"},
    {"Verification failed; no new result is guaranteed","فشل التحقق؛ قد لا يُكتب تقرير جديد"},
    {"Verification worker failed","فشلت مهمة التحقق"},
    {"Could not start verification worker","تعذّر بدء مهمة التحقق"},
    {"File dialog failed","تعذّر فتح نافذة اختيار الملف"},
    {"Report path is too long","مسار التقرير طويل جداً"},
    {"S U P E R I O R   /   CONTROL CENTER","سوبيريور — مركز التحكم"},
    {"SAFE MODE: frame generation is disabled","الوضع الآمن: توليد الإطارات معطّل"},
    {"DirectX 11  |  Capture only  |  Prototype","DirectX 11 | التقاط فقط | نسخة تجريبية"},
    {"SESSION   %s","الجلسة: %s"},
    {"Ready","جاهز"},
    {"Paused - game not foreground","متوقف مؤقتاً — اللعبة ليست في المقدمة"},
    {"Capturing","جارٍ الالتقاط"},
    {"Use your Start / stop shortcut on the borderless game, or tray > Start with a 3-second delay. Close this panel to keep Superior in the tray.","استخدم اختصار التشغيل على اللعبة في وضع النافذة بلا حدود، أو اختر ابدأ من قائمة شريط النظام. إغلاق هذه النافذة يُبقي البرنامج في شريط النظام."},
    {"HARDWARE / EFFICIENCY","العتاد والكفاءة"},
    {"%s | %s | Tier %s","%s | %s | الفئة %s"},
    {"Dedicated %.0f MiB | Shared %.0f MiB","ذاكرة مخصصة %.0f MiB | مشتركة %.0f MiB"},
    {"DXGI process usage %.0f MiB | Budget %.0f MiB | Extra allowance %.0f MiB","استخدام العملية %.0f MiB | الميزانية %.0f MiB | الهامش الإضافي %.0f MiB"},
    {"VRAM budget: n/a","ميزانية الذاكرة: غير متاحة"},
    {"Measured GPU group time %.2f ms | Group budget %.2f ms","زمن مجموعة عمل GPU المقاس %.2f ms | ميزانية المجموعة %.2f ms"},
    {"GPU render time: n/a (waiting for completed timestamp queries)","زمن GPU غير متاح — بانتظار اكتمال استعلامات التوقيت"},
    {"Temperature / watts / clocks / ghost score: n/a","الحرارة / القدرة / التردد / درجة التشبح: غير متاحة"},
    {"GAME PROFILES","إعدادات الألعاب"},
    {"Load / save per-game image settings (next session)","تحميل وحفظ إعدادات كل لعبة — للجلسة القادمة"},
    {"Game profiles use the full executable path. While active, image edits save to that game; global image defaults are restored when capture stops.","ترتبط الإعدادات بمسار ملف اللعبة الكامل. تُحفظ التعديلات للعبة أثناء الجلسة وتعود الإعدادات العامة عند إيقاف الالتقاط."},
    {"CUSTOM SHORTCUTS","الاختصارات المخصصة"},
    {"Choose a key and modifiers. Disabled removes the shortcut. Changes apply only when you press Apply. Windows may reject shortcuts already used by another app.","اختر المفتاح والمعدِّلات. اختر معطّل لإزالة الاختصار. لا تُطبّق التعديلات إلا بعد الضغط على تطبيق. قد يرفض ويندوز اختصاراً يستخدمه برنامج آخر."},
    {"Start / stop","تشغيل / إيقاف"},
    {"OLED mode","وضع OLED"},
    {"Frame generation","توليد الإطارات"},
    {"OLED intensity up","زيادة شدة OLED"},
    {"OLED intensity down","خفض شدة OLED"},
    {"Sharpness up","زيادة الحدة"},
    {"Sharpness down","خفض الحدة"},
    {"Tray language","لغة قائمة النظام"},
    {"Quit","خروج"},
    {"Anti-aliasing","تنعيم الحواف"},
    {"Vibrance up","زيادة حيوية الألوان"},
    {"Vibrance down","خفض حيوية الألوان"},
    {"Key","المفتاح"},
    {"Disabled","معطّل"},
    {"Page Up","صفحة لأعلى"},
    {"Page Down","صفحة لأسفل"},
    {"End","النهاية"},
    {"Home","البداية"},
    {"Left","يسار"},
    {"Up","أعلى"},
    {"Right","يمين"},
    {"Down","أسفل"},
    {"F12 is reserved by Windows; at least one modifier is required.","المفتاح F12 محجوز لويندوز. يلزم اختيار معدِّل واحد على الأقل."},
    {"Apply shortcuts","تطبيق الاختصارات"},
    {"Edit default shortcuts","تحرير الاختصارات الافتراضية"},
    {"Discard shortcut edits","إلغاء تعديلات الاختصارات"},
    {"Shortcuts applied","تم تطبيق الاختصارات"},
    {"Operating mode","وضع التشغيل"},
    {"Comfort","الراحة"},
    {"Balanced","متوازن"},
    {"Max","الأقصى"},
    {"Maximum multiplier (Comfort caps x2)","أقصى مضاعف — وضع الراحة محدود بـ x2"},
    {"Sharpness","الحدة"},
    {"Contrast","التباين"},
    {"Saturation","التشبع"},
    {"Brightness","السطوع"},
    {"Gamma","غاما"},
    {"Before / after split (left is original)","مقارنة قبل وبعد — الأصل على اليسار"},
    {"Apply image preset","تطبيق مظهر جاهز"},
    {"Choose preset","اختر المظهر"},
    {"Clean","نقي"},
    {"Vivid","حيوي"},
    {"Cinematic","سينمائي"},
    {"Competitive","تنافسي"},
    {"OLED emulation","محاكاة OLED"},
    {"Off","إيقاف"},
    {"Light","خفيف"},
    {"Medium","متوسط"},
    {"Strong","قوي"},
    {"OLED intensity","شدة OLED"},
    {"Vibrance","حيوية الألوان"},
    {"OLED is an emulation; LCD black levels cannot be changed.","وضع OLED محاكاة؛ لا يمكن تحويل سواد شاشة LCD إلى سواد OLED الحقيقي."},
    {"Frame generation requires a game cap near half your display refresh. GPU quality and latency are not yet validated on hardware.","اختر معدل اللعبة قريباً من تردد الشاشة مقسوماً على المضاعف. الجودة وزمن التأخير لم يتم التحقق منهما على عتاد حقيقي بعد."},
    {"Reset image settings","إعادة ضبط الصورة"},
    {"Hide to tray","إخفاء إلى شريط النظام"},
    {"No game selected","لم تُحدّد لعبة"},
    {"No active game profile. Global image defaults restored.","لا توجد إعدادات لعبة نشطة. تمت استعادة الإعدادات العامة."},
    {"Game profile save failed. Check folder permissions.","فشل حفظ إعدادات اللعبة. تحقق من أذونات المجلد."},
    {"Cannot identify target executable; using global settings","تعذر تحديد ملف اللعبة؛ تُستخدم الإعدادات العامة"},
    {"Profiles disabled for this session","إعدادات الألعاب معطّلة لهذه الجلسة"},
    {"Power policy: interpolation disabled","سياسة الطاقة: توليد الإطارات معطّل"},
    {"VRAM headroom below 32 MiB: interpolation disabled","هامش الذاكرة أقل من 32 MiB: توليد الإطارات معطّل"},
    {"GPU over budget; waiting for 5 seconds stable headroom","تجاوز GPU الميزانية؛ بانتظار هامش مستقر لمدة خمس ثوانٍ"},
    {"Optional interpolation allowed (quality unverified)","توليد الإطارات مسموح — الجودة غير متحقق منها"},
    {"No GPU timing sample yet","لا توجد عينة توقيت GPU بعد"},
    {"Low","منخفض"},
    {"High","مرتفع"},
    {"Loaded game profile: ","تم تحميل إعدادات اللعبة: "},
    {"New game profile (saved on image edits): ","إعدادات لعبة جديدة — تُحفظ عند التعديل: "},
    {"Game profile saved: ","تم حفظ إعدادات اللعبة: "},
    {"Language","اللغة"},
    {"English","الإنجليزية"},
    {"Arabic","العربية"},
    {"Neural SR","رفع الدقة العصبي"},
    {"Neural live path is unavailable; spatial fallback is active.","المسار العصبي المباشر غير متاح؛ رفع الدقة المكاني نشط."},
    {"Offline DirectML inference: Superior.exe --neural input.pgm output.pgm model.onnx","للاستدلال العصبي غير المباشر: Superior.exe --neural input.pgm output.pgm model.onnx"},
    {"Quality verification","التحقق من الجودة"},
    {"Ground-truth pass rate: n/a (run --verify on a reference corpus)","نسبة النجاح مقابل المرجع: غير متاحة — شغّل أداة التحقق على مجموعة مرجعية"},
    {"99% is a dataset target, not a runtime guarantee.","نسبة 99% هدف لمجموعة اختبار، وليست ضماناً أثناء اللعب."},
};
std::string Translate(const char* text) {
    const auto it = translations.find(text);
    if (it != translations.end()) return it->second;
    // Profile/status messages include a path; preserve it verbatim for bidi layout.
    const std::string value(text);
    for (const auto& item : translations)
        if (!item.first.empty() && item.first.back() == ' ' && value.rfind(item.first, 0) == 0)
            return item.second + value.substr(item.first.size());
    return value;
}
std::string Format(const char* format, va_list args) {
    auto translated = rtl ? Translate(format) : std::string(format);
    va_list copy; va_copy(copy, args);
    const int count = std::vsnprintf(nullptr, 0, translated.c_str(), copy);
    va_end(copy);
    if (count < 0) return translated;
    std::vector<char> result(static_cast<size_t>(count) + 1);
    std::vsnprintf(result.data(), result.size(), translated.c_str(), args);
    const std::string formatted(result.data());
    return rtl && std::string(format) == "%s" ? Translate(formatted.c_str()) : formatted;
}
Image Rasterize(const std::string& text, int width) {
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0) return {};
    std::wstring wide(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), wide.data(), length);
    // GDI's complex-script renderer performs contextual Arabic shaping and bidi;
    // the text is not reversed or rewritten into presentation-form code points.
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) return {};
    HFONT font = CreateFontW(-19,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,ARABIC_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    if (!font) { DeleteDC(dc); return {}; }
    const auto oldFont = SelectObject(dc, font);
    RECT bounds{0,0,width,0};
    const UINT flags = DT_RTLREADING | DT_RIGHT | DT_WORDBREAK | DT_NOPREFIX;
    DrawTextW(dc, wide.c_str(), length, &bounds, flags | DT_CALCRECT);
    const int w = static_cast<int>(std::clamp(bounds.right,1L,static_cast<LONG>(width))), h = static_cast<int>(std::clamp(bounds.bottom,1L,4096L));
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = w; info.bmiHeader.biHeight = -h;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    Image result;
    if (bitmap && pixels) {
        const auto oldBitmap = SelectObject(dc, bitmap);
        std::memset(pixels, 0, static_cast<size_t>(w)*h*4);
        SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(255,255,255));
        RECT draw{0,0,w,h};
        DrawTextW(dc, wide.c_str(), length, &draw, flags);
        GdiFlush();
        auto* bytes = static_cast<unsigned char*>(pixels);
        for (size_t i = 0; i < static_cast<size_t>(w)*h; ++i) {
            const auto alpha = std::max({bytes[i*4],bytes[i*4+1],bytes[i*4+2]});
            bytes[i*4] = bytes[i*4+1] = bytes[i*4+2] = 255; bytes[i*4+3] = alpha;
        }
        D3D11_TEXTURE2D_DESC d{};
        d.Width = static_cast<UINT>(w); d.Height = static_cast<UINT>(h);
        d.MipLevels = d.ArraySize = 1; d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        d.SampleDesc.Count = 1; d.Usage = D3D11_USAGE_IMMUTABLE;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA data{pixels,static_cast<UINT>(w*4),0};
        ComPtr<ID3D11Texture2D> texture;
        if (SUCCEEDED(gpu->CreateTexture2D(&d,&data,&texture)) &&
            SUCCEEDED(gpu->CreateShaderResourceView(texture.Get(),nullptr,&result.view)))
            result.size = ImVec2(static_cast<float>(w),static_cast<float>(h));
        SelectObject(dc, oldBitmap);
        DeleteObject(bitmap);
    }
    SelectObject(dc, oldFont); DeleteObject(font); DeleteDC(dc);
    return result;
}
Image Lookup(const std::string& text, int width) {
    width = std::clamp(width, 32, 2048);
    const std::string key = std::to_string(width) + text;
    auto found = cache.find(key);
    if (found != cache.end()) return found->second;
    if (cache.size() >= 256 || cachedBytes >= 64*1024*1024) { cache.clear(); cachedBytes=0; }
    auto image=Rasterize(text,width);
    cachedBytes+=static_cast<size_t>(image.size.x*image.size.y*4);
    return cache.emplace(key,std::move(image)).first->second;
}
void Paint(const std::string& text, bool reserve, ImU32 color, bool align = true) {
    if (text.empty()) return;
    const float available = ImGui::GetContentRegionAvail().x;
    auto image = Lookup(text, static_cast<int>(available));
    if (!image.view) { if (reserve) ImGui::TextUnformatted("[Arabic rendering unavailable]"); return; }
    frameReferences.push_back(image.view);
    auto pos = ImGui::GetCursorScreenPos();
    if (align) pos.x += std::max(0.f, available - image.size.x);
    ImGui::GetWindowDrawList()->AddImage(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(image.view.Get())), pos,
        ImVec2(pos.x+image.size.x,pos.y+image.size.y), ImVec2(0,0),ImVec2(1,1),color);
    if (reserve) ImGui::Dummy(ImVec2(available,image.size.y));
}
std::string Id(const char* label) { return "##" + std::string(label); }
void Label(const char* label) { Paint(Translate(label),true,ImGui::GetColorU32(ImGuiCol_Text)); }
}
void BeginFrame(ID3D11Device* device, bool arabic) {
    gpu = device; rtl = arabic; frameReferences.clear();
}
void Shutdown() { cache.clear(); cachedBytes=0; frameReferences.clear(); gpu.Reset(); }
std::string Value(const char* text) { return rtl ? Translate(text) : std::string(text); }
#define LOCALIZED_TEXT(Name, Color) \
void Name(const char* format, ...) { va_list args; va_start(args,format); auto text = Format(format,args); va_end(args); \
 if (rtl) Paint(text,true,Color); else ImGui::TextUnformatted(text.c_str()); }
LOCALIZED_TEXT(Text, ImGui::GetColorU32(ImGuiCol_Text))
void TextDisabled(const char* format, ...) { va_list args; va_start(args,format); auto text=Format(format,args); va_end(args);
 if(rtl) Paint(text,true,ImGui::GetColorU32(ImGuiCol_TextDisabled)); else { ImGui::BeginDisabled(); ImGui::TextUnformatted(text.c_str()); ImGui::EndDisabled(); } }
void TextWrapped(const char* format, ...) { va_list args; va_start(args,format); auto text=Format(format,args); va_end(args);
 if(rtl) Paint(text,true,ImGui::GetColorU32(ImGuiCol_Text)); else { ImGui::PushTextWrapPos(0); ImGui::TextUnformatted(text.c_str()); ImGui::PopTextWrapPos(); } }
void TextColored(const ImVec4& color,const char* format, ...) { va_list args; va_start(args,format); auto text=Format(format,args); va_end(args);
 if(rtl) Paint(text,true,ImGui::ColorConvertFloat4ToU32(color)); else { ImGui::PushStyleColor(ImGuiCol_Text,color); ImGui::TextUnformatted(text.c_str()); ImGui::PopStyleColor(); } }
void SeparatorText(const char* label) { if(!rtl) ImGui::SeparatorText(label); else { ImGui::Separator(); Label(label); } }
bool Checkbox(const char* label,bool* value) { if(!rtl) return ImGui::Checkbox(label,value); bool changed=ImGui::Checkbox(Id(label).c_str(),value); ImGui::SameLine(); Label(label); return changed; }
bool CheckboxFlags(const char* label,int* flags,int mask) { return ImGui::CheckboxFlags(label,flags,mask); }

bool Button(const char* label) {
 if(!rtl) return ImGui::Button(label);
 auto image=Lookup(Translate(label), static_cast<int>(ImGui::GetContentRegionAvail().x));
 const auto pos=ImGui::GetCursorScreenPos();
 const ImVec2 size(std::max(100.f,image.size.x+24),std::max(32.f,image.size.y+12));
 bool clicked=ImGui::Button(Id(label).c_str(),size);
 if(image.view) { frameReferences.push_back(image.view); ImGui::GetWindowDrawList()->AddImage(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(image.view.Get())),ImVec2(pos.x+12,pos.y+6),ImVec2(pos.x+12+image.size.x,pos.y+6+image.size.y)); }
 return clicked;
}
bool SliderFloat(const char* label,float* value,float lo,float hi,const char* format) { if(!rtl) ImGui::TextUnformatted(label); else Label(label); ImGui::SetNextItemWidth(-1); return ImGui::SliderFloat(Id(label).c_str(),value,lo,hi,format); }
bool SliderInt(const char* label,int* value,int lo,int hi) { if(!rtl) ImGui::TextUnformatted(label); else Label(label); ImGui::SetNextItemWidth(-1); return ImGui::SliderInt(Id(label).c_str(),value,lo,hi); }
bool CollapsingHeader(const char* label) {
 if(!rtl) return ImGui::CollapsingHeader(label);
 const auto pos=ImGui::GetCursorScreenPos(); bool open=ImGui::CollapsingHeader(Id(label).c_str());
 const auto after=ImGui::GetCursorScreenPos(); ImGui::SetCursorScreenPos(pos);
 Paint(Translate(label),false,ImGui::GetColorU32(ImGuiCol_Text)); ImGui::SetCursorScreenPos(after); return open;
}
bool BeginCombo(const char* label,const char* preview) { if(!rtl) return ImGui::BeginCombo(label,preview); Label(label); if(preview) Label(preview); ImGui::SetNextItemWidth(-1); return ImGui::BeginCombo(Id(label).c_str(),nullptr,ImGuiComboFlags_NoPreview); }
bool Selectable(const char* label,bool selected) { if(!rtl) return ImGui::Selectable(label,selected); const auto pos=ImGui::GetCursorScreenPos(); auto image=Lookup(Translate(label),static_cast<int>(ImGui::GetContentRegionAvail().x)); bool choice=ImGui::Selectable(Id(label).c_str(),selected,0,ImVec2(0,std::max(24.f,image.size.y))); auto after=ImGui::GetCursorScreenPos(); ImGui::SetCursorScreenPos(pos); Paint(Translate(label),false,ImGui::GetColorU32(ImGuiCol_Text)); ImGui::SetCursorScreenPos(after); return choice; }
bool Combo(const char* label,int* current,const char* const* items,int count) { if(!rtl) return ImGui::Combo(label,current,items,count); bool changed=false; if(BeginCombo(label,items[*current])) { for(int i=0;i<count;++i) if(Selectable(items[i],i==*current)){ *current=i; changed=true; } ImGui::EndCombo(); } return changed; }
}
