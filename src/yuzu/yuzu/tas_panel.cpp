// SPDX-FileCopyrightText: Copyright 2026 Lichii-TAS contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Based on code from the yuzu Emulator Project (GPL-2.0-or-later) and the Eden
// Emulator Project (GPL-3.0-or-later).

#include <algorithm>

#include <QFont>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QPainter>
#include <QPen>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include "common/settings.h"
#include "input_common/main.h"
#include "qt_common/qt_common.h"
#include "qt_common/render/emu_thread.h"
#include "tas_panel.h"

namespace {

constexpr QSize ButtonLabelSize = {34, 24};

} // namespace

TasStickWidget::TasStickWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(96, 96);
}

void TasStickWidget::SetValues(float x, float y) {
    pos_x = std::clamp(x, -1.0f, 1.0f);
    pos_y = std::clamp(y, -1.0f, 1.0f);
    update();
}

void TasStickWidget::paintEvent(QPaintEvent* event) {
    QWidget::paintEvent(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const int side = std::min(width(), height());
    const QPointF center(width() / 2.0, height() / 2.0);
    const double radius = side / 2.0 - 4.0;

    // Stick range circle
    painter.setPen(QPen(palette().mid().color(), 1));
    painter.drawEllipse(center, radius, radius);
    painter.drawLine(QPointF(center.x() - radius, center.y()),
                     QPointF(center.x() + radius, center.y()));
    painter.drawLine(QPointF(center.x(), center.y() - radius),
                     QPointF(center.x(), center.y() + radius));

    // Stick position
    const QPointF stick_pos(center.x() + pos_x * radius, center.y() - pos_y * radius);
    painter.setPen(QPen(palette().highlight().color(), 2));
    painter.setBrush(palette().highlight());
    painter.drawEllipse(stick_pos, 5.0, 5.0);
}

const std::array<TasPanel::ButtonInfo, 18> TasPanel::button_info{{
    {"A", static_cast<u64>(InputCommon::TasInput::TasButton::BUTTON_A)},
    {"B", static_cast<u64>(InputCommon::TasInput::TasButton::BUTTON_B)},
    {"X", static_cast<u64>(InputCommon::TasInput::TasButton::BUTTON_X)},
    {"Y", static_cast<u64>(InputCommon::TasInput::TasButton::BUTTON_Y)},
    {"L", static_cast<u64>(InputCommon::TasInput::TasButton::TRIGGER_L)},
    {"R", static_cast<u64>(InputCommon::TasInput::TasButton::TRIGGER_R)},
    {"ZL", static_cast<u64>(InputCommon::TasInput::TasButton::TRIGGER_ZL)},
    {"ZR", static_cast<u64>(InputCommon::TasInput::TasButton::TRIGGER_ZR)},
    {"+", static_cast<u64>(InputCommon::TasInput::TasButton::BUTTON_PLUS)},
    {"-", static_cast<u64>(InputCommon::TasInput::TasButton::BUTTON_MINUS)},
    {"L3", static_cast<u64>(InputCommon::TasInput::TasButton::STICK_L)},
    {"R3", static_cast<u64>(InputCommon::TasInput::TasButton::STICK_R)},
    {"SL", static_cast<u64>(InputCommon::TasInput::TasButton::BUTTON_SL)},
    {"SR", static_cast<u64>(InputCommon::TasInput::TasButton::BUTTON_SR)},
    {"LEFT", static_cast<u64>(InputCommon::TasInput::TasButton::BUTTON_LEFT)},
    {"UP", static_cast<u64>(InputCommon::TasInput::TasButton::BUTTON_UP)},
    {"RIGHT", static_cast<u64>(InputCommon::TasInput::TasButton::BUTTON_RIGHT)},
    {"DOWN", static_cast<u64>(InputCommon::TasInput::TasButton::BUTTON_DOWN)},
}};

TasPanel::TasPanel(std::shared_ptr<InputCommon::InputSubsystem> input_subsystem_,
                   QWidget* parent)
    : QWidget(parent, Qt::Tool | Qt::WindowStaysOnTopHint), input_subsystem{input_subsystem_} {
    setWindowTitle(tr("TAS Panel"));
    setMinimumWidth(430);

    auto* main_layout = new QVBoxLayout(this);

    // --- Status row -----------------------------------------------------------
    auto* status_layout = new QHBoxLayout();
    status_label = new QLabel(tr("IDLE"));
    QFont bold_font = status_label->font();
    bold_font.setBold(true);
    bold_font.setPointSize(bold_font.pointSize() + 2);
    status_label->setFont(bold_font);
    frame_label = new QLabel(tr("Frame: 0"));
    pause_label = new QLabel(tr("PAUSED"));
    pause_label->setStyleSheet(QStringLiteral("color: orange; font-weight: bold;"));
    pause_label->hide();

    status_layout->addWidget(status_label);
    status_layout->addWidget(frame_label, 1);
    status_layout->addWidget(pause_label);
    main_layout->addLayout(status_layout);

    // --- Buttons --------------------------------------------------------------
    auto* buttons_group = new QGroupBox(tr("Inputs of the current frame (Player 1)"), this);
    auto* buttons_grid = new QGridLayout(buttons_group);
    for (size_t i = 0; i < button_labels.size(); ++i) {
        auto* label = new QLabel(QString::fromUtf8(button_info[i].name), buttons_group);
        label->setAlignment(Qt::AlignCenter);
        label->setMinimumSize(ButtonLabelSize);
        label->setFrameShape(QFrame::StyledPanel);
        SetButtonActive(i, false);
        buttons_grid->addWidget(label, static_cast<int>(i / 9), static_cast<int>(i % 9));
        button_labels[i] = label;
    }
    main_layout->addWidget(buttons_group);

    // --- Sticks ---------------------------------------------------------------
    auto* sticks_layout = new QHBoxLayout();
    auto* left_group = new QGroupBox(tr("Left stick"), this);
    auto* left_layout = new QVBoxLayout(left_group);
    left_stick = new TasStickWidget(left_group);
    left_layout->addWidget(left_stick);
    auto* right_group = new QGroupBox(tr("Right stick"), this);
    auto* right_layout = new QVBoxLayout(right_group);
    right_stick = new TasStickWidget(right_group);
    right_layout->addWidget(right_stick);
    sticks_layout->addWidget(left_group);
    sticks_layout->addWidget(right_group);
    main_layout->addLayout(sticks_layout);

    // --- Speed ----------------------------------------------------------------
    auto* speed_layout = new QHBoxLayout();
    auto* speed_caption = new QLabel(tr("Emulation speed:"), this);
    speed_combo = new QComboBox(this);
    for (int percent : {5, 10, 25, 50, 100}) {
        speed_combo->addItem(QString::number(percent) + QStringLiteral("%"), percent);
    }
    speed_combo->setCurrentIndex(4); // 100%
    speed_label = new QLabel(this);
    speed_layout->addWidget(speed_caption);
    speed_layout->addWidget(speed_combo, 1);
    speed_layout->addWidget(speed_label);
    main_layout->addLayout(speed_layout);

    // --- Commands -------------------------------------------------------------
    auto* command_layout = new QGridLayout();
    auto* record_button = new QPushButton(tr("Record"), this);
    auto* play_button = new QPushButton(tr("Play / Stop"), this);
    auto* reset_button = new QPushButton(tr("Reset"), this);
    auto* frame_advance_button = new QPushButton(tr("Frame +1"), this);
    auto* seek_button = new QPushButton(tr("Seek & Re-record..."), this);
    auto* new_recording_button = new QPushButton(tr("Restart && Record"), this);
    auto* play_start_button = new QPushButton(tr("Restart && Play"), this);
    auto* pause_button = new QPushButton(tr("Pause / Resume"), this);

    command_layout->addWidget(record_button, 0, 0);
    command_layout->addWidget(play_button, 0, 1);
    command_layout->addWidget(reset_button, 0, 2);
    command_layout->addWidget(frame_advance_button, 0, 3);
    command_layout->addWidget(seek_button, 1, 0);
    command_layout->addWidget(new_recording_button, 1, 1);
    command_layout->addWidget(play_start_button, 1, 2);
    command_layout->addWidget(pause_button, 1, 3);
    main_layout->addLayout(command_layout);

    // --- Wiring ---------------------------------------------------------------
    connect(record_button, &QPushButton::clicked, this, &TasPanel::RecordClicked);
    connect(play_button, &QPushButton::clicked, this, &TasPanel::PlayClicked);
    connect(reset_button, &QPushButton::clicked, this, &TasPanel::ResetClicked);
    connect(frame_advance_button, &QPushButton::clicked, this, &TasPanel::FrameAdvanceClicked);
    connect(seek_button, &QPushButton::clicked, this, &TasPanel::SeekClicked);
    connect(new_recording_button, &QPushButton::clicked, this, &TasPanel::NewRecordingClicked);
    connect(play_start_button, &QPushButton::clicked, this, &TasPanel::PlayFromStartClicked);
    connect(pause_button, &QPushButton::clicked, this, &TasPanel::PauseClicked);
    connect(speed_combo, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index >= 0) {
            emit SpeedPresetChanged(speed_combo->itemData(index).toInt());
        }
    });

    refresh_timer = new QTimer(this);
    connect(refresh_timer, &QTimer::timeout, this, &TasPanel::Refresh);
    refresh_timer->start(33); // ~30 Hz
    Refresh();
}

