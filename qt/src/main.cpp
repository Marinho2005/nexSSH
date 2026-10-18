#include "ssh_config.hpp"

#include <QApplication>
#include <QDateTime>
#include <QFrame>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QProcess>
#include <QPainter>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSet>
#include <QSpinBox>
#include <QStyle>
#include <QStackedWidget>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QDir>
#include <qtermwidget.h>
#include <memory>
#include <vector>
#include <functional>
#include <algorithm>
#include <QHash>

namespace {
QString iconResource(QString name) {
    if (QFileInfo::exists(name)) return name;
    name = name.section('/', -1).section('.', 0, 0).toLower();
    const QSet<QString> known = {"server", "database", "container", "cloud", "debian", "ubuntu", "archlinux", "alpinelinux", "fedora", "centos", "rockylinux", "googlecloud", "digitalocean", "hetzner", "oracle", "amazonaws"};
    return ":/icons/" + (known.contains(name) ? name : "server") + ".svg";
}

QPixmap tintedIcon(const QString &path, const QColor &color) {
    const QPixmap source = QIcon(path).pixmap(32, 32);
    QPixmap result(32, 32);
    result.fill(Qt::transparent);
    QPainter painter(&result);
    painter.drawPixmap(0, 0, source);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(result.rect(), color.isValid() ? color : QColor("#aeb6c0"));
    return result;
}

QColor brandColor(const QString &iconName, const QString &configuredColor) {
    if (QFileInfo::exists(iconName)) return QColor(configuredColor);
    const QString name = iconName.section('/', -1).section('.', 0, 0).toLower();
    static const QHash<QString, QColor> colors = {
        {"debian", QColor("#A81D33")}, {"ubuntu", QColor("#E95420")},
        {"archlinux", QColor("#1793D1")}, {"alpinelinux", QColor("#0D597F")},
        {"fedora", QColor("#51A2DA")}, {"centos", QColor("#932279")},
        {"rockylinux", QColor("#10B981")}, {"googlecloud", QColor("#4285F4")},
        {"digitalocean", QColor("#0080FF")}, {"hetzner", QColor("#D50C2D")},
        {"oracle", QColor("#F80000")}, {"amazonaws", QColor("#FF9900")},
        {"server", QColor("#AEB6C0")}, {"database", QColor("#5C8D89")},
        {"container", QColor("#2496ED")}, {"cloud", QColor("#6B7280")}
    };
    return colors.value(name, QColor(configuredColor).isValid() ? QColor(configuredColor) : QColor("#AEB6C0"));
}

void openTerminal(const QString &alias) {
    if (!isSafeAlias(alias)) return;
    const QVector<Vm> knownVms = loadVms();
    const bool known = std::any_of(knownVms.cbegin(), knownVms.cend(), [&alias](const Vm &vm) { return vm.alias == alias; });
    if (!known) return;
    // Avoid the distro wrapper x-terminal-emulator: on this machine it points to
    // Terminator, which emits duplicate global-shortcut warnings per instance.
    const QStringList terminals = {"konsole", "xterm", "gnome-terminal", "alacritty", "kitty"};
    for (const QString &terminal : terminals) {
        const QStringList args = terminal == "gnome-terminal" ? QStringList{"--", "ssh", alias} : QStringList{"-e", "ssh", alias};
        if (QProcess::startDetached(terminal, args)) return;
    }
}

class VmListButton final : public QPushButton {
public:
    VmListButton(const QString &text, const std::function<void()> &onDoubleClick, QWidget *parent = nullptr)
        : QPushButton(text, parent), onDoubleClick_(onDoubleClick) {}

protected:
    void mouseDoubleClickEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton && onDoubleClick_) onDoubleClick_();
        QPushButton::mouseDoubleClickEvent(event);
    }

private:
    std::function<void()> onDoubleClick_;
};

