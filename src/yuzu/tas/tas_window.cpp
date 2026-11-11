// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QAction>
#include <QCheckBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QPushButton>
#include <QShowEvent>
#include <QHideEvent>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>

#include "common/settings.h"
#include "common/settings_enums.h"
#include "input_common/drivers/tas_input.h"
#include "input_common/main.h"
#include "yuzu/tas/tas_window.h"

namespace {

/// Usable buttons of the TAS engine: bit index (TasButton) + display name.
struct TasButtonDescriptor {
    int bit;
    const char* label;
};

// HOME and CAPTURE are intentionally excluded: the TAS engine disables them to
// avoid TAS scripts from triggering emulator hotkeys.
constexpr std::array<TasButtonDescriptor, 18> tas_button_descriptors{{
    TasButtonDescriptor{0, QT_TRANSLATE_NOOP("TasWindow", "A")},
    TasButtonDescriptor{1, QT_TRANSLATE_NOOP("TasWindow", "B")},
    TasButtonDescriptor{2, QT_TRANSLATE_NOOP("TasWindow", "X")},
    TasButtonDescriptor{3, QT_TRANSLATE_NOOP("TasWindow", "Y")},
    TasButtonDescriptor{4, QT_TRANSLATE_NOOP("TasWindow", "L-Stick Click")},
    TasButtonDescriptor{5, QT_TRANSLATE_NOOP("TasWindow", "R-Stick Click")},
    TasButtonDescriptor{6, QT_TRANSLATE_NOOP("TasWindow", "L")},
    TasButtonDescriptor{7, QT_TRANSLATE_NOOP("TasWindow", "R")},
    TasButtonDescriptor{8, QT_TRANSLATE_NOOP("TasWindow", "ZL")},
    TasButtonDescriptor{9, QT_TRANSLATE_NOOP("TasWindow", "ZR")},
    TasButtonDescriptor{10, QT_TRANSLATE_NOOP("TasWindow", "Plus (+)")},
    TasButtonDescriptor{11, QT_TRANSLATE_NOOP("TasWindow", "Minus (-)")},
    TasButtonDescriptor{12, QT_TRANSLATE_NOOP("TasWindow", "D-Pad Left")},
    TasButtonDescriptor{13, QT_TRANSLATE_NOOP("TasWindow", "D-Pad Up")},
    TasButtonDescriptor{14, QT_TRANSLATE_NOOP("TasWindow", "D-Pad Right")},
    TasButtonDescriptor{15, QT_TRANSLATE_NOOP("TasWindow", "D-Pad Down")},
    TasButtonDescriptor{16, QT_TRANSLATE_NOOP("TasWindow", "SL")},
    TasButtonDescriptor{17, QT_TRANSLATE_NOOP("TasWindow", "SR")},
}};

} // namespace

