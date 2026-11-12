// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <memory>

#include <QWidget>

class QAction;
class QCheckBox;
class QHideEvent;
class QLabel;
class QShowEvent;
class QSlider;
class QTimer;

namespace InputCommon {
class InputSubsystem;
}

/// Non-modal TAS input panel: live status of the TAS recording/playback,
/// a view of the current per-frame input and a manual input editor that
/// injects buttons/sticks into the TAS input engine of player 1.
class TasWindow : public QWidget {
    Q_OBJECT

public:
    explicit TasWindow(std::shared_ptr<InputCommon::InputSubsystem> input_subsystem_,
                       QWidget* parent = nullptr);
    ~TasWindow() override;

    /// Returns a QAction that can be used to toggle visibility of this panel.
    QAction* toggleViewAction();

protected:
    void showEvent(QShowEvent* ev) override;
    void hideEvent(QHideEvent* ev) override;

private:
    /// Called by the update timer: refreshes status, input view and speed.
    void UpdateUi();

    void OnManualModeToggled(bool checked);
    void OnButtonToggled(int button, bool checked);
    void OnAxisChanged(int axis, int value);
    void ResetStick(int stick);

    void ConnectWidget();

    std::shared_ptr<InputCommon::InputSubsystem> input_subsystem;

    QAction* toggle_view_action = nullptr;
    QTimer* update_timer = nullptr;

    QLabel* status_label = nullptr;
    QLabel* input_label = nullptr;
    QLabel* speed_label = nullptr;

    QCheckBox* manual_checkbox = nullptr;
    std::array<QCheckBox*, 18> button_boxes{};
    std::array<QSlider*, 4> axis_sliders{};
    std::array<QLabel*, 4> axis_value_labels{};
};
