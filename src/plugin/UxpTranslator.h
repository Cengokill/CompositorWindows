#pragma once
#include <QString>
#include <QStringList>
#include <QVector>

namespace compositor::plugin {

// Static result of inspecting a Photoshop UXP plugin directory.
struct UxpMapping {
    QString pattern;    // The UXP construct that was recognised.
    QString sdkCall;    // The native SDK call that replaces it.
};

struct UxpImportResult {
    QString id;                 // Derived plugin id.
    QString name;               // Display name from the manifest.
    QString version;
    QString entryFile;          // JavaScript entry point that was analysed.
    QVector<UxpMapping> mapped;
    QStringList unsupported;    // Capabilities with no SDK equivalent.
    QStringList manifestNotes;
    QString generatedSource;    // C skeleton.
    bool analysed{};
};

// Reads a UXP plugin directory and produces a native plugin skeleton.
//
// This is a static translator: it never executes the plugin's JavaScript, and it
// reports every construct it cannot map instead of silently producing a plugin
// that would behave differently from the original.
class UxpTranslator {
public:
    // Returns false and fills error when the directory is not a readable plugin.
    static bool analyse(const QString& pluginDirectory, UxpImportResult& result, QString& error);

    // Writes plugin.c and report.txt into outputDirectory.
    static bool writeFiles(const UxpImportResult& result, const QString& outputDirectory, QString& error);

    // Compiles the generated source into a loadable plugin with the local MSVC
    // toolchain. Returns false with error when no compiler is available.
    static bool compile(const UxpImportResult& result, const QString& outputDirectory,
                        const QString& sdkIncludeDirectory, const QString& binaryPath, QString& error);

    // Locates cl.exe through vswhere/vcvars. Empty when MSVC is not installed.
    static QString findCompiler();
};

}  // namespace compositor::plugin