class VmCard final : public QFrame {
public:
    explicit VmCard(const Vm &vm, const std::function<void(const Vm &)> &onEdit, const std::function<void(const QString &)> &onConnect, QWidget *parent = nullptr) : QFrame(parent) {
        aliasName_ = vm.alias;
        onConnect_ = onConnect;
        setObjectName("vmCard");
        setFixedSize(190, 190);
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(18, 18, 18, 14);
        layout->setSpacing(9);
        auto *top = new QHBoxLayout;
        auto *icon = new QLabel(this);
        icon->setFixedSize(48, 48);
        icon->setPixmap(tintedIcon(iconResource(vm.icon), brandColor(vm.icon, vm.color)));
        icon->setAlignment(Qt::AlignCenter);
        icon->setStyleSheet("background: #3a3d43; border-radius: 10px; padding: 9px;");
        top->addWidget(icon);
        top->addStretch();
        auto *favorite = new QLabel(vm.favorite ? QStringLiteral("★") : QStringLiteral("☆"), this);
        favorite->setObjectName("favorite");
        top->addWidget(favorite);
        layout->addLayout(top);
        auto *alias = new QLabel(vm.alias, this);
        alias->setObjectName("alias");
        layout->addWidget(alias);
        auto *endpoint = new QLabel(QString("%1%2:%3").arg(vm.user.isEmpty() ? "" : vm.user + "@").arg(vm.hostname).arg(vm.port), this);
        endpoint->setObjectName("endpoint");
        layout->addWidget(endpoint);
        auto *tags = new QLabel(vm.tags.join("  "), this);
        tags->setObjectName("tags");
        layout->addWidget(tags);
        layout->addStretch();
        auto *edit = new QPushButton(QStringLiteral("Editar"), this); edit->setObjectName("secondaryButton");
        layout->addWidget(edit);
        QObject::connect(edit, &QPushButton::clicked, this, [vm, onEdit] { onEdit(vm); });
        installEventFilter(this);
        for (QWidget *child : findChildren<QWidget*>()) child->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        Q_UNUSED(watched);
        if (event->type() == QEvent::MouseButtonDblClick) {
            onConnect_(aliasName_);
            return true;
        }
        return QFrame::eventFilter(watched, event);
    }

private:
    QString aliasName_;
    std::function<void(const QString &)> onConnect_;
};
}

class MainWindow final : public QMainWindow {
public:
    MainWindow() {
        setWindowTitle("nexSSH");
        resize(1020, 700);
        setMinimumSize(760, 520);
        auto *shell = new QWidget(this);
        auto *shellLayout = new QHBoxLayout(shell);
        shellLayout->setContentsMargins(0, 0, 0, 0);
        shellLayout->setSpacing(0);

        auto *sidebar = new QFrame(shell);
        sidebar->setObjectName("sidebar");
        sidebar->setFixedWidth(280);
        auto *sideLayout = new QVBoxLayout(sidebar);
        sideLayout->setContentsMargins(22, 20, 18, 20);
        sideLayout->setSpacing(12);
        auto *brand = new QLabel("nexSSH", sidebar);
        brand->setObjectName("brand");
        sideLayout->addWidget(brand);
        auto *brandSub = new QLabel("GERENCIADOR OPENSSH", sidebar);
        brandSub->setObjectName("brandSub");
        sideLayout->addWidget(brandSub);
        auto *actions = new QHBoxLayout;
        auto *newButton = new QToolButton(sidebar);
        newButton->setText("＋");
        newButton->setToolTip("Nova conexão");
        auto *editButton = new QToolButton(sidebar);
        editButton->setText("✎");
        editButton->setToolTip("Editar conexão selecionada");
        auto *deleteButton = new QToolButton(sidebar);
        deleteButton->setIcon(deleteButton->style()->standardIcon(QStyle::SP_TrashIcon));
        deleteButton->setIconSize(QSize(18, 18));
        deleteButton->setToolTip("Excluir conexão selecionada");
        auto *connectionsButton = new QToolButton(sidebar);
        connectionsButton->setText("▣");
        connectionsButton->setToolTip("Conexões");
        actions->addWidget(newButton);
        actions->addWidget(editButton);
        actions->addWidget(deleteButton);
        actions->addWidget(connectionsButton);
        actions->addStretch();
        sideLayout->addLayout(actions);
        auto *sideSearch = new QLineEdit(sidebar);
        sideSearch->setPlaceholderText("Filtrar VPS...");
        sideSearch->setObjectName("sideSearch");
        sideLayout->addWidget(sideSearch);
        vmList_ = new QVBoxLayout;
        vmList_->setSpacing(6);
        sideLayout->addLayout(vmList_);
        sideLayout->addStretch();
        auto *configPath = new QLabel("~/.ssh/config", sidebar);
        configPath->setObjectName("configPath");
        sideLayout->addWidget(configPath);
        shellLayout->addWidget(sidebar);

        pages_ = new QStackedWidget(shell);
        pages_->addWidget(buildConnectionsPage());
        pages_->addWidget(buildNewConnectionPage());
        pages_->addWidget(buildTerminalPage());
        shellLayout->addWidget(pages_, 1);
        setCentralWidget(shell);

        connect(newButton, &QToolButton::clicked, this, [this] { clearForm(); pages_->setCurrentIndex(1); });
        connect(connectionsButton, &QToolButton::clicked, this, [this] { pages_->setCurrentIndex(0); refresh(); });
        connect(sideSearch, &QLineEdit::textChanged, this, [this](const QString &text) {
            for (int i = 0; i < vmList_->count(); ++i) {
                if (auto *widget = vmList_->itemAt(i)->widget())
                    widget->setVisible(text.isEmpty() || widget->property("alias").toString().contains(text, Qt::CaseInsensitive));
            }
        });
        connect(editButton, &QToolButton::clicked, this, [this] { editSelected(); });
        connect(deleteButton, &QToolButton::clicked, this, [this] { deleteSelected(); });
        refreshSidebar(vmList_);
    }

private:
    QStackedWidget *pages_ = nullptr;
    QTabWidget *terminalTabs_ = nullptr;
    QGridLayout *cards_ = nullptr;
    QWidget *cardsHost_ = nullptr;
    std::vector<std::unique_ptr<VmCard>> cardStorage_;
    QVBoxLayout *vmList_ = nullptr;
    QString selectedAlias_;
    std::vector<std::unique_ptr<QTermWidget>> terminalStorage_;
    QLineEdit *alias_ = nullptr;
    QLineEdit *hostname_ = nullptr;
    QLineEdit *user_ = nullptr;
    QLineEdit *identity_ = nullptr;
    QComboBox *authMode_ = nullptr;
    QWidget *identityRow_ = nullptr;
    QLineEdit *password_ = nullptr;
    QFormLayout *connectionForm_ = nullptr;
    QHash<QString, QString> passwordCache_;
    QSpinBox *port_ = nullptr;
    QComboBox *icon_ = nullptr;
    QString customIconPath_;
    Vm editingVm_;

