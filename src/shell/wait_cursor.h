#pragma once

#include <QGuiApplication>

namespace jsontitan::shell {

// RAII override-cursor guard for synchronous long operations (save, export,
// union load). restore() may be called early — e.g. before showing a modal
// error dialog — after which the destructor is a no-op.
class WaitCursorGuard {
public:
    WaitCursorGuard() { QGuiApplication::setOverrideCursor(Qt::WaitCursor); }
    ~WaitCursorGuard() { restore(); }

    WaitCursorGuard(const WaitCursorGuard&) = delete;
    WaitCursorGuard& operator=(const WaitCursorGuard&) = delete;

    void restore() {
        if (m_active) {
            QGuiApplication::restoreOverrideCursor();
            m_active = false;
        }
    }

private:
    bool m_active = true;
};

}  // namespace jsontitan::shell
