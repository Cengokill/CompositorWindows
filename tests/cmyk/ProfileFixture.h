#pragma once
#include <QByteArray>
#include <QFile>
#include <QTemporaryDir>
#include <QString>
#include <filesystem>
#include <stdexcept>

namespace cmyk_test {
inline QByteArray profileBytes(const std::filesystem::path& source=std::filesystem::path(__FILE__).parent_path()/"synthetic-cmyk.icc.base64"){
    QFile file(QString::fromStdWString(source.wstring()));
    if(!file.open(QIODevice::ReadOnly))throw std::runtime_error("Cannot read synthetic CMYK fixture");
    auto encoded=file.readAll();encoded.replace("\r","");encoded.replace("\n","");
    const auto bytes=QByteArray::fromBase64(encoded,QByteArray::AbortOnBase64DecodingErrors);
    if(bytes.isEmpty())throw std::runtime_error("Invalid synthetic CMYK fixture encoding");
    return bytes;
}
// Native file pickers need a real ICC file; keep the decoded fixture outside the repository.
struct ProfileFile {
    QTemporaryDir directory;
    QString path;
    ProfileFile():path(directory.filePath("synthetic-cmyk.icc")){
        if(!directory.isValid())throw std::runtime_error("Cannot create CMYK fixture directory");
        QFile file(path);const auto bytes=profileBytes();
        if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size())throw std::runtime_error("Cannot prepare CMYK profile fixture");
    }
};
}