TasPanel::~TasPanel() = default;

void TasPanel::SetButtonActive(size_t index, bool active) {
    if (index >= button_labels.size() || button_labels[index] == nullptr) {
        return;
    }
    if (active) {
        button_labels[index]->setStyleSheet(
            QStringLiteral("background-color: #2d7530; color: white; font-weight: bold;"));
    } else {
        button_labels[index]->setStyleSheet(QString());
    }
}

void TasPanel::Refresh() {
    const auto snapshot = input_subsystem->GetTas()->GetSnapshot();

    switch (snapshot.state) {
    case InputCommon::TasInput::TasState::Recording:
        status_label->setText(tr("RECORDING"));
        status_label->setStyleSheet(QStringLiteral("color: #d32f2f;"));
        break;
    case InputCommon::TasInput::TasState::Running:
        status_label->setText(tr("PLAYING"));
        status_label->setStyleSheet(QStringLiteral("color: #2d7530;"));
        break;
    case InputCommon::TasInput::TasState::Stopped:
    default:
        status_label->setText(tr("IDLE"));
        status_label->setStyleSheet(QString());
        break;
    }

    QString frame_text = tr("Frame: %1").arg(snapshot.current_frame);
    if (snapshot.state == InputCommon::TasInput::TasState::Recording) {
        frame_text += tr("  (rec)");
    } else if (snapshot.script_length > 0) {
        frame_text += tr(" / %1").arg(snapshot.script_length);
    }
    if (snapshot.is_stepping) {
        frame_text += tr("  [stepping]");
    }
    frame_label->setText(frame_text);

    const bool paused =
        QtCommon::emu_thread != nullptr && !QtCommon::emu_thread->IsRunning();
    pause_label->setVisible(paused);

    switch (Settings::SpeedMode(Settings::values.current_speed_mode.GetValue())) {
    case Settings::SpeedMode::Slow:
        speed_label->setText(
            tr("SLOW %1%").arg(Settings::values.slow_speed_limit.GetValue()));
        break;
    case Settings::SpeedMode::Turbo:
        speed_label->setText(
            tr("TURBO %1%").arg(Settings::values.turbo_speed_limit.GetValue()));
        break;
    case Settings::SpeedMode::Standard:
    default:
        if (Settings::values.use_speed_limit.GetValue()) {
            speed_label->setText(tr("LIMIT %1%").arg(Settings::values.speed_limit.GetValue()));
        } else {
            speed_label->setText(tr("UNLOCKED"));
        }
        break;
    }

    for (size_t i = 0; i < button_labels.size(); ++i) {
        SetButtonActive(i, (snapshot.buttons & button_info[i].mask) != 0);
    }
    left_stick->SetValues(snapshot.l_axis.x, snapshot.l_axis.y);
    right_stick->SetValues(snapshot.r_axis.x, snapshot.r_axis.y);
}
