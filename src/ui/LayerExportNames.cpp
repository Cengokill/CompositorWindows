#include "LayerExportNames.h"
#include <QRegularExpression>

namespace compositor::ui {
QString layerExportStem(QString name){
    for(auto& character:name)if(character.unicode()<32||QStringLiteral("<>:\"/\\|?*").contains(character))character=QLatin1Char('-');
    const auto trim=[](QString& value){while(!value.isEmpty()&&(value.endsWith(QLatin1Char(' '))||value.endsWith(QLatin1Char('.'))))value.chop(1);};
    trim(name);
    static const QRegularExpression reserved(QStringLiteral("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\\..*)?$"),QRegularExpression::CaseInsensitiveOption);
    if(name.isEmpty()||name.startsWith(QLatin1Char('.'))||reserved.match(name).hasMatch())name=name.isEmpty()?QStringLiteral("Layer"):QStringLiteral("Layer ")+name;
    if(name.size()>120){name.truncate(120);if(name.back().isHighSurrogate())name.chop(1);trim(name);}
    return name;
}
}
