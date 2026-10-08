#pragma once

#include <QString>

namespace compositor::logging {

void install();
void trace(const QString& message);
QString logPath();

}