TasWindow::TasWindow(std::shared_ptr<InputCommon::InputSubsystem> input_subsystem_,
                     QWidget* parent)
    : QWidget(parent, Qt::Dialog), input_subsystem{std::move(input_subsystem_)} {
    setObjectName(QStringLiteral("TasInputPanel"));
    setWindowTitle(tr("TAS Input Panel"));
    resize(460, 640);
    setMinimumSize(400, 480);
    // Enable the maximize button
    setWindowFlags(windowFlags() | Qt::WindowMaximizeButtonHint);

    auto* root_layout = new QVBoxLayout(this);

    // --- Status section ------------------------------------------------------
    status_label = new QLabel(this);
    status_label->setWordWrap(true);
    QFont bold_font = status_label->font();
    bold_font.setBold(true);
    bold_font.setPointSizeF(bold_font.pointSizeF() * 1.2f);
    status_label->setFont(bold_font);
    status_label->setAlignment(Qt::AlignCenter);
    status_label->setText(tr("TAS is disabled"));

    input_label = new QLabel(this);
    input_label->setWordWrap(true);
    input_label->setAlignment(Qt::AlignCenter);

    speed_label = new QLabel(this);
    speed_label->setAlignment(Qt::AlignCenter);

    root_layout->addWidget(status_label);
    root_layout->addWidget(input_label);
    root_layout->addWidget(speed_label);
    root_layout->addSpacing(4);

    // --- Manual input section ------------------------------------------------
    auto* manual_group = new QGroupBox(tr("Manual input (Player 1)"), this);
    auto* manual_layout = new QVBoxLayout(manual_group);

    manual_checkbox = new QCheckBox(tr("Inject manual input into the game"), manual_group);
    manual_layout->addWidget(manual_checkbox);

    auto* manual_hint = new QLabel(
        tr("While active, the checkboxes and sticks below are fed directly to the game.\n"
           "Combine with Frame Advance and recording to craft inputs frame by frame."),
        manual_group);
    manual_hint->setWordWrap(true);
    manual_layout->addWidget(manual_hint);

    root_layout->addWidget(manual_group);

    // --- Buttons section -----------------------------------------------------
    auto* buttons_group = new QGroupBox(tr("Buttons"), this);
    auto* buttons_grid = new QGridLayout(buttons_group);
    for (std::size_t i = 0; i < tas_button_descriptors.size(); ++i) {
        const int bit = tas_button_descriptors[i].bit;
        auto* box = new QCheckBox(tr(tas_button_descriptors[i].label), buttons_group);
        buttons_grid->addWidget(box, static_cast<int>(i) / 3, static_cast<int>(i) % 3);
        button_boxes[i] = box;
        connect(box, &QCheckBox::toggled, this,
                [this, bit](bool checked) { OnButtonToggled(bit, checked); });
    }
    buttons_group->setLayout(buttons_grid);
    root_layout->addWidget(buttons_group);

    // --- Sticks section ------------------------------------------------------
    auto* sticks_layout = new QHBoxLayout();
    const std::array<const char*, 2> stick_names{
        {QT_TRANSLATE_NOOP("TasWindow", "Left stick"), QT_TRANSLATE_NOOP("TasWindow", "Right stick")}};
    const std::array<const char*, 4> axis_names{{QT_TRANSLATE_NOOP("TasWindow", "X (left/right)"),
                                                 QT_TRANSLATE_NOOP("TasWindow", "Y (down/up)"),
                                                 QT_TRANSLATE_NOOP("TasWindow", "X (left/right)"),
                                                 QT_TRANSLATE_NOOP("TasWindow", "Y (down/up)")}};
    for (int stick = 0; stick < 2; ++stick) {
        auto* stick_group = new QGroupBox(tr(stick_names[stick]), this);
        auto* stick_layout = new QVBoxLayout(stick_group);
        for (int axis_in_stick = 0; axis_in_stick < 2; ++axis_in_stick) {
            const int axis = stick * 2 + axis_in_stick;
            auto* row = new QHBoxLayout();
            auto* axis_label = new QLabel(tr(axis_names[axis]), stick_group);
            axis_label->setFixedWidth(86);
            auto* slider = new QSlider(Qt::Horizontal, stick_group);
            slider->setRange(-100, 100);
            slider->setValue(0);
            slider->setTickPosition(QSlider::TicksBelow);
            slider->setTickInterval(25);
            auto* value_label = new QLabel(QStringLiteral("0"), stick_group);
            value_label->setFixedWidth(40);
            value_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            row->addWidget(axis_label);
            row->addWidget(slider, 1);
            row->addWidget(value_label);
            stick_layout->addLayout(row);

            axis_sliders[axis] = slider;
            axis_value_labels[axis] = value_label;
            connect(slider, &QSlider::valueChanged, this,
                    [this, axis, value_label](int value) {
                        value_label->setText(QString::number(value));
                        OnAxisChanged(axis, value);
                    });
        }
        auto* reset_button =
            new QPushButton(tr("Recentre"), stick_group);
        connect(reset_button, &QPushButton::clicked, this, [this, stick] { ResetStick(stick); });
        stick_layout->addWidget(reset_button);
        stick_group->setLayout(stick_layout);
        sticks_layout->addWidget(stick_group);
    }
    root_layout->addLayout(sticks_layout);
    root_layout->addStretch(1);

    // --- Update timer --------------------------------------------------------
    update_timer = new QTimer(this);
    update_timer->setInterval(100);
    connect(update_timer, &QTimer::timeout, this, &TasWindow::UpdateUi);

    ConnectWidget();
    UpdateUi();
}

TasWindow::~TasWindow() = default;

void TasWindow::ConnectWidget() {
    connect(manual_checkbox, &QCheckBox::toggled, this, &TasWindow::OnManualModeToggled);
}

QAction* TasWindow::toggleViewAction() {
    if (toggle_view_action == nullptr) {
        toggle_view_action = new QAction(tr("TAS &Input Panel..."), this);
        toggle_view_action->setCheckable(true);
        toggle_view_action->setChecked(isVisible());
        connect(toggle_view_action, &QAction::toggled, this, &TasWindow::setVisible);
    }

    return toggle_view_action;
}

