#include "ssh_config.hpp"

#include <QFile>
#include <QRegularExpression>
#include <QTextStream>
#include <QDir>
#include <QDateTime>
#include <QSaveFile>
#include <QFileDevice>

namespace {
QString configPath() {
    return QDir::homePath() + "/.ssh/config";
}

void applyMetadata(Vm &vm, const QString &key, const QString &value) {
    if (key == "icon") vm.icon = value;
    else if (key == "color") vm.color = value;
    else if (key == "favorite") vm.favorite = value.compare("true", Qt::CaseInsensitive) == 0;
    else if (key == "tags") vm.tags = value.split(',', Qt::SkipEmptyParts);
    for (QString &tag : vm.tags) tag = tag.trimmed();
}
}

bool isSafeAlias(const QString &alias) {
    return !alias.isEmpty() && !alias.startsWith('-') && !alias.contains(QRegularExpression("\\s"));
}

bool saveVm(const Vm &vm, QString *error) {
    if (!isSafeAlias(vm.alias) || vm.hostname.trimmed().isEmpty() || vm.port == 0) {
        if (error) *error = "Preencha alias, hostname e uma porta válida.";
        return false;
    }
    const QString path = configPath();
    QFile source(path);
    if (!source.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) *error = source.errorString();
        return false;
    }
    const QByteArray content = source.readAll();
    source.close();
    const QString backup = path + ".bak." + QDateTime::currentDateTime().toString("yyyyMMdd-hhmmss");
    if (!QFile::copy(path, backup)) {
        if (error) *error = "Não foi possível criar o backup do config.";
        return false;
    }
    QString block = QString("# @icon: %1\n# @color: %2\n# @tags: %3\n# @favorite: %4\nHost %5\n    HostName %6\n")
        .arg(vm.icon, vm.color, vm.tags.join(", "), vm.favorite ? "true" : "false", vm.alias, vm.hostname);
    if (!vm.user.trimmed().isEmpty()) block += QString("    User %1\n").arg(vm.user.trimmed());
    if (!vm.identityFile.trimmed().isEmpty()) block += QString("    IdentityFile %1\n").arg(vm.identityFile.trimmed());
    block += QString("    Port %1\n").arg(vm.port);
    QString updated = QString::fromUtf8(content);
    QStringList lines = updated.split('\n');
    int hostIndex = -1;
    for (int i = 0; i < lines.size(); ++i) {
        const QString trimmed = lines.at(i).trimmed();
        if (trimmed.startsWith("Host ") && trimmed.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts).contains(vm.alias)) { hostIndex = i; break; }
    }
    if (hostIndex >= 0) {
        int start = hostIndex;
        while (start > 0 && lines.at(start - 1).trimmed().startsWith("# @")) --start;
        int end = hostIndex + 1;
        while (end < lines.size() && !lines.at(end).trimmed().startsWith("Host ")) ++end;
        const QStringList replacement = block.trimmed().split('\n');
        lines.erase(lines.begin() + start, lines.begin() + end);
        for (int offset = 0; offset < replacement.size(); ++offset) lines.insert(start + offset, replacement.at(offset));
        updated = lines.join('\n');
    } else {
        updated += (updated.endsWith('\n') ? "" : "\n") + block;
    }
    QSaveFile output(path);
    const QByteArray serialized = updated.toUtf8();
    if (!output.open(QIODevice::WriteOnly | QIODevice::Text) || output.write(serialized) != serialized.size() || !output.commit()) {
        if (error) *error = output.errorString();
        return false;
    }
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

QVector<Vm> loadVms(QString *error) {
    QFile file(configPath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) *error = file.errorString();
        return {};
    }

    QVector<Vm> result;
    Vm pending;
    Vm current;
    QStringList aliases;
    bool inHost = false;
    const auto flush = [&] {
        for (const QString &alias : aliases) {
            Vm vm = current;
            vm.alias = alias;
            result.push_back(vm);
        }
        aliases.clear();
    };

    QTextStream stream(&file);
    while (!stream.atEnd()) {
        const QString raw = stream.readLine();
        const QString line = raw.trimmed();
        if (line.startsWith("# @")) {
            const int separator = line.indexOf(':');
            if (separator > 3) applyMetadata(pending, line.mid(3, separator - 3).trimmed(), line.mid(separator + 1).trimmed());
            continue;
        }
        if (line.isEmpty() || line.startsWith('#')) continue;
        const QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (parts.isEmpty()) continue;
        const QString key = parts.first().toLower();
        if (key == "host") {
            if (inHost) flush();
            QStringList names = parts.mid(1);
            if (names.isEmpty() || std::any_of(names.cbegin(), names.cend(), [](const QString &name) { return name.contains('*') || name.contains('?'); })) {
                pending = Vm{}; current = Vm{}; inHost = false; continue;
            }
            current = pending;
            pending = Vm{};
            aliases = names;
            inHost = true;
        } else if (inHost && parts.size() >= 2) {
            const QString value = parts.mid(1).join(' ').trimmed();
            if (key == "hostname") current.hostname = value;
            else if (key == "user") current.user = value;
            else if (key == "identityfile") current.identityFile = value;
            else if (key == "port") current.port = static_cast<quint16>(value.toUShort());
        }
    }
    if (inHost) flush();
    return result;
}

bool deleteVm(const QString &alias, QString *error) {
    if (!isSafeAlias(alias)) { if (error) *error = "Alias inválido."; return false; }
    const QString path = configPath(); QFile source(path);
    if (!source.open(QIODevice::ReadOnly | QIODevice::Text)) { if (error) *error = source.errorString(); return false; }
    const QByteArray content = source.readAll(); source.close();
    QStringList lines = QString::fromUtf8(content).split('\n'); int hostIndex = -1;
    for (int i = 0; i < lines.size(); ++i) { const QString trimmed = lines.at(i).trimmed(); if (trimmed.startsWith("Host ") && trimmed.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts).contains(alias)) { hostIndex = i; break; } }
    if (hostIndex < 0) { if (error) *error = "Conexão não encontrada."; return false; }
    int start = hostIndex; while (start > 0 && lines.at(start - 1).trimmed().startsWith("# @")) --start;
    int end = hostIndex + 1; while (end < lines.size() && !lines.at(end).trimmed().startsWith("Host ")) ++end;
    lines.erase(lines.begin() + start, lines.begin() + end);
    const QString backup = path + ".bak." + QDateTime::currentDateTime().toString("yyyyMMdd-hhmmss");
    if (!QFile::copy(path, backup)) { if (error) *error = "Não foi possível criar o backup."; return false; }
    const QByteArray updated = lines.join('\n').toUtf8(); QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Text) || output.write(updated) != updated.size() || !output.commit()) { if (error) *error = output.errorString(); return false; }
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner); return true;
}