    QWidget *buildConnectionsPage() {
        auto *page = new QWidget;
        auto *layout = new QVBoxLayout(page);
        layout->setContentsMargins(38, 28, 38, 28);
        auto *title = new QLabel("Conexões", page); title->setObjectName("pageTitle"); layout->addWidget(title);
        auto *subtitle = new QLabel("Suas VPS e hosts OpenSSH em um só lugar.", page); subtitle->setObjectName("pageSubtitle"); layout->addWidget(subtitle);
        auto *search = new QLineEdit(page); search->setPlaceholderText("Buscar conexões..."); search->setClearButtonEnabled(true); search->setObjectName("search"); layout->addWidget(search);
        auto *scroll = new QScrollArea(page); scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame); cardsHost_ = new QWidget; cards_ = new QGridLayout(cardsHost_); cards_->setContentsMargins(0, 12, 0, 0); cards_->setHorizontalSpacing(0); cards_->setVerticalSpacing(16); cards_->setAlignment(Qt::AlignTop | Qt::AlignLeft); scroll->setWidget(cardsHost_); layout->addWidget(scroll, 1);
        connect(search, &QLineEdit::textChanged, this, [this](const QString &text) { for (QFrame *card : cardsHost_->findChildren<QFrame*>("vmCard")) card->setVisible(text.isEmpty() || card->findChild<QLabel*>("alias")->text().contains(text, Qt::CaseInsensitive)); });
        return page;
    }

    QWidget *buildNewConnectionPage() {
        auto *page = new QWidget; auto *layout = new QVBoxLayout(page); layout->setContentsMargins(50, 38, 50, 38); layout->setSpacing(14);
        auto *title = new QLabel("Nova conexão", page); title->setObjectName("pageTitle"); layout->addWidget(title); auto *subtitle = new QLabel("Adicione uma VPS ao seu config do OpenSSH.", page); subtitle->setObjectName("pageSubtitle"); layout->addWidget(subtitle);
        auto *form = new QFormLayout; connectionForm_ = form; alias_ = new QLineEdit; hostname_ = new QLineEdit; user_ = new QLineEdit; identity_ = new QLineEdit; password_ = new QLineEdit; authMode_ = new QComboBox; port_ = new QSpinBox; icon_ = new QComboBox; port_->setRange(0, 65535); port_->setValue(0); port_->setSpecialValueText("22"); password_->setEchoMode(QLineEdit::Password);
        authMode_->addItem("Chave privada", "key"); authMode_->addItem("Login e senha", "password");
        icon_->addItem("Servidor", "server"); icon_->addItem("Debian", "debian"); icon_->addItem("Ubuntu", "ubuntu"); icon_->addItem("Arch Linux", "archlinux"); icon_->addItem("Alpine", "alpinelinux"); icon_->addItem("Fedora", "fedora"); icon_->addItem("CentOS", "centos"); icon_->addItem("Rocky Linux", "rockylinux"); icon_->addItem("Google Cloud", "googlecloud"); icon_->addItem("AWS", "amazonaws"); icon_->addItem("DigitalOcean", "digitalocean"); icon_->addItem("Hetzner", "hetzner"); icon_->addItem("Oracle", "oracle");
        auto *keyBrowse = new QPushButton("Escolher arquivo...", page);
        identityRow_ = new QWidget(page); auto *identityLayout = new QHBoxLayout(identityRow_); identityLayout->setContentsMargins(0, 0, 0, 0); identityLayout->setSpacing(8); identityLayout->addWidget(identity_); identityLayout->addWidget(keyBrowse);
        auto *chooseSvg = new QPushButton("Escolher SVG...", page); form->addRow("Alias", alias_); form->addRow("Hostname", hostname_); form->addRow("Usuário / login", user_); form->addRow("Autenticação", authMode_); form->addRow("Chave privada", identityRow_); form->addRow("Senha", password_); form->addRow("Porta (sugerida: 22)", port_); form->addRow("Sistema / ícone", icon_); form->addRow("SVG próprio", chooseSvg); layout->addLayout(form);
        auto *authHint = new QLabel("A senha fica somente em memória durante esta execução e é enviada apenas ao prompt do SSH. A passphrase da chave continua sendo solicitada pelo terminal.", page); authHint->setObjectName("pageSubtitle"); layout->addWidget(authHint);
        connect(authMode_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) { if (identityRow_) identityRow_->setVisible(index == 0); if (password_) password_->setVisible(index == 1); if (connectionForm_) connectionForm_->labelForField(password_)->setVisible(index == 1); });
        password_->setVisible(false);
        if (QWidget *passwordLabel = connectionForm_->labelForField(password_)) passwordLabel->setVisible(false);
        connect(keyBrowse, &QPushButton::clicked, this, [this] { const QString path = QFileDialog::getOpenFileName(this, "Escolher chave privada", QDir::homePath(), "Chaves privadas (*.pem *.key id_*);;Todos os arquivos (*)"); if (!path.isEmpty()) identity_->setText(path); });
        connect(chooseSvg, &QPushButton::clicked, this, [this, chooseSvg] { const QString path = QFileDialog::getOpenFileName(this, "Escolher SVG", QDir::homePath(), "SVG (*.svg)"); if (!path.isEmpty()) { customIconPath_ = path; chooseSvg->setText(QFileInfo(path).fileName()); } });
        auto *buttons = new QHBoxLayout; auto *cancel = new QPushButton("Cancelar"); auto *save = new QPushButton("Salvar conexão"); save->setObjectName("primaryButton"); buttons->addStretch(); buttons->addWidget(cancel); buttons->addWidget(save); layout->addLayout(buttons); layout->addStretch();
        connect(cancel, &QPushButton::clicked, this, [this] { pages_->setCurrentIndex(0); });
        connect(save, &QPushButton::clicked, this, [this] { Vm vm = editingVm_; vm.alias = alias_->text().trimmed(); vm.hostname = hostname_->text().trimmed(); vm.user = user_->text().trimmed(); vm.identityFile = authMode_->currentData().toString() == "key" ? identity_->text().trimmed() : QString(); vm.port = static_cast<quint16>(port_->value() == 0 ? 22 : port_->value()); vm.icon = customIconPath_.isEmpty() ? icon_->currentData().toString() : customIconPath_; QString error; if (!saveVm(vm, &error)) { QMessageBox::warning(this, "Não foi possível salvar", error); return; } if (authMode_->currentData().toString() == "password" && !password_->text().isEmpty()) passwordCache_[vm.alias] = password_->text(); else if (authMode_->currentData().toString() == "key") passwordCache_.remove(vm.alias); clearForm(); pages_->setCurrentIndex(0); refresh(); });
        return page;
    }

    void refreshSidebar(QVBoxLayout *list) {
        while (QLayoutItem *item = list->takeAt(0)) { if (QWidget *widget = item->widget()) widget->deleteLater(); delete item; }
        QString error; for (const Vm &vm : loadVms(&error)) {
            auto *button = new VmListButton(vm.alias, [this, alias = vm.alias] { startSession(alias); });
            button->setIcon(QIcon(tintedIcon(iconResource(vm.icon), brandColor(vm.icon, vm.color))));
            button->setIconSize(QSize(22, 22));
            button->setProperty("alias", vm.alias);
            button->setObjectName("vmListButton");
            button->setCheckable(true);
            list->addWidget(button);
            connect(button, &QPushButton::clicked, this, [this, alias = vm.alias] { selectVm(alias); });
        }
    }

    void selectVm(const QString &alias) {
        selectedAlias_ = alias;
        for (QPushButton *button : findChildren<QPushButton*>("vmListButton")) button->setChecked(button->property("alias").toString() == alias);
    }

    void refresh() {
        cardStorage_.clear(); while (QLayoutItem *item = cards_->takeAt(0)) delete item;
        QString error; const QVector<Vm> vms = loadVms(&error); for (int i = 0; i < vms.size(); ++i) { auto card = std::make_unique<VmCard>(vms.at(i), [this](const Vm &vm) { editVm(vm); }, [this](const QString &alias) { startSession(alias); }, cardsHost_); cards_->addWidget(card.get(), i, 0, Qt::AlignTop | Qt::AlignLeft); cardStorage_.push_back(std::move(card)); }
        refreshSidebar(vmList_);
        const bool stillExists = std::any_of(vms.cbegin(), vms.cend(), [this](const Vm &vm) { return vm.alias == selectedAlias_; });
        if (stillExists) selectVm(selectedAlias_); else selectVm(QString());
    }

    QWidget *buildTerminalPage() {
        auto *page = new QWidget;
        auto *layout = new QVBoxLayout(page);
        layout->setContentsMargins(10, 10, 10, 10);
        terminalTabs_ = new QTabWidget(page);
        terminalTabs_->setTabsClosable(true);
        layout->addWidget(terminalTabs_);
        connect(terminalTabs_, &QTabWidget::tabCloseRequested, this, [this](int index) {
            QWidget *widget = terminalTabs_->widget(index);
            terminalTabs_->removeTab(index);
            for (auto it = terminalStorage_.begin(); it != terminalStorage_.end(); ++it) {
                if (it->get() == widget) { terminalStorage_.erase(it); break; }
            }
            if (terminalTabs_->count() == 0) pages_->setCurrentIndex(0);
        });
        return page;
    }

    void startSession(const QString &alias) {
        const QVector<Vm> vms = loadVms();
        if (!isSafeAlias(alias) || !std::any_of(vms.cbegin(), vms.cend(), [&alias](const Vm &vm) { return vm.alias == alias; })) return;
        auto terminal = std::make_unique<QTermWidget>(0, terminalTabs_);
        const QString password = passwordCache_.value(alias);
        terminal->setShellProgram("/usr/bin/ssh");
        terminal->setArgs({alias});
        terminal->setColorScheme("Linux");
        terminal->setScrollBarPosition(QTermWidget::ScrollBarRight);
        if (!password.isEmpty()) {
            auto passwordSent = std::make_shared<bool>(false);
            connect(terminal.get(), &QTermWidget::receivedData, terminal.get(), [terminalPtr = terminal.get(), password, passwordSent](const QString &text) {
                if (!*passwordSent && text.contains(QRegularExpression("(password|senha|passphrase)\\s*:", QRegularExpression::CaseInsensitiveOption))) {
                    *passwordSent = true;
                    terminalPtr->sendText(password + "\n");
                }
            });
        }
        terminal->startShellProgram();
        terminalTabs_->addTab(terminal.get(), alias);
        terminalTabs_->setCurrentWidget(terminal.get());
        terminalStorage_.push_back(std::move(terminal));
        pages_->setCurrentIndex(2);
        terminalTabs_->currentWidget()->setFocus();
    }

    void editVm(const Vm &vm) { editingVm_ = vm; alias_->setText(vm.alias); hostname_->setText(vm.hostname); user_->setText(vm.user); identity_->setText(vm.identityFile); password_->setText(passwordCache_.value(vm.alias)); authMode_->setCurrentIndex(vm.identityFile.trimmed().isEmpty() ? 1 : 0); port_->setValue(vm.port); customIconPath_ = vm.icon.contains('/') ? vm.icon : QString(); const int index = icon_->findData(vm.icon); icon_->setCurrentIndex(index >= 0 ? index : 0); pages_->setCurrentIndex(1); }
    void editSelected() { if (selectedAlias_.isEmpty()) return; for (const Vm &vm : loadVms()) if (vm.alias == selectedAlias_) { editVm(vm); return; } }
    void deleteSelected() { if (selectedAlias_.isEmpty()) return; if (QMessageBox::question(this, "Excluir conexão", QString("Excluir '%1' do ~/.ssh/config?").arg(selectedAlias_)) != QMessageBox::Yes) return; QString error; if (!deleteVm(selectedAlias_, &error)) { QMessageBox::warning(this, "Não foi possível excluir", error); return; } passwordCache_.remove(selectedAlias_); selectedAlias_.clear(); refresh(); }
    void clearForm() { editingVm_ = Vm{}; alias_->clear(); hostname_->clear(); user_->clear(); identity_->clear(); password_->clear(); authMode_->setCurrentIndex(0); port_->setValue(0); icon_->setCurrentIndex(0); customIconPath_.clear(); }
};

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setStyleSheet("QMainWindow { background: #1f2023; } QFrame#sidebar { background: #2b2d31; } QLabel#brand { color: #f1f2f4; font-size: 21px; font-weight: 700; } QLabel#brandSub, QLabel#configPath { color: #90959d; font-size: 10px; letter-spacing: 1px; } QToolButton { color: #e4e6e9; background: transparent; border: 0; font-size: 22px; padding: 7px; } QToolButton:hover { background: #3a3d43; border-radius: 6px; } QToolButton#enterButton { color: #ffffff; background: #5c78a8; border-radius: 6px; padding: 10px; font-size: 14px; font-weight: 600; } QToolButton#enterButton:disabled { color: #777d86; background: #34363b; } QLineEdit { background: #303237; color: #f1f2f4; border: 1px solid #484b52; border-radius: 7px; padding: 10px; } QLineEdit:focus { border: 2px solid #6f8fc4; } QLineEdit#search { background: #27292d; min-height: 22px; } QPushButton#vmListButton { text-align: left; color: #e4e6e9; background: #34363b; border: 1px solid #4d515a; border-radius: 6px; padding: 8px; } QPushButton#vmListButton:hover, QPushButton#vmListButton:checked { background: #4b5260; border-color: #6f8fc4; } QLabel#pageTitle { color: #f1f2f4; font-size: 29px; font-weight: 600; } QLabel#pageSubtitle { color: #949aa3; font-size: 14px; margin-bottom: 7px; } QFrame#vmCard { background: #292b2f; border: 1px solid #41444b; border-radius: 10px; } QLabel#alias { color: #f1f2f4; font-size: 21px; } QLabel#endpoint, QLabel#tags { color: #969ca5; font-family: monospace; font-size: 12px; } QLabel#favorite { color: #abb1bb; font-size: 23px; } QPushButton#connectButton { color: #d4d8de; background: transparent; border: 0; border-top: 1px solid #41444b; padding: 9px; } QPushButton#connectButton:hover { background: #383b42; } QPushButton#primaryButton { background: #5c78a8; color: white; border: 0; border-radius: 6px; padding: 10px 16px; } QFormLayout QLabel { color: #c3c7ce; }");
    MainWindow window; window.show(); return app.exec();
}
