// SPDX-FileCopyrightText: Copyright 2026 Lichii-TAS contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Based on code from the yuzu Emulator Project (GPL-2.0-or-later) and the Eden
// Emulator Project (GPL-3.0-or-later).

#pragma once

#include <array>
#include <memory>

#include <QComboBox>
#include <QLabel>
#include <QWidget>

#include "common/common_types.h"
#include "input_common/drivers/tas_input.h"

class QPushButton;
class QTimer;

namespace InputCommon {
class InputSubsystem;
}

/// Small custom widget that draws an analog stick position inside a circle.
class TasStickWidget : public QWidget {
    Q_OBJECT

public:
    explicit TasStickWidget(QWidget* parent = nullptr);

    void SetValues(float x, float y);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    float pos_x{};
    float pos_y{};
};

/**
 * Floating TAS control panel: shows the current state (recording / playing /
 * idle), the frame counter and the inputs registered on the current frame
 * (buttons + both sticks), and provides quick access to the TAS commands
 * (record, playback, frame advance, seek & re-record, speed presets).
 *
 * The panel is passive: it polls a thread-safe snapshot from the Tas engine
 * and forwards user interaction to MainWindow via signals.
 */
class TasPanel : public QWidget {
    Q_OBJECT

public:
    explicit TasPanel(std::shared_ptr<InputCommon::InputSubsystem> input_subsystem_,
                      QWidget* parent = nullptr);
    ~TasPanel() override;

signals:
    void RecordClicked();
    void PlayClicked();
    void ResetClicked();
    void FrameAdvanceClicked();
    void SeekClicked();
    void NewRecordingClicked();
    void PlayFromStartClicked();
    void PauseClicked();
    void SpeedPresetChanged(int percent);

private:
    void Refresh();
    void SetButtonActive(size_t index, bool active);

    std::shared_ptr<InputCommon::InputSubsystem> input_subsystem;

    QTimer* refresh_timer = nullptr;
    QLabel* status_label = nullptr;
    QLabel* frame_label = nullptr;
    QLabel* pause_label = nullptr;
    QLabel* speed_label = nullptr;
    TasStickWidget* left_stick = nullptr;
    TasStickWidget* right_stick = nullptr;
    QComboBox* speed_combo = nullptr;
    std::array<QLabel*, 18> button_labels{};

    struct ButtonInfo {
        const char* name;
        u64 mask;
    };
    static const std::array<ButtonInfo, 18> button_info;
};
