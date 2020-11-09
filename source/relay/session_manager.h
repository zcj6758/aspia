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

#ifndef RELAY__SESSION_MANAGER_H
#define RELAY__SESSION_MANAGER_H

#include "proto/relay_peer.pb.h"
#include "relay/pending_session.h"
#include "relay/session.h"
#include "relay/shared_pool.h"

#include <asio/high_resolution_timer.hpp>

namespace base {
class TaskRunner;
} // namespace base

namespace relay {

class SessionManager
    : public PendingSession::Delegate,
      public Session::Delegate
{
public:
    class Delegate
    {
    public:
        virtual ~Delegate() = default;

        virtual void onSessionFinished() = 0;
    };

    SessionManager(std::shared_ptr<base::TaskRunner> task_runner,
                   uint16_t port,
                   const std::chrono::minutes& idle_timeout);
    ~SessionManager();

    void start(std::unique_ptr<SharedPool> shared_pool, Delegate* delegate);

protected:
    // PendingSession::Delegate implementation.
    void onPendingSessionReady(
        PendingSession* session, const proto::PeerToRelay& message) override;
    void onPendingSessionFailed(PendingSession* session) override;

    // Session::Delegate implementation.
    void onSessionFinished(Session* session) override;

private:
    static void doAccept(SessionManager* self);
    static void doIdleTimeout(SessionManager* self, const std::error_code& error_code);
    void doIdleTimeoutImpl(const std::error_code& error_code);

    void removePendingSession(PendingSession* sessions);
    void removeSession(Session* session);

    std::shared_ptr<base::TaskRunner> task_runner_;

    asio::ip::tcp::acceptor acceptor_;
    std::vector<std::unique_ptr<PendingSession>> pending_sessions_;
    std::vector<std::unique_ptr<Session>> active_sessions_;

    const std::chrono::minutes idle_timeout_;
    asio::high_resolution_timer idle_timer_;

    std::unique_ptr<SharedPool> shared_pool_;
    Delegate* delegate_ = nullptr;

    DISALLOW_COPY_AND_ASSIGN(SessionManager);
};

} // namespace relay

#endif // RELAY__SESSION_MANAGER_H
