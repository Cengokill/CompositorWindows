#pragma once
#include <QString>

namespace compositor::ui {
// A file name stem for an exported layer: Windows-invalid characters become "-", trailing spaces and dots go, and a name
// that is empty, starts with a dot or is a reserved device name gets a "Layer " prefix. The result is at most 120 UTF-16
// units and never ends inside a surrogate pair.
QString layerExportStem(QString name);
}
