//
// Aspia Project
// Copyright (C) 2020 Dmitry Chapyshev <dmitry@aspia.ru>
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

#include "relay/pending_session.h"

#include "base/endian_util.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/strings/unicode.h"

#include <asio/read.hpp>

namespace relay {

namespace {

constexpr std::chrono::seconds kTimeout{ 30 };

} // namespace

PendingSession::PendingSession(std::shared_ptr<base::TaskRunner> task_runner,
                               asio::ip::tcp::socket&& socket,
                               Delegate* delegate)
    : delegate_(delegate),
      timer_(std::move(task_runner)),
      socket_(std::move(socket))
{
    // Nothing
}

PendingSession::~PendingSession()
{
    stop();
}

void PendingSession::start()
{
    LOG(LS_INFO) << "Starting pending session";

    timer_.start(kTimeout, std::bind(
        &PendingSession::onErrorOccurred, this, FROM_HERE, std::error_code()));
    PendingSession::doReadMessage(this);
}

void PendingSession::stop()
{
    if (!delegate_)
        return;

    delegate_ = nullptr;
    timer_.stop();

    std::error_code ignored_code;
    socket_.cancel(ignored_code);
    socket_.close(ignored_code);
}

void PendingSession::setIdentify(uint32_t key_id, const base::ByteArray& secret)
{
    secret_ = secret;
    key_id_ = key_id;
}

bool PendingSession::isPeerFor(const PendingSession& other) const
{
    if (secret_.empty() || other.secret_.empty())
        return false;

    return key_id_ == other.key_id_ && base::equals(secret_, other.secret_);
}

asio::ip::tcp::socket PendingSession::takeSocket()
{
    return std::move(socket_);
}

// static
void PendingSession::doReadMessage(PendingSession* session)
{
    LOG(LS_INFO) << "Reading message size";

    asio::async_read(session->socket_,
                     asio::buffer(&session->buffer_size_, sizeof(uint32_t)),
                     [session](const std::error_code& error_code, size_t bytes_transferred)
    {
        if (error_code)
        {
            if (error_code != asio::error::operation_aborted)
                session->onErrorOccurred(FROM_HERE, error_code);
            return;
        }

        session->buffer_size_ = base::EndianUtil::fromBig(session->buffer_size_);
        if (!session->buffer_size_ || session->buffer_size_ > session->buffer_.size())
        {
            session->onErrorOccurred(FROM_HERE, error_code);
            return;
        }

        LOG(LS_INFO) << "Reading message";

        asio::async_read(session->socket_,
                         asio::buffer(session->buffer_.data(), session->buffer_size_),
                         [session](const std::error_code& error_code, size_t bytes_transferred)
        {
            if (error_code)
            {
                if (error_code != asio::error::operation_aborted)
                    session->onErrorOccurred(FROM_HERE, error_code);
                return;
            }

            session->onMessage();
        });
    });
}

void PendingSession::onErrorOccurred(
    const base::Location& location, const std::error_code& error_code)
{
    LOG(LS_ERROR) << "Connection error: " << base::utf16FromLocal8Bit(error_code.message())
                  << " (" << location.toString() << ")";
    if (delegate_)
        delegate_->onPendingSessionFailed(this);

    stop();
}

void PendingSession::onMessage()
{
    proto::PeerToRelay message;
    if (!message.ParseFromArray(buffer_.data(), buffer_.size()))
    {
        onErrorOccurred(FROM_HERE, std::error_code());
        return;
    }

    LOG(LS_INFO) << "Pending session ready";

    if (delegate_)
        delegate_->onPendingSessionReady(this, message);
}

} // namespace relay
