// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstring>
#include <sstream>
#include <fmt/ranges.h>

#include "common/fs/file.h"
#include "common/fs/fs_types.h"
#include "common/fs/path_util.h"
#include "common/logging.h"
#include "common/settings.h"
#include "input_common/drivers/tas_input.h"

namespace InputCommon::TasInput {

enum class Tas::TasAxis : u8 {
    StickX,
    StickY,
    SubstickX,
    SubstickY,
    Undefined,
};

// Supported keywords and buttons from a TAS file
constexpr std::array<std::pair<std::string_view, TasButton>, 18> text_to_tas_button = {
    std::pair{"KEY_A", TasButton::BUTTON_A},
    {"KEY_B", TasButton::BUTTON_B},
    {"KEY_X", TasButton::BUTTON_X},
    {"KEY_Y", TasButton::BUTTON_Y},
    {"KEY_LSTICK", TasButton::STICK_L},
    {"KEY_RSTICK", TasButton::STICK_R},
    {"KEY_L", TasButton::TRIGGER_L},
    {"KEY_R", TasButton::TRIGGER_R},
    {"KEY_PLUS", TasButton::BUTTON_PLUS},
    {"KEY_MINUS", TasButton::BUTTON_MINUS},
    {"KEY_DLEFT", TasButton::BUTTON_LEFT},
    {"KEY_DUP", TasButton::BUTTON_UP},
    {"KEY_DRIGHT", TasButton::BUTTON_RIGHT},
    {"KEY_DDOWN", TasButton::BUTTON_DOWN},
    {"KEY_SL", TasButton::BUTTON_SL},
    {"KEY_SR", TasButton::BUTTON_SR},
    // These buttons are disabled to avoid TAS input from activating hotkeys
    // {"KEY_CAPTURE", TasButton::BUTTON_CAPTURE},
    // {"KEY_HOME", TasButton::BUTTON_HOME},
    {"KEY_ZL", TasButton::TRIGGER_ZL},
    {"KEY_ZR", TasButton::TRIGGER_ZR},
};

Tas::Tas(std::string input_engine_) : InputEngine(std::move(input_engine_)) {
    for (size_t player_index = 0; player_index < PLAYER_NUMBER; player_index++) {
        PadIdentifier identifier{
            .guid = Common::UUID{},
            .port = player_index,
            .pad = 0,
        };
        PreSetController(identifier);
    }
    ClearInput();
    if (!Settings::values.tas_enable) {
        needs_reset = true;
        return;
    }
    LoadTasFiles();
}

Tas::~Tas() {
    Stop();
}

void Tas::LoadTasFiles() {
    script_length = 0;
    for (size_t i = 0; i < commands.size(); i++) {
        LoadTasFile(i, 0);
        if (commands[i].size() > script_length) {
            script_length = commands[i].size();
        }
    }
}

void Tas::LoadTasFile(size_t player_index, size_t file_index) {
    commands[player_index].clear();

    std::string file = Common::FS::ReadStringFromFile(
        Common::FS::GetEdenPath(Common::FS::EdenPath::TASDir) /
            fmt::format("script{}-{}.txt", file_index, player_index + 1),
        Common::FS::FileType::BinaryFile);
    std::istringstream command_line(file);
    std::string line;
    int frame_no = 0;
    while (std::getline(command_line, line, '\n')) {
        if (line.empty()) {
            continue;
        }

        std::vector<std::string> seg_list;
        {
            std::istringstream line_stream(line);
            std::string segment;
            while (std::getline(line_stream, segment, ' ')) {
                seg_list.push_back(std::move(segment));
            }
        }

        if (seg_list.size() < 4) {
            continue;
        }

        try {
            const auto num_frames = std::stoi(seg_list[0]);
            while (frame_no < num_frames) {
                commands[player_index].emplace_back();
                frame_no++;
            }
        } catch (const std::invalid_argument&) {
            LOG_ERROR(Input, "Invalid argument: '{}' at command {}", seg_list[0], frame_no);
        } catch (const std::out_of_range&) {
            LOG_ERROR(Input, "Out of range: '{}' at command {}", seg_list[0], frame_no);
        }

        TASCommand command = {
            .buttons = ReadCommandButtons(seg_list[1]),
            .l_axis = ReadCommandAxis(seg_list[2]),
            .r_axis = ReadCommandAxis(seg_list[3]),
        };
        commands[player_index].push_back(command);
        frame_no++;
    }
    LOG_INFO(Input, "TAS file loaded! {} frames", frame_no);
}

void Tas::WriteTasFile(std::u8string_view file_name) {
    std::string output_text;
    for (size_t frame = 0; frame < record_commands.size(); frame++) {
        const TASCommand& line = record_commands[frame];
        output_text += fmt::format("{} {} {} {}\n", frame, WriteCommandButtons(line.buttons),
                                   WriteCommandAxis(line.l_axis), WriteCommandAxis(line.r_axis));
    }

    const auto tas_file_name = Common::FS::GetEdenPath(Common::FS::EdenPath::TASDir) / file_name;
    const auto bytes_written =
        Common::FS::WriteStringToFile(tas_file_name, Common::FS::FileType::TextFile, output_text);
    if (bytes_written == output_text.size()) {
        LOG_INFO(Input, "TAS file written to file!");
    } else {
        LOG_ERROR(Input, "Writing the TAS-file has failed! {} / {} bytes written", bytes_written,
                  output_text.size());
    }
}

void Tas::RecordInput(u64 buttons, TasAnalog left_axis, TasAnalog right_axis) {
    last_input = {
        .buttons = buttons,
        .l_axis = left_axis,
        .r_axis = right_axis,
    };
}

std::tuple<TasState, size_t, std::array<size_t, PLAYER_NUMBER>> Tas::GetStatus() const {
    TasState state;
    std::array<size_t, PLAYER_NUMBER> lengths{0};
    if (is_recording) {
        lengths[0] = record_commands.size();
        return {TasState::Recording, record_commands.size(), lengths};
    }

    if (is_running) {
        state = TasState::Running;
    } else {
        state = TasState::Stopped;
    }

    for (size_t i = 0; i < PLAYER_NUMBER; i++) {
        lengths[i] = commands[i].size();
    }

    return {state, current_command, lengths};
}

void Tas::UpdateThread() {
    if (!Settings::values.tas_enable) {
        if (is_running) {
            Stop();
        }
        return;
    }

    if (manual_mode) {
        // Manual mode: keep the manual input applied, don't clear the input state
        // and don't advance loaded scripts. If recording is active, the manual input
        // is recorded frame by frame (used with frame advance to craft inputs).
        if (is_recording) {
            record_commands.push_back(manual_input);
        }
        return;
    }

    if (is_recording) {
        record_commands.push_back(last_input);
    }
    if (needs_reset) {
        current_command = 0;
        needs_reset = false;
        LoadTasFiles();
        LOG_DEBUG(Input, "tas_reset done");
    }

    if (!is_running) {
        ClearInput();
        return;
    }
    if (current_command < script_length) {
        LOG_DEBUG(Input, "Playing TAS {}/{}", current_command, script_length);
        const size_t frame = current_command++;
        for (size_t player_index = 0; player_index < commands.size(); player_index++) {
            TASCommand command{};
            if (frame < commands[player_index].size()) {
                command = commands[player_index][frame];
            }

            PadIdentifier identifier{
                .guid = Common::UUID{},
                .port = player_index,
                .pad = 0,
            };
            for (std::size_t i = 0; i < sizeof(command.buttons) * 8; ++i) {
                const bool button_status = (command.buttons & (1LLU << i)) != 0;
                const int button = static_cast<int>(i);
                SetButton(identifier, button, button_status);
            }
            SetTasAxis(identifier, TasAxis::StickX, command.l_axis.x);
            SetTasAxis(identifier, TasAxis::StickY, command.l_axis.y);
            SetTasAxis(identifier, TasAxis::SubstickX, command.r_axis.x);
            SetTasAxis(identifier, TasAxis::SubstickY, command.r_axis.y);
        }
    } else {
        is_running = Settings::values.tas_loop.GetValue();
        LoadTasFiles();
        current_command = 0;
        ClearInput();
    }
}

void Tas::ClearInput() {
    ResetButtonState();
    ResetAnalogState();
}

TasAnalog Tas::ReadCommandAxis(const std::string& line) const {
    std::vector<std::string> seg_list;
    {
        std::istringstream line_stream(line);
        std::string segment;
        while (std::getline(line_stream, segment, ';')) {
            seg_list.push_back(std::move(segment));
        }
    }

    if (seg_list.size() < 2) {
        LOG_ERROR(Input, "Invalid axis data: '{}'", line);
        return {};
    }

    try {
        const float x = std::stof(seg_list.at(0)) / 32767.0f;
        const float y = std::stof(seg_list.at(1)) / 32767.0f;
        return {x, y};
    } catch (const std::invalid_argument&) {
        LOG_ERROR(Input, "Invalid argument: '{}'", line);
    } catch (const std::out_of_range&) {
        LOG_ERROR(Input, "Out of range: '{}'", line);
    }
    return {};
}

u64 Tas::ReadCommandButtons(const std::string& line) const {
    std::istringstream button_text(line);
    std::string button_line;
    u64 buttons = 0;
    while (std::getline(button_text, button_line, ';')) {
        for (const auto& [text, tas_button] : text_to_tas_button) {
            if (text == button_line) {
                buttons |= static_cast<u64>(tas_button);
                break;
            }
        }
    }
    return buttons;
}

std::string Tas::WriteCommandButtons(u64 buttons) const {
    std::string returns;
    for (const auto& [text_button, tas_button] : text_to_tas_button) {
        if ((buttons & static_cast<u64>(tas_button)) != 0) {
            returns += fmt::format("{};", text_button);
        }
    }
    return returns.empty() ? "NONE" : returns;
}

std::string Tas::WriteCommandAxis(TasAnalog analog) const {
    return fmt::format("{};{}", analog.x * 32767, analog.y * 32767);
}

void Tas::SetTasAxis(const PadIdentifier& identifier, TasAxis axis, f32 value) {
    SetAxis(identifier, static_cast<int>(axis), value);
}

void Tas::StartStop() {
    if (!Settings::values.tas_enable) {
        return;
    }
    if (is_running) {
        Stop();
    } else {
        is_running = true;
    }
}

void Tas::Stop() {
    is_running = false;
}

void Tas::Reset() {
    if (!Settings::values.tas_enable) {
        return;
    }
    needs_reset = true;
}

bool Tas::Record() {
    if (!Settings::values.tas_enable) {
        return true;
    }
    is_recording = !is_recording;
    return is_recording;
}

void Tas::SaveRecording(bool overwrite_file) {
    if (is_recording) {
        return;
    }
    if (record_commands.empty()) {
        return;
    }
    WriteTasFile(u8"record.txt");
    if (overwrite_file) {
        WriteTasFile(u8"script0-1.txt");
    }
    needs_reset = true;
    record_commands.clear();
}

void Tas::SetManualMode(bool enabled) {
    if (manual_mode == enabled) {
        return;
    }
    manual_mode = enabled;
    if (enabled) {
        // Make sure the engine reflects the current manual input
        ApplyManualInput();
    } else {
        ClearInput();
    }
}

bool Tas::GetManualMode() const {
    return manual_mode;
}

void Tas::ManualSetButton(int button, bool value) {
    if (button < 0 || button >= static_cast<int>(sizeof(u64) * 8)) {
        return;
    }
    const u64 mask = 1ULL << button;
    if (value) {
        manual_input.buttons |= mask;
    } else {
        manual_input.buttons &= ~mask;
    }
    if (manual_mode) {
        const PadIdentifier identifier{
            .guid = Common::UUID{},
            .port = 0,
            .pad = 0,
        };
        SetButton(identifier, button, value);
    }
}

void Tas::ManualSetAxis(int axis, f32 value) {
    switch (axis) {
    case 0:
        manual_input.l_axis.x = value;
        break;
    case 1:
        manual_input.l_axis.y = value;
        break;
    case 2:
        manual_input.r_axis.x = value;
        break;
    case 3:
        manual_input.r_axis.y = value;
        break;
    default:
        return;
    }
    if (manual_mode) {
        const PadIdentifier identifier{
            .guid = Common::UUID{},
            .port = 0,
            .pad = 0,
        };
        SetAxis(identifier, axis, value);
    }
}

Tas::TASCommand Tas::GetLastInput() const {
    return last_input;
}

Tas::TASCommand Tas::GetManualInput() const {
    return manual_input;
}

void Tas::ApplyManualInput() {
    const PadIdentifier identifier{
        .guid = Common::UUID{},
        .port = 0,
        .pad = 0,
    };
    for (std::size_t i = 0; i < sizeof(manual_input.buttons) * 8; ++i) {
        const bool button_status = (manual_input.buttons & (1LLU << i)) != 0;
        SetButton(identifier, static_cast<int>(i), button_status);
    }
    SetAxis(identifier, 0, manual_input.l_axis.x);
    SetAxis(identifier, 1, manual_input.l_axis.y);
    SetAxis(identifier, 2, manual_input.r_axis.x);
    SetAxis(identifier, 3, manual_input.r_axis.y);
}

} // namespace InputCommon::TasInput
