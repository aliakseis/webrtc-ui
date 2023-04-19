#pragma once

#include <QString>

inline const auto SETTING_SESSION_ID = QStringLiteral("sessionId");

inline const auto SETTING_USE_TURN = QStringLiteral("useTURN");
inline const auto SETTING_TURN = QStringLiteral("TURN");

inline const auto SETTING_VIDEO_LAUNCH_LINE = QStringLiteral("videoLaunchLine");
inline const auto SETTING_AUDIO_LAUNCH_LINE = QStringLiteral("audioLaunchLine");

inline const auto VIDEO_LAUNCH_LINE_DEFAULT = QStringLiteral("autovideosrc");
inline const auto AUDIO_LAUNCH_LINE_DEFAULT = QStringLiteral("autoaudiosrc");

inline const auto SETTING_DO_SAVE = QStringLiteral("doSave");
inline const auto SETTING_SAVE_PATH = QStringLiteral("savePath");

int getSliceDurationSecs(); // zero if no slices
