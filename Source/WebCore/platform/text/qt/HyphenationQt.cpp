#include "config.h"
#include "Hyphenation.h"
#include "hyphen.h"
#include <wtf/text/AtomicString.h>
#include <string.h>
#if OS(LINUX) || OS(DARWIN)
#include <dlfcn.h>
#endif
#if OS(WINDOWS)
#include <windows.h>
#include <psapi.h>
#define dlsym(hnd, sym) GetProcAddress(hnd, sym)
#endif
#include <QCoreApplication>
#include <QRegExp>
#include <QDebug>
#include <QFile>
#include <QHash>

namespace WebCore {

static QHash<AtomicString, bool> availableDictionaries;

static QString dictionaryPathForLocale(const AtomicString& localeIdentifier)
{
    QString locale(reinterpret_cast<const QChar *>(localeIdentifier.characters()), localeIdentifier.length());
    QString dictionaryPath = QString::fromLatin1("/usr/share/hyphen/hyph_%1.dic").arg(locale);
    if (QFile::exists(dictionaryPath)) {
        return dictionaryPath;
    }
    dictionaryPath = QString::fromLatin1("%1/hyphenDicts/hyph_%2.dic").arg(QCoreApplication::applicationDirPath()).arg(locale);
    if (QFile::exists(dictionaryPath)) {
        return dictionaryPath;
    }
    dictionaryPath = QString::fromLatin1("%1/hyphenDicts/hyph_%2.dic").arg(QCoreApplication::applicationDirPath()).arg(locale.left(2));
    if (QFile::exists(dictionaryPath)) {
        return dictionaryPath;
    }
    return QString::null;
}

bool canHyphenate(const AtomicString& localeIdentifier)
{
    if (!availableDictionaries.contains(localeIdentifier)) {
        availableDictionaries[localeIdentifier] = dictionaryPathForLocale(localeIdentifier).size() > 0;
    }
    return availableDictionaries[localeIdentifier];
}

size_t lastHyphenLocation(const UChar* characters, size_t length, size_t beforeIndex, const AtomicString& localeIdentifier)
{
    static bool initialized = false;
    static HyphenDict *(*hnj_hyphen_load)(const char *);
    static void (*hnj_hyphen_free)(HyphenDict *);
    static int (*hnj_hyphen_hyphenate2)(HyphenDict *, const char *, int, char *, char *, char ***, int **, int **);
    if (!initialized) {
#if OS(LINUX) || OS(DARWIN)
        void *lib = dlopen(NULL, RTLD_LAZY);
#endif
#if OS(WINDOWS)
        HMODULE modules[32];
        DWORD count;
        EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &count);
        HMODULE lib = 0;
        for (int i = 0; i < count / sizeof(HMODULE); i++) {
            HMODULE d  = modules[i];
            if (dlsym(d, "hnj_hyphen_load") != NULL) {
                lib = d;
                break;
            }
        }
#endif
        if (lib == NULL) {
            return 0;
        }

        hnj_hyphen_load = (HyphenDict *(*)(const char *)) dlsym(lib, "hnj_hyphen_load");
        hnj_hyphen_free = (void (*)(HyphenDict *)) dlsym(lib, "hnj_hyphen_free");
        hnj_hyphen_hyphenate2 = (int (*)(HyphenDict *, const char *, int, char *, char *, char ***, int **, int **)) dlsym(lib, "hnj_hyphen_hyphenate2");
        if (hnj_hyphen_load == NULL) {
            return 0;
        }
        initialized = true;
    }
    static HyphenDict *dict = NULL;
    static AtomicString dictLocale;
    if (dict == NULL || dictLocale != localeIdentifier) {
        if (dict) {
            hnj_hyphen_free(dict);
            dict = NULL;
        }
        QByteArray dictionaryPath = dictionaryPathForLocale(localeIdentifier).toUtf8();
        dict = hnj_hyphen_load(dictionaryPath.constData());
        dictLocale = localeIdentifier;
        if (dict == NULL) {
            qWarning() << "Couldn't load a hyphenation dictionary!";
            return 0;
        }
    }

    static QRegExp punctuation("[^A-Z0-9]", Qt::CaseInsensitive);
    static QRegExp spaces("\\s+$");
    QString s = QString::fromRawData(reinterpret_cast<const QChar *>(characters), length).replace(punctuation, " ").remove(spaces).toLower();
    QByteArray c = s.simplified().toUtf8();
    int leadingSpaces = length - c.length();

    if (c.length() - c.lastIndexOf(" ") <= 5) {
        return 0;
    }

    char *hyphens = (char *) malloc(c.size() + 5);
    char *hword = (char *) malloc(c.size() * 2);
    memset(hyphens, 0, c.size() + 5);
    memset(hword, 0, c.size() * 2);

    char **rep = NULL;
    int *pos = NULL;
    int *cut = NULL;
    int index = 0;

    hnj_hyphen_hyphenate2(dict, c.constData(), c.size(), hyphens, hword, &rep, &pos, &cut);

    for (int i = beforeIndex - 2 - leadingSpaces; i > 0; i--) {
        if (hyphens[i] & 1) {
            index = i + 1 + leadingSpaces;
            break;
        }
    }

    free(rep);
    free(pos);
    free(cut);

    free(hyphens);
    free(hword);

    return index;
}

}
