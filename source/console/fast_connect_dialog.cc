//
// Aspia Project
// Copyright (C) 2016-2022 Dmitry Chapyshev <dmitry@aspia.ru>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.
//

#include "console/fast_connect_dialog.h"

#include "base/logging.h"
#include "base/net/address.h"
#include "build/build_config.h"
#include "client/config_factory.h"
#include "client/router_config_storage.h"
#include "client/ui/client_settings.h"
#include "client/ui/desktop_config_dialog.h"
#include "client/ui/qt_desktop_window.h"
#include "client/ui/qt_file_manager_window.h"
#include "common/desktop_session_constants.h"
#include "common/ui/session_type.h"
#include "console/application.h"

#include <QMessageBox>

namespace console {

FastConnectDialog::FastConnectDialog(QWidget* parent,
                                     const QString& address_book_guid,
                                     const std::optional<client::RouterConfig>& router_config)
    : QDialog(parent),
      address_book_guid_(address_book_guid),
      router_config_(router_config)
{
    LOG(LS_INFO) << "Ctor";

    ui.setupUi(this);
    readState();

    QComboBox* combo_address = ui.combo_address;

    combo_address->addItems(state_.history);
    combo_address->setCurrentIndex(0);

    auto add_session = [this](const QString& icon, proto::SessionType session_type)
    {
        ui.combo_session_type->addItem(QIcon(icon),
                                       common::sessionTypeToLocalizedString(session_type),
                                       QVariant(session_type));
    };

    add_session(QStringLiteral(":/img/monitor-keyboard.png"), proto::SESSION_TYPE_DESKTOP_MANAGE);
    add_session(QStringLiteral(":/img/monitor.png"), proto::SESSION_TYPE_DESKTOP_VIEW);
    add_session(QStringLiteral(":/img/folder-stand.png"), proto::SESSION_TYPE_FILE_TRANSFER);

    int current_session_type = ui.combo_session_type->findData(QVariant(state_.session_type));
    if (current_session_type != -1)
    {
        ui.combo_session_type->setCurrentIndex(current_session_type);
        sessionTypeChanged(current_session_type);
    }

    connect(ui.button_clear, &QPushButton::clicked, this, [this]()
    {
        int ret = QMessageBox::question(
            this,
            tr("Confirmation"),
            tr("The list of entered addresses will be cleared. Continue?"),
            QMessageBox::Yes | QMessageBox::No);

        if (ret == QMessageBox::Yes)
        {
            ui.combo_address->clear();
            state_.history.clear();
            writeState();
        }
    });

    connect(ui.combo_session_type, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &FastConnectDialog::sessionTypeChanged);

    connect(ui.button_session_config, &QPushButton::clicked,
            this, &FastConnectDialog::sessionConfigButtonPressed);

    connect(ui.button_box, &QDialogButtonBox::clicked, this, &FastConnectDialog::onButtonBoxClicked);

    combo_address->setFocus();
}

FastConnectDialog::~FastConnectDialog()
{
    LOG(LS_INFO) << "Dtor";
    writeState();
}

void FastConnectDialog::sessionTypeChanged(int item_index)
{
    state_.session_type = static_cast<proto::SessionType>(
        ui.combo_session_type->itemData(item_index).toInt());

    switch (state_.session_type)
    {
        case proto::SESSION_TYPE_DESKTOP_MANAGE:
        case proto::SESSION_TYPE_DESKTOP_VIEW:
            ui.button_session_config->setEnabled(true);
            break;

        default:
            ui.button_session_config->setEnabled(false);
            break;
    }
}

void FastConnectDialog::sessionConfigButtonPressed()
{
    proto::SessionType session_type = static_cast<proto::SessionType>(
        ui.combo_session_type->currentData().toInt());

    switch (session_type)
    {
        case proto::SESSION_TYPE_DESKTOP_MANAGE:
        {
            client::DesktopConfigDialog dialog(session_type,
                                               state_.desktop_manage_config,
                                               common::kSupportedVideoEncodings,
                                               this);

            if (dialog.exec() == client::DesktopConfigDialog::Accepted)
                state_.desktop_manage_config = dialog.config();
        }
        break;

        case proto::SESSION_TYPE_DESKTOP_VIEW:
        {
            client::DesktopConfigDialog dialog(session_type,
                                               state_.desktop_view_config,
                                               common::kSupportedVideoEncodings,
                                               this);

            if (dialog.exec() == client::DesktopConfigDialog::Accepted)
                state_.desktop_view_config = dialog.config();
        }
        break;

        default:
            break;
    }
}

void FastConnectDialog::onButtonBoxClicked(QAbstractButton* button)
{
    if (ui.button_box->standardButton(button) == QDialogButtonBox::Cancel)
    {
        reject();
        close();
        return;
    }

    QComboBox* combo_address = ui.combo_address;
    QString current_address = combo_address->currentText();
    bool host_id_entered = true;

    for (int i = 0; i < current_address.length(); ++i)
    {
        if (!current_address[i].isDigit())
        {
            host_id_entered = false;
            break;
        }
    }

    if (host_id_entered && !router_config_.has_value())
    {
        QMessageBox::warning(this,
                             tr("Warning"),
                             tr("Connection by ID is specified but the router is not configured. "
                                "Check the parameters of the router in the properties of the "
                                "address book."),
                             QMessageBox::Ok);
        return;
    }

    client::Config client_config;

    if (!host_id_entered)
    {
        LOG(LS_INFO) << "Direct connection selected";

        base::Address address = base::Address::fromString(
            current_address.toStdU16String(), DEFAULT_HOST_TCP_PORT);

        if (!address.isValid())
        {
            QMessageBox::warning(this,
                                 tr("Warning"),
                                 tr("An invalid computer address was entered."),
                                 QMessageBox::Ok);
            combo_address->setFocus();
            return;
        }

        client_config.address_or_id = address.host();
        client_config.port = address.port();
    }
    else
    {
        LOG(LS_INFO) << "Relay connection selected";
        client_config.address_or_id = current_address.toStdU16String();
    }

    client_config.session_type = static_cast<proto::SessionType>(
        ui.combo_session_type->currentData().toInt());
    client_config.router_config = router_config_;

    int current_index = combo_address->findText(current_address);
    if (current_index != -1)
        combo_address->removeItem(current_index);

    combo_address->insertItem(0, current_address);
    combo_address->setCurrentIndex(0);

    state_.history.clear();
    for (int i = 0; i < std::min(combo_address->count(), 15); ++i)
        state_.history.append(combo_address->itemText(i));

    client::SessionWindow* session_window = nullptr;

    switch (client_config.session_type)
    {
        case proto::SESSION_TYPE_DESKTOP_MANAGE:
        {
            session_window = new client::QtDesktopWindow(
                state_.session_type, state_.desktop_manage_config);
        }
        break;

        case proto::SESSION_TYPE_DESKTOP_VIEW:
        {
            session_window = new client::QtDesktopWindow(
                state_.session_type, state_.desktop_view_config);
        }
        break;

        case proto::SESSION_TYPE_FILE_TRANSFER:
            session_window = new client::QtFileManagerWindow();
            break;

        default:
            NOTREACHED();
            break;
    }

    if (!session_window)
        return;

    session_window->setAttribute(Qt::WA_DeleteOnClose);
    if (!session_window->connectToHost(client_config))
    {
        session_window->close();
    }
    else
    {
        accept();
        close();
    }
}

void FastConnectDialog::readState()
{
    QDataStream stream(Application::instance()->settings().fastConnectConfig(address_book_guid_));
    stream.setVersion(QDataStream::Qt_5_12);

    int session_type;
    QByteArray desktop_manage_config;
    QByteArray desktop_view_config;

    stream >> state_.history >> session_type >> desktop_manage_config >> desktop_view_config;

    if (session_type != 0)
        state_.session_type = static_cast<proto::SessionType>(session_type);
    else
        state_.session_type = proto::SESSION_TYPE_DESKTOP_MANAGE;

    if (!desktop_manage_config.isEmpty())
    {
        state_.desktop_manage_config.ParseFromArray(
            desktop_manage_config.data(), desktop_manage_config.size());
    }
    else
    {
        state_.desktop_manage_config = client::ConfigFactory::defaultDesktopManageConfig();
    }

    if (!desktop_view_config.isEmpty())
    {
        state_.desktop_view_config.ParseFromArray(
            desktop_view_config.data(), desktop_view_config.size());
    }
    else
    {
        state_.desktop_view_config = client::ConfigFactory::defaultDesktopViewConfig();
    }
}

void FastConnectDialog::writeState()
{
    QByteArray buffer;

    {
        int session_type = static_cast<int>(state_.session_type);
        QByteArray desktop_manage_config =
            QByteArray::fromStdString(state_.desktop_manage_config.SerializeAsString());
        QByteArray desktop_view_config =
            QByteArray::fromStdString(state_.desktop_view_config.SerializeAsString());

        QDataStream stream(&buffer, QIODevice::WriteOnly);
        stream.setVersion(QDataStream::Qt_5_12);

        stream << state_.history << session_type << desktop_manage_config << desktop_view_config;
    }

    Application::instance()->settings().setFastConnectConfig(address_book_guid_, buffer);
}

} // namespace console