void TasWindow::showEvent(QShowEvent* ev) {
    if (toggle_view_action) {
        toggle_view_action->setChecked(isVisible());
    }
    if (update_timer) {
        update_timer->start();
    }
    QWidget::showEvent(ev);
}

void TasWindow::hideEvent(QHideEvent* ev) {
    if (toggle_view_action) {
        toggle_view_action->setChecked(isVisible());
    }
    if (update_timer) {
        update_timer->stop();
    }
    QWidget::hideEvent(ev);
}

void TasWindow::UpdateUi() {
    auto* tas = input_subsystem->GetTas();
    if (tas == nullptr) {
        return;
    }

    const auto [state, current_frame, lengths] = tas->GetStatus();

    QString status;
    if (!Settings::values.tas_enable.GetValue()) {
        status = tr("TAS is disabled — enable it in Tools → TAS → Enable TAS");
    } else if (tas->GetManualMode()) {
        if (state == InputCommon::TasInput::TasState::Recording) {
            status = tr("MANUAL — recording frame %1").arg(lengths[0]);
        } else {
            status = tr("MANUAL INPUT ACTIVE");
        }
    } else {
        switch (state) {
        case InputCommon::TasInput::TasState::Recording:
            status = tr("RECORDING — frame %1").arg(lengths[0]);
            break;
        case InputCommon::TasInput::TasState::Running:
            status = tr("PLAYING — frame %1 / %2").arg(current_frame).arg(lengths[0]);
            break;
        case InputCommon::TasInput::TasState::Stopped:
        default:
            status = tr("IDLE — script length: %1 frames").arg(lengths[0]);
            break;
        }
    }
    status_label->setText(status);

    // Current input view: manual input in manual mode, otherwise the last
    // input sampled from the physical controller.
    const auto command =
        tas->GetManualMode() ? tas->GetManualInput() : tas->GetLastInput();
    QStringList pressed_buttons;
    for (const auto& descriptor : tas_button_descriptors) {
        if ((command.buttons & (1ULL << descriptor.bit)) != 0) {
            pressed_buttons.append(tr(descriptor.label));
        }
    }
    const QString axes_text = tr("L(%1, %2)  R(%3, %4)")
                                  .arg(command.l_axis.x, 0, 'f', 2)
                                  .arg(command.l_axis.y, 0, 'f', 2)
                                  .arg(command.r_axis.x, 0, 'f', 2)
                                  .arg(command.r_axis.y, 0, 'f', 2);
    input_label->setText(tr("Input: %1 | %2")
                             .arg(pressed_buttons.isEmpty() ? tr("none")
                                                            : pressed_buttons.join(QStringLiteral(" + ")),
                                  axes_text));

    // Emulation speed view.
    QString speed_text;
    if (!Settings::values.use_speed_limit.GetValue()) {
        speed_text = tr("Speed: Unlimited");
    } else {
        switch (Settings::values.current_speed_mode.GetValue()) {
        case Settings::SpeedMode::Turbo:
            speed_text = tr("Speed: %1% (turbo)").arg(Settings::values.turbo_speed_limit.GetValue());
            break;
        case Settings::SpeedMode::Slow:
            speed_text = tr("Speed: %1% (slow)").arg(Settings::values.slow_speed_limit.GetValue());
            break;
        case Settings::SpeedMode::Standard:
        default:
            speed_text = tr("Speed: %1%").arg(Settings::values.speed_limit.GetValue());
            break;
        }
    }
    speed_label->setText(speed_text);
}

void TasWindow::OnManualModeToggled(bool checked) {
    auto* tas = input_subsystem->GetTas();
    if (tas == nullptr) {
        return;
    }
    tas->SetManualMode(checked);
    UpdateUi();
}

void TasWindow::OnButtonToggled(int button, bool checked) {
    auto* tas = input_subsystem->GetTas();
    if (tas == nullptr) {
        return;
    }
    tas->ManualSetButton(button, checked);
}

void TasWindow::OnAxisChanged(int axis, int value) {
    auto* tas = input_subsystem->GetTas();
    if (tas == nullptr) {
        return;
    }
    tas->ManualSetAxis(axis, static_cast<f32>(value) / 100.0f);
}

void TasWindow::ResetStick(int stick) {
    const int base = stick * 2;
    if (axis_sliders[base] != nullptr) {
        axis_sliders[base]->setValue(0);
    }
    if (axis_sliders[base + 1] != nullptr) {
        axis_sliders[base + 1]->setValue(0);
    }
}
