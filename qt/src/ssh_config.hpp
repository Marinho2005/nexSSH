#pragma once

#include <QString>
#include <QVector>
#include <QStringList>

struct Vm {
    QString alias;
    QString hostname;
    QString user;
    QString identityFile;
    quint16 port = 22;
    QString icon = "server";
    QString color = "#59636e";
    QStringList tags;
    bool favorite = false;
};

QVector<Vm> loadVms(QString *error = nullptr);
bool isSafeAlias(const QString &alias);
bool saveVm(const Vm &vm, QString *error = nullptr);
bool deleteVm(const QString &alias, QString *error = nullptr);
