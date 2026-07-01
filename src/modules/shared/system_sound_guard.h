#pragma once

#include <QtGlobal>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace panthera::modules {

class ScopedSystemBeepMute final {
public:
    ScopedSystemBeepMute()
    {
#ifdef Q_OS_WIN
        BOOL beepEnabled = FALSE;
        if (SystemParametersInfoW(SPI_GETBEEP, 0, &beepEnabled, 0)) {
            m_wasEnabled = beepEnabled != FALSE;
            if (m_wasEnabled && SystemParametersInfoW(SPI_SETBEEP, FALSE, nullptr, 0)) {
                m_restoreRequired = true;
            }
        }
#endif
    }

    ~ScopedSystemBeepMute()
    {
#ifdef Q_OS_WIN
        if (m_restoreRequired) {
            SystemParametersInfoW(SPI_SETBEEP, TRUE, nullptr, 0);
        }
#endif
    }

    ScopedSystemBeepMute(const ScopedSystemBeepMute&) = delete;
    ScopedSystemBeepMute& operator=(const ScopedSystemBeepMute&) = delete;

private:
    bool m_wasEnabled {false};
    bool m_restoreRequired {false};
};

}  // namespace panthera::modules
