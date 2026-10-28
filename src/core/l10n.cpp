// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KDEGlance contributors
#include "l10n.h"

#include <QHash>
#include <QLocale>

namespace KDEGlance
{

namespace
{
const QHash<QString, QString> &russian()
{
    static const QHash<QString, QString> table{
        {QStringLiteral("Cannot open an empty or invalid URL"), QStringLiteral("Невозможно открыть пустой или неверный адрес")},
        {QStringLiteral("KDEGlance is the default application for this file. Choose another application in Dolphin."),
         QStringLiteral("Для этого файла приложением по умолчанию назначен KDEGlance. Выберите другое приложение в Dolphin.")},
        {QStringLiteral("Active module settings were reset to defaults"), QStringLiteral("Настройки текущего модуля сброшены")},
        {QStringLiteral("Loading…"), QStringLiteral("Загрузка…")},
        {QStringLiteral("Loading document…"), QStringLiteral("Загрузка документа…")},
        {QStringLiteral("Fit width"), QStringLiteral("По ширине")},
        {QStringLiteral("Reflow text"), QStringLiteral("Перекомпоновка текста")},
        {QStringLiteral("Auto encoding"), QStringLiteral("Кодировка автоматически")},
        {QStringLiteral("Markdown / Source"), QStringLiteral("Markdown / Исходный текст")},
        {QStringLiteral("Load next 8 MiB"), QStringLiteral("Загрузить следующие 8 МиБ")},
        {QStringLiteral("%1 pages"), QStringLiteral("Страниц: %1")},
        {QStringLiteral("Extracting text…"), QStringLiteral("Извлечение текста…")},
        {QStringLiteral("No text layer; showing pages"), QStringLiteral("Нет текстового слоя; показаны страницы")},
        {QStringLiteral("Text reflow (layout simplified)"), QStringLiteral("Перекомпоновка текста (упрощённый вид)")},
        {QStringLiteral("text"), QStringLiteral("текст")},
        {QStringLiteral("office documents"), QStringLiteral("офисные документы")},
        {QStringLiteral("documents"), QStringLiteral("документы")},
        {QStringLiteral("%1 items"), QStringLiteral("Элементов: %1")},
        {QStringLiteral("Access to the file was denied"), QStringLiteral("Нет доступа к файлу")},
        {QStringLiteral("Cannot decode RAW data (%1)"), QStringLiteral("Не удалось декодировать данные RAW (%1)")},
        {QStringLiteral("Close the preview when it loses focus"), QStringLiteral("Закрывать просмотр при потере фокуса")},
        {QStringLiteral("Decode every file in DIR and report which formats work."),
         QStringLiteral("Декодировать каждый файл в DIR и показать, какие форматы работают.")},
        {QStringLiteral("Default (Space)"), QStringLiteral("По умолчанию (пробел)")},
        {QStringLiteral("Files to preview."), QStringLiteral("Файлы для просмотра.")},
        {QStringLiteral("Flip horizontally"), QStringLiteral("Отразить по горизонтали")},
        {QStringLiteral("Forget window sizes"), QStringLiteral("Забыть размеры окон")},
        {QStringLiteral("Full screen"), QStringLiteral("Во весь экран")},
        {QStringLiteral("JSON output for --probe-formats."), QStringLiteral("Вывод --probe-formats в формате JSON.")},
        {QStringLiteral("KDEGlance Settings"), QStringLiteral("Настройки KDEGlance")},
        {QStringLiteral("KDEGlance settings were reset to defaults"), QStringLiteral("Настройки KDEGlance сброшены")},
        {QStringLiteral("MIDI playback needs a synthesizer: install fluidsynth and a SoundFont (e.g. soundfont-fluid)"),
         QStringLiteral("Для воспроизведения MIDI нужен синтезатор: установите fluidsynth и банк звуков SoundFont (например, soundfont-fluid)")},
        {QStringLiteral("Mute"), QStringLiteral("Выключить звук")},
        {QStringLiteral("No decoder for this image format is installed"), QStringLiteral("Декодер для этого формата изображений не установлен")},
        {QStringLiteral("Opening the file takes too long; it may be damaged or on a slow device"),
         QStringLiteral("Файл открывается слишком долго: возможно, он повреждён или находится на медленном устройстве")},
        {QStringLiteral("Page %1 is not available"), QStringLiteral("Страница %1 недоступна")},
        {QStringLiteral("Page %1 of %2"), QStringLiteral("Страница %1 из %2")},
        {QStringLiteral("Play / Pause"), QStringLiteral("Воспроизведение / пауза")},
        {QStringLiteral("Play stream"), QStringLiteral("Воспроизвести поток")},
        {QStringLiteral("Playback failed"), QStringLiteral("Ошибка воспроизведения")},
        {QStringLiteral("Print the state of the running service (JSON)."), QStringLiteral("Показать состояние запущенной службы (JSON).")},
        {QStringLiteral("Quick Look key in Dolphin:"), QStringLiteral("Клавиша просмотра в Dolphin:")},
        {QStringLiteral("Quick Look style file previews for Dolphin"), QStringLiteral("Быстрый просмотр файлов в Dolphin в стиле Quick Look")},
        {QStringLiteral("RAW support is not available (KDEGlance was built without LibRaw)"),
         QStringLiteral("Поддержка RAW недоступна (KDEGlance собран без LibRaw)")},
        {QStringLiteral("RealMedia link to a stream:"), QStringLiteral("Ссылка RealMedia на поток:")},
        {QStringLiteral("Remembered window sizes: %1"), QStringLiteral("Запомненные размеры окон: %1")},
        {QStringLiteral("Remembered window sizes: none"), QStringLiteral("Запомненные размеры окон: нет")},
        {QStringLiteral("Repeat"), QStringLiteral("Повтор")},
        {QStringLiteral("Repeat videos"), QStringLiteral("Повторять видео")},
        {QStringLiteral("Required: %1"), QStringLiteral("Требуется: %1")},
        {QStringLiteral("Reset KDEGlance settings to defaults."), QStringLiteral("Сбросить настройки KDEGlance.")},
        {QStringLiteral("Rotate 90° counterclockwise"), QStringLiteral("Повернуть на 90° против часовой стрелки")},
        {QStringLiteral("Rotate left"), QStringLiteral("Повернуть влево")},
        {QStringLiteral("Run as the background preview service."), QStringLiteral("Запустить как фоновую службу просмотра.")},
        {QStringLiteral("SVG support is not available (Qt SVG is missing)"), QStringLiteral("Поддержка SVG недоступна (нет модуля Qt SVG)")},
        {QStringLiteral("Select the previewed file in Dolphin"), QStringLiteral("Синхронизировать выделение в Dolphin с просматриваемым файлом")},
        {QStringLiteral("Show the camera's embedded preview of RAW photos first (much faster)"),
         QStringLiteral("Сначала показывать встроенное превью RAW-фотографий (намного быстрее)")},
        {QStringLiteral("Show the settings window."), QStringLiteral("Показать окно настроек.")},
        {QStringLiteral("Start KDEGlance in the background together with Dolphin (faster first preview)"),
         QStringLiteral("Запускать KDEGlance в фоне вместе с Dolphin (первый просмотр открывается быстрее)")},
        {QStringLiteral("Start playing audio and video automatically"), QStringLiteral("Автоматически начинать воспроизведение аудио и видео")},
        {QStringLiteral("Stop the running service."), QStringLiteral("Остановить запущенную службу.")},
        {QStringLiteral("The MIDI file could not be rendered"), QStringLiteral("Не удалось синтезировать MIDI-файл")},
        {QStringLiteral("The SVG document is invalid"), QStringLiteral("SVG-документ повреждён")},
        {QStringLiteral("The file cannot be opened; it may be damaged"), QStringLiteral("Не удалось открыть файл; возможно, он повреждён")},
        {QStringLiteral("The file cannot be read"), QStringLiteral("Не удалось прочитать файл")},
        {QStringLiteral("The file is not available"), QStringLiteral("Файл недоступен")},
        {QStringLiteral("The file no longer exists or cannot be read"), QStringLiteral("Файл удалён или недоступен для чтения")},
        {QStringLiteral("The image cannot be decoded"), QStringLiteral("Не удалось декодировать изображение")},
        {QStringLiteral("The image data is damaged or incomplete"), QStringLiteral("Данные изображения повреждены или неполны")},
        {QStringLiteral("The stream cannot be reached"), QStringLiteral("Поток недоступен")},
        {QStringLiteral("This audio/video format or codec is not supported by the installed decoders"),
         QStringLiteral("Этот формат или кодек не поддерживается установленными декодерами")},
        {QStringLiteral("Tip: a double middle click in the middle of the preview resets all KDEGlance settings."),
         QStringLiteral("Подсказка: двойной щелчок средней кнопкой в центре окна просмотра сбрасывает все настройки KDEGlance.")},
        {QStringLiteral("Unsupported or damaged RAW file (%1)"), QStringLiteral("RAW-файл не поддерживается или повреждён (%1)")},
        {QStringLiteral("Use hardware video decoding when available (applies after restart)"),
         QStringLiteral("Использовать аппаратное декодирование видео, если доступно (после перезапуска)")},
        {QStringLiteral("Volume"), QStringLiteral("Громкость")},
        {QStringLiteral("audio"), QStringLiteral("аудио")},
        {QStringLiteral("images"), QStringLiteral("изображения")},
        {QStringLiteral("video"), QStringLiteral("видео")},
    };
    return table;
}

QString detectLanguage()
{
    const QString forced = qEnvironmentVariable("KDEGLANCE_LANG");
    if (!forced.isEmpty()) {
        return forced.left(2).toLower();
    }
    // uiLanguages honours LANGUAGE/LC_MESSAGES (Plasma's language setting).
    const QStringList langs = QLocale::system().uiLanguages();
    for (const QString &l : langs) {
        const QString code = l.left(2).toLower();
        if (code == QLatin1String("ru") || code == QLatin1String("en")) {
            return code;
        }
    }
    return QStringLiteral("en");
}
} // namespace

QString uiLanguage()
{
    static const QString lang = detectLanguage();
    return lang;
}

QString tr_(const char *source)
{
    const QString s = QString::fromUtf8(source);
    if (uiLanguage() == QLatin1String("ru")) {
        return russian().value(s, s);
    }
    return s;
}

} // namespace KDEGlance
